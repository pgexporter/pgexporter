# Custom metrics for pgexporter

This tutorial will show how to set custom metrics for pgexporter.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgexporter.

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
