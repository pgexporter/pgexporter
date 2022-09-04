# Customized Metrics configuration

The customized metrics yaml configuration is loaded from the the path specified by the `metrics_path` option in `pgexporter.conf`. If the `metrics_path` is specified, the metrics include some basic metrics and the customized metrics. 

The `metrics_path` can either be a single yaml files or the directory of multiple yaml files.

Each yaml is sequcence of queries. Every query can include four parts. See a [sample_metrics](./etc/postgresql-10.yaml) for running basic metrics.

## metrics yaml
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| query | | Yes | The query sql of metric |
| tag | | Yes | The tag of metric |
| server  | both | No | The query on which server type. Valid options: `both`, `primary`, `replica` |
| sort | name | No | The sort type of metric. Valid options: `name`, `data` |
| columns | | Yes | The specific information of columns  | 


## columns 
| Property | Default | Required | Description |
|----------|---------|----------|-------------|
| name | | No | The name of this column |
| type | | Yes | The type of column. Valid options: `label`, `gauge`, `counter` |
| description |  | No | The description of this column |