\newpage

# Custom metrics

[**pgexporter**][pgexporter] can use custom YAML files to define the queries against
[**PostgreSQL**][postgresql].

## Configuration

Change `pgexporter.conf` to add

```
metrics_path=[PATH_TO_YAML_FILE_DIR]
```

or run the command with `-Y` options.

## Get metrics

You can now access the metrics via

```
http://localhost:5002/metrics
```

## YAML

The [YAML Example Directory][examples] has a lot of example YAML files
that can be used. Here's an example of a general structure:

```yaml
version: 13
metrics:
  - tag: ...
    sort: ...
    collector: ...
    server: ...
    queries:
      - query: SELECT * ...
        version: 14
        columns:
          - name: ...
            description: ...
            type: ...
          - name: ...
            description: ...
            type: ...
      - query: SELECT * ...
        columns:
          - name: ...
            description: ...
            type: ...
          - name: ...
            description: ...
            type: ...
```

Here's a breakdown of this YAML:

* `version`: This refers to the topmost `version` provided in the YAML. This is the default `version` value. The "queries" below each have a version associated with it (explained later). If the query does not specify a version, this is the default value.
* `metrics`: Contains all the metrics.
* `database`: If it has the value `all`, then the metrics are collected from all present databases on the cluster that are accessible to the account used. For all other values, only `postgres` database is queried.
* `queries`: Contains all the query alternative. For a given server with version, the query alternative with the closest and smaller or equal version will be chosen. For example, if there are alternatives with the following versions `{16, 15, 12, 11}` then for server with version `13`, the query with version `12` is chosen.
* `query`: This contains the SQL query string.
* `columns`: A list of all the columns that the given SQL query's results will contain.
* `name`: Any name of preference for the column. If it is a metric column (ie. a column with type as counter/gauge/histogram), then the name will be appended to the collector name. For label columns, it's required to provide the name. For metric columns, it's not required to provide the name if there's only one metric column in the whole list of columns for one query. But, for more than one metric columns, it's required to provide the names.
* `description`: An optional description of the column. Label columns don't require descriptions, however, metric columns do require a description.
* `type`: It can be `counter`, `gauge`, `histogram` or `label`.

The customized metrics yaml configuration is loaded from the the path specified by the `metrics_path` option in `pgexporter.conf`. If the `metrics_path` is specified, the metrics include some basic metrics and the customized metrics. 

The `metrics_path` can either be a single yaml files or the directory of multiple yaml files.

Each yaml is sequcence of queries. Every query can include four parts. See a [sample_metrics](../contrib/yaml/postgresql-12.yaml) for running basic metrics.

### metrics
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| query | | Yes | The query sql of the metrics |
| tag | | Yes | The tag of the metrics |
| columns | | Yes | The column information  |
| server  | `both` | No | The query on which server type. Valid options: `both`, `primary`, `replica` |
| sort | `name` | No | The sort type of the metrics. Valid options: `name`, `data` |


### columns
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| type | | Yes | The type of column. Valid options: `label`, `gauge`, `counter`, `histogram` |
| name | | No | The name of this column |
| description |  | No | The description of this column |

For the `histogram` type, the column names of the query should be `X`, `X_bucket`, `X_sum` and `X_count`, . The `X` is the name property of histogram columns. The specific meaning of the column names are the following:

| Column Name |  Description |
|----------|---------|
| X        | The "le" values |
| X_bucket | The bucket values |
| X_sum    | The histogram sum |
| X_count  | The histogram count |


The customized metrics configuration is loaded from the path specified by the `metrics_path` option in `pgexporter.conf`. If the `metrics_path` is specified, the metrics include some basic metrics and the customized metrics.

The `metrics_path` can either be a single file or a directory of multiple files. pgexporter supports both YAML (*.yaml, *.yml) and JSON (*.json) formats for metrics configuration.

## JSON

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

### Example

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
* `pgexporter_pg_stat_database_xact_commit` - Shows committed transactions by database
* `pgexporter_pg_stat_database_xact_rollback` - Shows rolled back transactions by database
