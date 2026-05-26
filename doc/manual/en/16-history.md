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
```

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

## Backends

The storage backend is selected with `history_backend` (or `bridge_history_backend`
for bridge history). The currently supported backends are:

| Backend | Value    | Description |
|---------|----------|-------------|
| SQLite  | `sqlite` | Default. Local file-based storage. |


## Access

The history component acts as a JSON HTTP endpoint. You can access it with

```sh
curl http://localhost:5005/metrics?name=pg_stat_database_xact_commit&from=-1h
```

The endpoint accepts a metric name and a time window and returns the
matching records as JSON.

Bridge history is available on its own port:

```sh
curl http://localhost:5006/metrics?name=pg_stat_database_xact_commit&from=-1h
```

## Console integration

When `history` is set, the web console queries the history backend for
recent data instead of performing a live scrape. When `history` is not set,
the console behaves exactly as it does today.
