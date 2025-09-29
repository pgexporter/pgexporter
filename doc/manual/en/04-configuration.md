\newpage

# Configuration

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgexporter/pgexporter.conf`.

The configuration of [**pgexporter**][pgexporter] is split into sections using the `[` and `]` characters.

The main section, called `[pgexporter]`, is where you configure the overall properties of [**pgexporter**][pgexporter].

Other sections doesn't have any requirements to their naming so you can give them meaningful names like `[primary]` for the primary [PostgreSQL][postgresql] instance.

All properties are in the format `key = value`.

The characters `#` and `;` can be used for comments; must be the first character on the line.

The `Bool` data type supports the following values: `on`, `yes`, `1`, `true`, `off`, `no`, `0` and `false`.

See a [sample][sample] configuration for running [**pgexporter**][pgexporter] on `localhost`.

## [pgexporter]

| Property              |Default|Unit  |Required| Description |
|-----------------------|-------|------|--------|-------------|
| host | | String | Yes | The bind address for pgexporter |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| metrics | | Int | Yes | The metrics port |
| metrics_path | | String | No | Path to customized metrics (either a YAML file or a directory with YAML files) |
| metrics_cache_max_age | 0 | String | No | The number of seconds to keep in cache a Prometheus (metrics) response. If set to zero, the caching will be disabled. Can be a string with a suffix, like `2m` to indicate 2 minutes |
| metrics_cache_max_size | 256k | String | No | The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart. This parameter determines the size of memory allocated for the cache even if `metrics_cache_max_age` or `metrics` are disabled. Its value, however, is taken into account only if `metrics_cache_max_age` is set to a non-zero value. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| bridge | | Int | No | The bridge port |
| bridge_endpoints | | String | No | A comma-separated list of bridge endpoints specified by host:port |
| bridge_cache_max_age | `5m` | String | No | The number of seconds to keep in cache a Prometheus (bridge) response. If set to zero, the caching will be disabled. Can be a string with a suffix, like `2m` to indicate 2 minutes |
| bridge_cache_max_size | `10M` | String | No | The maximum amount of data to keep in cache when serving bridge responses. Changes require restart. This parameter determines the size of memory allocated for the cache even if `bridge_cache_max_age` or `bridge` are disabled. Its value, however, is taken into account only if `bridge_cache_max_age` is set to a non-zero value. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| bridge_json | | Int | No | The bridge JSON port |
| bridge_json_cache_max_size | `10M` | String | No | The maximum amount of data to keep in cache when serving bridge JSON responses. Changes require restart. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| management | 0 | Int | No | The remote management port (disable = 0) |
| cache | `on` | Bool | No | Cache connection |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgexporter.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| log_rotation_age | 0 | String | No | The age that will trigger a log file rotation. If expressed as a positive number, is managed as seconds. Supports suffixes: 'S' (seconds, the default), 'M' (minutes), 'H' (hours), 'D' (days), 'W' (weeks). A value of `0` disables. |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes). A value of `0` (with or without suffix) disables. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| blocking_timeout | 30 | Int | No | The number of seconds the process will be blocking for a connection (disable = 0) |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgexporter or root. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgexporter or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgexporter or root.  |
| metrics_cert_file | | String | No | Certificate file for TLS for Prometheus metrics. This file must be owned by either the user running pgexporter or root. |
| metrics_key_file | | String | No | Private key file for TLS for Prometheus metrics. This file must be owned by either the user running pgexporter or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| metrics_ca_file | | String | No | Certificate Authority (CA) file for TLS for Prometheus metrics. This file must be owned by either the user running pgexporter or root.  |
| libev | `auto` | String | No | Select the [libev](http://software.schmorp.de/pkg/libev.html) backend to use. Valid options: `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll` and `port` |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| non_blocking | on | Bool | No | Have `O_NONBLOCK` on sockets |
| backlog | 16 | Int | No | The backlog for `listen()`. Minimum `16` |
| hugepage | `try` | String | No | Huge page support (`off`, `try`, `on`) |
| pidfile | | String | No | Path to the PID file |
| update_process_title | `verbose` | String | No | The behavior for updating the operating system process title. Allowed settings are: `never` (or `off`), does not update the process title; `strict` to set the process title without overriding the existing initial process title length; `minimal` to set the process title to the base description; `verbose` (or `full`) to set the process title to the full description. Please note that `strict` and `minimal` are honored only on those systems that do not provide a native way to set the process title (e.g., Linux). On other systems, there is no difference between `strict` and `minimal` and the assumed behaviour is `minimal` even if `strict` is used. `never` and `verbose` are always honored, on every system. On Linux systems the process title is always trimmed to 255 characters, while on system that provide a natve way to set the process title it can be longer. |
| extensions | | String | No | Comma-separated list of extensions to enable globally. If specified, only listed extensions will be enabled for metrics collection. If empty, all extensions are disabled. If not specified, all detected extensions are enabled by default. |

## Server section

| Property       | Default | Unit | Required | Applies To | Description |
|----------------|---------|------|----------|------------|-------------|
| host | | String | Yes | All | The address of the PostgreSQL instance or Prometheus endpoint |
| port | | Int | Yes | All | The port of the PostgreSQL instance or Prometheus endpoint |
| type | postgresql | String | No | All | The server type: `postgresql` or `prometheus` |
| user | | String | Conditional | PostgreSQL | The user name. Required for `postgresql` type |
| data_dir | | String | No | PostgreSQL | The location of the data directory |
| wal_dir | | String | No | PostgreSQL | The location of the WAL directory |
| tls_cert_file | | String | No | All | Certificate file for TLS. This file must be owned by either the user running pgexporter or root. |
| tls_key_file | | String | No | All | Private key file for TLS. This file must be owned by either the user running pgexporter or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | All | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgexporter or root.  |
| extensions | | String | No | PostgreSQL | Comma-separated list of extensions to enable for this specific server. Overrides global extensions setting. If specified, only listed extensions will be enabled. If empty, all extensions are disabled for this server. |

Note, that PostgreSQL 13+ is required for PostgreSQL servers.

Note, that if `host` starts with a `/` it represents a path and [**pgexporter**][pgexporter] will connect using a Unix Domain Socket.

## Server Types

The `type` property defines how [**pgexporter**][pgexporter] interacts with the server:

- **`postgresql`** (default): PostgreSQL database server for direct metrics collection. Requires `user` property and establishes database connections.
- **`prometheus`**: Prometheus-compatible HTTP endpoint for metrics scraping. No `user` property required. Metrics pass through unchanged.

## Extension Configuration

The `extensions` property allows fine-grained control over which PostgreSQL extensions are monitored for metrics collection.

**Behavior:**
- **Not specified**: All detected extensions are enabled (default behavior)
- **Empty value** (`extensions =`): All extensions are disabled
- **Comma-separated list** (`extensions = pg_stat_statements, timescaledb`): Only listed extensions are enabled

**Hierarchy:**
- Server-specific `extensions` setting overrides global setting
- Global `extensions` setting applies to all servers without specific configuration
- Default behavior when no configuration is present

**Examples:**
```
# Enable only specific extensions globally
[pgexporter]
extensions = pg_stat_statements, timescaledb

# Override for specific server
[primary]
extensions = pg_stat_statements

# Disable all extensions for a server
[replica1]
extensions = 

# No extensions setting - inherits global or default
[replica2]
```
## pgexporter_users configuration

The `pgexporter_users` configuration defines the users known to the system. This file is created and managed through the `pgexporter-admin` tool.

The configuration is loaded from either the path specified by the `-u` flag or `/etc/pgexporter/pgexporter_users.conf`.

## pgexporter_admins configuration

The `pgexporter_admins` configuration defines the administrators known to the system. This file is created and managed through the `pgexporter-admin` tool.

The configuration is loaded from either the path specified by the `-A` flag or `/etc/pgexporter/pgexporter_admins.conf`.

If pgexporter has both Transport Layer Security (TLS) and `management` enabled then `pgexporter-cli` can connect with TLS using the files `~/.pgexporter/pgexporter.key` (must be 0600 permission), `~/.pgexporter/pgexporter.crt` and `~/.pgexporter/root.crt`.

## Configuration Directory

You can specify a directory for all configuration files using the `-D` flag (or `--directory`).
Alternatively, you can set the `PGEXPORTER_CONFIG_DIR` environment variable to define the configuration directory.

**Behavior:**
- When the directory flag (`-D`) is set, pgexporter will look for all configuration files in the specified directory.
- If a required file is not found in the specified directory, pgexporter will look for it in its default location (e.g., `/etc/pgexporter/pgexporter.conf`).
- If the file is not found in either location:
  - If the file is mandatory, pgexporter will log an error and fail to start.
  - If the file is optional, pgexporter will log a warning and continue without it.
- All file lookup attempts and missing files are logged for troubleshooting.

**Precedence Rules:**
- Individual file flags (such as `-c`, `-u`, `-A`, etc.) always take precedence over the directory flag and environment variable for their respective files.
- The directory flag (`-D`) takes precedence over the environment variable (`PGEXPORTER_CONFIG_DIR`).
- If neither the directory flag nor individual file flags are set, pgexporter uses the default locations for all configuration files.

**Using the Environment Variable:**
1. Set the environment variable before starting pgexporter:
``` sh
export PGEXPORTER_CONFIG_DIR=/path/to/config_dir
pgexporter -d
```

2. If both the environment variable and the `-D` flag are set, the flag takes precedence.

**Example:**
``` sh
pgexporter -D /custom/config/dir -d
```
or
``` sh
export PGEXPORTER_CONFIG_DIR=/custom/config/dir
pgexporter -d
```

Refer to logs for details about which configuration files were loaded and from which locations.