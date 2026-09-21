# History

The history module adds **optional historical storage** so that metric
snapshots are persisted directly by pgexporter. Once enabled, pgexporter
periodically forks a *history worker* that takes a snapshot of all configured
metrics and writes them to a storage backend. A separate *retention worker*
periodically prunes records older than a configured threshold.

The history data is exposed through a JSON HTTP API served on a dedicated
port.

## Configuration

### PostgreSQL metrics history

To record a history of your PostgreSQL metrics, add the following to `pgexporter.conf`:

```ini
[pgexporter]

history           = 5005
history_interval  = 60s
history_retention = 30d
history_backend   = sqlite
history_path      = /var/lib/pgexporter/history.db
history_cert_file = /path/to/server.crt
history_key_file  = /path/to/server.key
history_ca_file   = /path/to/ca.crt
```

`history_cert_file`, `history_key_file` and `history_ca_file` are optional;
when omitted the history HTTP API is served over plain HTTP. If a configured
file does not exist, TLS is disabled and a warning is logged at startup.

### Bridge metrics history

To record a history of metrics collected from other pgexporter nodes via the bridge, add:

```ini
[pgexporter]

bridge_history           = 5006
bridge_history_interval  = 60s
bridge_history_retention = 30d
bridge_history_backend   = sqlite
bridge_history_path      = /var/lib/pgexporter/bridge_history.db
```

Both can be enabled at the same time on a combined instance.

The `history` and `bridge_history` settings are the ports on which the respective
history JSON APIs are served, following the same fork-per-request model as the
metrics, console and bridge endpoints.

If either port is unset (or `-1`), that history module is disabled.

### Snapshot interval

`history_interval` (and `bridge_history_interval`) set the minimum gap between
saved snapshots.

Whenever Prometheus (or any client) scrapes your `/metrics` endpoint, a snapshot
is **always** saved to the history database — regardless of the timer. The timer
only adds *automatic* snapshots on top of that. If a scrape already saved a
snapshot within the configured period, the automatic timer skips its turn to
avoid saving the same data twice.

Setting the interval to zero disables the automatic timer entirely. Snapshots
are then only saved when an outside client scrapes the endpoint.

The maximum supported interval is approximately **24.8 days**. Larger values are capped to that maximum and a warning is logged at startup.

## Backends

The storage backend is selected with `history_backend` (or `bridge_history_backend`
for bridge history). The currently supported backends are:

| Backend    | Value        | Description |
|------------|--------------|-------------|
| SQLite     | `sqlite`     | Default. Local file-based storage. |
| PostgreSQL | `postgresql` | Stores history in a PostgreSQL database. |

Either way the store, meaning the SQLite file or the PostgreSQL schema, is created
when pgexporter starts, so a missing database or a wrong credential is reported
right away at startup.

### SQLite

The SQLite backend stores history in the file pointed to by `history_path`
(`bridge_history_path` for bridge history). The database is opened with the
following hardcoded settings:

- `journal_mode = WAL` — write-ahead logging, so a scrape can read the history
  while a snapshot is being written without blocking.
- `busy_timeout = 5000` — when the database is momentarily locked (for example a
  snapshot insert racing an in-flight prune), the writer waits and retries for up
  to 5 seconds instead of failing immediately.
- `auto_vacuum = INCREMENTAL` — pages freed by pruning are placed on a free list.
  After each prune, up to 1000 free pages are returned to the operating system
  with `PRAGMA incremental_vacuum`, keeping the database file from growing
  unbounded while never holding the write lock for long.

### PostgreSQL

The PostgreSQL backend stores history in a PostgreSQL database instead of a local
file, so the history can be shared, backed up alongside your other databases, and
queried directly.

```ini
[pgexporter]

history_backend                  = postgresql
history_postgresql_host          = history.example.com
history_postgresql_port          = 5432
history_postgresql_database      = pgexporter_history
history_postgresql_user          = pgexporter
history_postgresql_password_file = /etc/pgexporter/pgexporter_history.conf
history_postgresql_tls           = on
history_postgresql_tls_ca_file   = /path/to/ca.crt
```

`history_postgresql_host` may be a directory rather than a hostname, in which case
it is treated as the location of a Unix domain socket and `history_postgresql_port`
selects the socket file.

The credential lives in its own encrypted file rather than in
`pgexporter_users.conf`, managed with the same tool:

```sh
pgexporter-admin -f /etc/pgexporter/pgexporter_history.conf -U pgexporter user add
```

`history_postgresql_user` is required, and the file must hold a single entry for that
user. Leave `history_postgresql_password_file` unset when the history server uses
`trust` or `peer` authentication.

#### Privileges

pgexporter creates and owns a `pgexporter` schema, holding the `series` and `sample`
tables, in the database it is pointed at, so the role needs `CREATE` on that
database as well as `CONNECT`. Because the tables are
then owned by that role, no further grants are needed - it can read, write and
prune its own objects.

```sql
CREATE DATABASE pgexporter_history;
CREATE USER pgexporter WITH PASSWORD 'secret';
GRANT CONNECT, CREATE ON DATABASE pgexporter_history TO pgexporter;
```

Creating the schema at startup is also why a dedicated database is recommended
rather than reusing an application database: `CREATE` on a database is a broad
privilege, and confining it to a database that holds nothing else keeps the blast
radius small.

The role does **not** need `pg_monitor`. That is a requirement of the roles used to
scrape monitored servers — pgexporter refuses to start when one of those lacks it —
but the history role only touches its own tables.

### Retention and pruning

`history_retention` (and `bridge_history_retention`) set how long records are
kept. Records older than the retention period are deleted by a pruning task that
runs on a fixed **hourly** tick, independent of the snapshot interval. A prune is
also run once at startup so a daemon that was down longer than its retention
period catches up immediately rather than waiting a full hour.

If `history_retention` is unset (disabled), records are kept forever and no
pruning is scheduled.


## Access

The history component exposes a JSON HTTP API on the `history` port:

```
GET /history/<metric_name>?timestamp=<epoch_seconds>&duration=<seconds>
```

Both query parameters are optional:

- `timestamp` — the anchor point of the query window, as a Unix epoch
  timestamp. Defaults to the current time.
- `duration` — the size of the window in seconds, relative to `timestamp`.
  May be negative to look backwards. Defaults to `-3600` (the last hour).

The queried window is `[timestamp + min(0, duration), timestamp + max(0,
duration)]`. For example:

```sh
# Last 10 minutes of pg_stat_database_xact_commit, ending now
curl http://localhost:5005/history/pg_stat_database_xact_commit?duration=-600

# A specific 10 minute window starting at a fixed timestamp
curl "http://localhost:5005/history/pg_stat_database_xact_commit?timestamp=1735689600&duration=600"
```

An unknown path returns `404`, an unparsable `timestamp`/`duration` returns
`400`, and a successful (possibly empty) query returns `200` with a JSON
array of matching records.

The endpoint supports TLS via the `history_cert_file`, `history_key_file`
and `history_ca_file` configuration keys (see [Configuration](#configuration)
below); when unset, the endpoint serves plain HTTP.
