\newpage

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

| Backend | Value    | Description |
|---------|----------|-------------|
| SQLite  | `sqlite` | Default. Local file-based storage. |


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
and `history_ca_file` configuration keys; when unset, the endpoint serves
plain HTTP.

Bridge history (`bridge_history`) does not yet expose an HTTP API.

## Console integration

When `history` is set, the web console queries the history backend for
recent data instead of performing a live scrape. When `history` is not set,
the console behaves exactly as it does today.
