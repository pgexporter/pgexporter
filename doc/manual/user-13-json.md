# Customized Metrics Configuration

The customized metrics configuration is loaded from the path specified by the `metrics_path` option in `pgexporter.conf`. If the `metrics_path` is specified, the metrics include some basic metrics and the customized metrics.

The `metrics_path` can either be a single file or a directory of multiple files. pgexporter supports both YAML (*.yaml, *.yml) and JSON (*.json) formats for metrics configuration.

## JSON Metrics Configuration

Each JSON file contains a root object with a metrics array. See a [sample JSON metrics file](../contrib/json/postgresql-12.json) for running basic metrics.

### Root Object Properties
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| version  | | No | Default PostgreSQL version if not specified in queries |
| metrics  | | Yes | Array of metric objects |

### Metric Object Properties
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| tag | | Yes | The tag of the metrics |
| collector | | Yes | The collector name for this metric |
| queries | | Yes | Array of query objects |
| server  | `both` | No | The query on which server type. Valid options: `both`, `primary`, `replica` |
| sort | `name` | No | The sort type of the metrics. Valid options: `name`, `data` |

### Query Object Properties
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| query | | Yes | The SQL query for the metrics |
| version | parent's version | No | PostgreSQL version this query is compatible with |
| columns | | Yes | Array of column objects |
| is_histogram | false | No | Whether this query produces histogram data |

### Column Object Properties
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| type | | Yes | The type of column. Valid options: `label`, `gauge`, `counter`, `histogram` |
| name | | No | The name of this column |
| description | | No | The description of this column |

For the `histogram` type, the column names of the query should be `X`, `X_bucket`, `X_sum` and `X_count`, where `X` is the name property of histogram columns. The specific meaning of the column names are the following:

| Column Name | Description |
|-------------|-------------|
| X           | The "le" values |
| X_bucket    | The bucket values |
| X_sum       | The histogram sum |
| X_count     | The histogram count |

## Example JSON Configuration

```json
{
  "version": 12,
  "metrics": [
    {
      "tag": "pg_stat_database",
      "collector": "database_stats",
      "sort": "name",
      "server": "both",
      "queries": [
        {
          "query": "SELECT datname, xact_commit, xact_rollback FROM pg_stat_database WHERE datname != 'template0'",
          "columns": [
            {
              "name": "datname",
              "type": "label",
              "description": "Database name"
            },
            {
              "name": "xact_commit",
              "type": "counter",
              "description": "Number of committed transactions"
            },
            {
              "name": "xact_rollback",
              "type": "counter",
              "description": "Number of rolled back transactions"
            }
          ]
        }
      ]
    }
  ]
}
```
After setting up this configuration, Prometheus will be able to, among other things, query metrics like:
- `pgexporter_pg_stat_database_xact_commit` - Shows committed transactions by database
- `pgexporter_pg_stat_database_xact_rollback` - Shows rolled back transactions by database
