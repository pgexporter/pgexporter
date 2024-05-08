\newpage

# Customized Metrics configuration

The customized metrics yaml configuration is loaded from the the path specified by the `metrics_path` option in `pgexporter.conf`. If the `metrics_path` is specified, the metrics include some basic metrics and the customized metrics. 

The `metrics_path` can either be a single yaml files or the directory of multiple yaml files.

Each yaml is sequcence of queries. Every query can include four parts. See a [sample_metrics](../contrib/yaml/postgresql-12.yaml) for running basic metrics.

## metrics yaml
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| query | | Yes | The query sql of the metrics |
| tag | | Yes | The tag of the metrics |
| columns | | Yes | The column information  | 
| server  | `both` | No | The query on which server type. Valid options: `both`, `primary`, `replica` |
| sort | `name` | No | The sort type of the metrics. Valid options: `name`, `data` |


## columns 
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
