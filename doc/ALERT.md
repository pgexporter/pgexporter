# Customized Alerts configuration

The customized alerts yaml configuration is loaded from the path specified by the `alerts_path` option in `pgexporter.conf`. If `alerts_path` is specified and `alerts = on`, the alerts include some basic alerts and the customized alerts.

The `alerts_path` can either be a single yaml file or the directory of multiple yaml files.

Each yaml is a sequence of alerts. Every alert can include seven properties.

## alerts yaml

| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| name | | Yes | The unique identifier of the alert |
| description | | Yes* | Human-readable HELP description. |
| type | | Yes* | `connection` or `query`. |
| query | | No | The SQL query to execute (only available for `type: query`). Should return a single numeric value. |
| operator | `>` | No | The comparison operator. Valid options: `>`, `<`, `>=`, `<=`, `==`, `!=` |
| threshold | | Yes | The numeric threshold value to compare against. |
| servers | `all` | No | Target servers for the alert. Can be `all`, a specific server name (e.g., `primary`), or a list of server names (`[primary, replica]`). |

### Built-in Alerts
`pgexporter` comes with several pre-configured, built-in alerts that apply uniformly out-of-the-box unless overridden:
*   `pgexporter_down`: `pgexporter` itself is unreachable (always targeted as `type: connection`).
*   `postgresql_down`: The target PostgreSQL server is down or unreachable (`type: connection`).
*   `connections_high`: Active connections exceed 80% of `max_connections` (threshold: 0.8).
*   `replication_lag`: Replication lag (in seconds) exceeds 300 seconds.
*   `wal_receiver_down`: The WAL receiver process on a replica is not running.
*   `replication_slot_inactive`: There are replication slots, but none are active.
*   `xid_wraparound`: Transaction ID age exceeds 1,500,000,000.
*   `multixact_wraparound`: MultiXact age exceeds 1,500,000,000.

### Built-in Overrides Structure
When defining an alert with the same `name` as an existing built-in alert, `pgexporter` merges your custom values.
You can override any field of a built-in alert by providing a custom value for it. This allows for complete customization of built-in alerts.

### Server Targeting Logic
The `servers` property controls which instances the alert applies to. You can specify:
*   A single server: `servers: primary`
*   A list of servers: `servers: [primary, replica]`
*   All servers: `servers: all`

**Example YAML definition (`alerts.yaml`):**
```yaml
alerts:
  # Override built-in connection alert
  - name: postgresql_down
    threshold: 1
    servers: primary

  # Custom new query alert
  - name: replica_lag_high
    description: Replication lag exceeds 500MB
    type: query
    query: "SELECT pg_wal_lsn_diff(pg_current_wal_lsn(), replay_lsn) FROM pg_stat_replication"
    operator: ">"
    threshold: 524288000
    servers: [replica]
```
