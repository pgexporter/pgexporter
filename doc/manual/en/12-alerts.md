\newpage

# Alerts

[**pgexporter**][pgexporter] includes a built-in alert system that monitors the health of your
[**PostgreSQL**][postgresql] instances and exposes alert states as Prometheus metrics.

Alerts let you detect problems — such as a server going down, connections running out, or
replication falling behind — early, before they impact users.

Each alert evaluates a condition and publishes the result on the `/metrics` endpoint as a
Prometheus gauge (`1` = firing, `0` = OK). You can query them at the `/metrics` endpoint
or visualize them in a Grafana dashboard.
To receive notifications when an alert fires (for example, via a Slack webhook), see
the [Grafana](09-grafana.md) chapter.

Several commonly needed alerts (server down, high connection usage, replication lag, etc.) are
provided out-of-the-box and require no configuration. For custom monitoring needs you can define
additional alerts — or override the built-in defaults — in a YAML file.

## Configuration

The customized alerts YAML configuration is loaded from the path specified by the `alerts_path`
option in `pgexporter.conf` or via the `--alerts` command-line flag. If `alerts_path` is
specified (either way), alerting is automatically enabled and the built-in alerts are loaded
along with the customized alerts.

The `alerts_path` must be a single YAML file.

Each YAML file is a sequence of alerts. Every alert can include seven properties.

## Alerts YAML

| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| name | | Yes | The unique identifier of the alert |
| description | | Yes | Human-readable HELP description. |
| type | | Yes | `connection` or `query`. |
| query | | No | The SQL query to execute (only for `type: query`). Should return a single integer value. |
| operator | `>` | No | The comparison operator (only for `type: query`). Valid options: `>`, `<`, `>=`, `<=`, `==`, `!=` |
| threshold | | Yes | The integer threshold value to compare against (only for `type: query`). |
| servers | `all` | No | Target servers for the alert. Can be `all`, a specific server name (e.g., `primary`), or a list of server names (`[primary, replica]`). |

**Note:** The `threshold` property is defined as an integer — for example, `80` for 80% of `max_connections`, or `1500000000` for a transaction ID age limit. Queries that return a percentage should scale the result to a whole number, for example `SELECT (count(*) * 100 / max_conn) FROM ...` so that a threshold of `80` means 80%.

\* Required when defining a **new** alert. Not required when overriding a built-in alert, since unspecified fields keep their built-in values.

### Built-in Alerts

[**pgexporter**][pgexporter] comes with several pre-configured, built-in alerts that apply uniformly out-of-the-box unless overridden:

*   `postgresql_down`: The target PostgreSQL server is down or unreachable (`type: connection`).
*   `connections_high`: Active connections exceed 80% of `max_connections` (threshold: 80).
*   `replication_lag`: Replication lag (in seconds) exceeds 300 seconds.
*   `wal_receiver_down`: The WAL receiver process on a replica is not running.
*   `replication_slot_inactive`: There are replication slots, but none are active.
*   `xid_wraparound`: Transaction ID age exceeds 1,500,000,000.
*   `multixact_wraparound`: MultiXact age exceeds 1,500,000,000.

### Built-in Overrides Structure

When defining an alert with the same `name` as an existing built-in alert, [**pgexporter**][pgexporter] merges your custom values.
You can override any field of a built-in alert by providing a custom value for it. This allows for complete customization of built-in alerts.

### Server Targeting Logic

The `servers` property controls which instances the alert applies to. You can specify:

*   A single server: `servers: primary`
*   A list of servers: `servers: [primary, replica]`
*   All servers: `servers: all`

### Example

**Example YAML definition (`alerts.yaml`):**

```yaml
alerts:
  # Override built-in connection alert
  - name: postgresql_down
    threshold: 1
    servers: primary

  # Override built-in connections_high threshold to 90%
  - name: connections_high
    threshold: 90

  # Custom new query alert
  - name: replica_lag_high
    description: Replication lag exceeds 500MB
    type: query
    query: "SELECT pg_wal_lsn_diff(pg_current_wal_lsn(), replay_lsn) FROM pg_stat_replication"
    operator: ">"
    threshold: 524288000
    servers: [replica]
```
