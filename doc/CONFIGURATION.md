# pgexporter configuration

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgexporter/pgexporter.conf`.

The configuration of `pgexporter` is split into sections using the `[` and `]` characters.

The main section, called `[pgexporter]`, is where you configure the overall properties
of `pgexporter`.

Other sections doesn't have any requirements to their naming so you can give them
meaningful names like `[primary]` for the primary [PostgreSQL](https://www.postgresql.org)
instance.

All properties are in the format `key = value`.

The characters `#` and `;` can be used for comments; must be the first character on the line.
The `Bool` data type supports the following values: `on`, `1`, `true`, `off`, `0` and `false`.

See a [sample](./etc/pgexporter.conf) configuration for running `pgexporter` on `localhost`.

## [pgexporter]

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgexporter |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| metrics | | Int | Yes | The metrics port |
| management | 0 | Int | No | The remote management port (disable = 0) |
| cache | `on` | Bool | No | Cache connection |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level (fatal, error, warn, info, debug1, ..., debug5) |
| log_path | pgexporter.log | String | No | The log file location |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| blocking_timeout | 30 | Int | No | The number of seconds the process will be blocking for a connection (disable = 0) |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgexporter or root. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgexporter or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgexporter or root.  |
| libev | `auto` | String | No | Select the [libev](http://software.schmorp.de/pkg/libev.html) backend to use. Valid options: `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll` and `port` |
| buffer_size | 65535 | Int | No | The network buffer size (`SO_RCVBUF` and `SO_SNDBUF`) |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| non_blocking | on | Bool | No | Have `O_NONBLOCK` on sockets |
| backlog | 16 | Int | No | The backlog for `listen()`. Minimum `16` |
| hugepage | `try` | String | No | Huge page support (`off`, `try`, `on`) |
| pidfile | | String | No | Path to the PID file |

## Server section

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| user | | String | Yes | The user name |
| data_dir | | String | No | The location of the data directory |
| wal_dir | | String | No | The location of the WAL directory |

Note, that PostgreSQL 10+ is required.

Note, that if `host` starts with a `/` it represents a path and `pgexporter` will connect using a Unix Domain Socket.

# pgexporter_users configuration

The `pgexporter_users` configuration defines the users known to the system. This file is created and managed through
the `pgexporter-admin` tool.

The configuration is loaded from either the path specified by the `-u` flag or `/etc/pgexporter/pgexporter_users.conf`.

# pgexporter_admins configuration

The `pgexporter_admins` configuration defines the administrators known to the system. This file is created and managed through
the `pgexporter-admin` tool.

The configuration is loaded from either the path specified by the `-A` flag or `/etc/pgexporter/pgexporter_admins.conf`.

If pgexporter has both Transport Layer Security (TLS) and `management` enabled then `pgexporter-cli` can
connect with TLS using the files `~/.pgexporter/pgexporter.key` (must be 0600 permission),
`~/.pgexporter/pgexporter.crt` and `~/.pgexporter/root.crt`.
