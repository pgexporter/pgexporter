# Custom metrics for pgexporter

This tutorial will show how to set custom metrics for pgexporter.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 12+ and pgexporter.

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf install -y postgresql10 postgresql10-server pgexporter
```

## Change pgexporter configuration

Change `pgexporter.conf` to add

```
metrics_path=[PATH_TO_YAML_FILE_DIR]
```

or run the command with `-Y` options.

(`pgexporter` user)


## Get Prometheus metrics

You can now access the metrics via

```
http://localhost:5002/metrics
```

## YAML Structure

The [YAML Example Directory](../../contrib/yaml) has a lot of example YAML files
that can be used. Here's an example of a general structure:

```yaml
version: 12
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

- `version`: This refers to the topmost `version` provided in the YAML. This is the default `version` value. The "queries" below each have a version associated with it (explained later). If the query does not specify a version, this is the default value.
- `metrics`: Contains all the metrics.
- `queries`: Contains all the query alternative. For a given server with version, the query alternative with the closest and smaller or equal version will be chosen. For example, if there are alternatives with the following versions `{16, 15, 12, 11}` then for server with version `13`, the query with version `12` is chosen.
- `query`: This contains the SQL query string.
- `columns`: A list of all the columns that the given SQL query's results will contain.
- `name`: Any name of preference for the column. If it is a metric column (ie. a column with type as counter/gauge/histogram), then the name will be appended to the collector name. For label columns, it's required to provide the name. For metric columns, it's not required to provide the name if there's only one metric column in the whole list of columns for one query. But, for more than one metric columns, it's required to provide the names.
- `description`: An optional description of the column. Label columns don't require descriptions, however, metric columns do require a description.
- `type`: It can be `counter`, `gauge`, `histogram` or `label`.
