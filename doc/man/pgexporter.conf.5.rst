===============
pgexporter.conf
===============

--------------------------------------
Main configuration file for pgexporter
--------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgexporter.conf is the main configuration file for pgexporter.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgexporter]``.

Other sections specifies the PostgreSQL server configuration.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the main section are

host
  The bind address for pgexporter. Mandatory

unix_socket_dir
  The Unix Domain Socket location. Mandatory

metrics
  The metrics port. Mandatory

metrics_path
  Path to customized metrics (either a YAML file or a directory with YAML files)

metrics_cache_max_age
  The number of seconds to keep in cache a Prometheus (metrics) response.
  If set to zero, the caching will be disabled. Can be a string with a suffix, like ``2m`` to indicate 2 minutes.
  Default is 0 (disabled)

metrics_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart.
  This parameter determines the size of memory allocated for the cache even if metrics_cache_max_age or
  metrics are disabled. Its value, however, is taken into account only if metrics_cache_max_age is set
  to a non-zero value. Supports suffixes: B (bytes), the default if omitted, K or KB (kilobytes),
  M or MB (megabytes), G or GB (gigabytes).
  Default is 256k

metrics_query_timeout
  The timeout in milliseconds for metric SQL queries.
  If set to 0, no timeout is applied. Minimum value is 50ms when set
  Default is 0

bridge
  The bridge port

bridge_endpoints
  A comma-separated list of bridge endpoints specified by host:port

bridge_cache_max_age
  The number of seconds to keep in cache a Prometheus (bridge) response.
  If set to zero, the caching will be disabled. Can be a string with a suffix, like ``2m`` to indicate 2 minutes.
  Default is ``5m``

bridge_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart.
  If set to zero, the caching will be disabled. Supports suffixes: B (bytes), the default if omitted,
  K or KB (kilobytes), M or MB (megabytes), G or GB (gigabytes).
  Default is 10M

bridge_json
  The bridge JSON port

bridge_json_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus JSON responses. Changes require restart.
  If set to zero, the caching will be disabled. Supports suffixes: B (bytes), the default if omitted,
  K or KB (kilobytes), M or MB (megabytes), G or GB (gigabytes).
  Default is 10M

management
  The remote management port. Default is 0 (disabled)

cache
  Cache connection. Default is on

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level, any of the (case insensitive) strings FATAL, ERROR, WARN, INFO and DEBUG
  (that can be more specific as DEBUG1 thru DEBUG5). Debug level greater than 5 will be set to DEBUG5.
  Not recognized values will make the log_level be INFO. Default is info

log_path
  The log file location. Default is pgexporter.log. Can be a strftime(3) compatible string

log_rotation_age
  The age that will trigger a log file rotation. If expressed as a positive number, is managed as seconds.
  Supports suffixes: S (seconds, the default), M (minutes), H (hours), D (days), W (weeks).
  A value of 0 disables. Default is 0 (disabled)

log_rotation_size
  The size of the log file that will trigger a log rotation. Supports suffixes: B (bytes), the default if omitted,
  K or KB (kilobytes), M or MB (megabytes), G or GB (gigabytes). A value of 0 (with or without suffix) disables.
  Default is 0

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces.
  Default is %Y-%m-%d %H:%M:%S

log_mode
  Append to or create the log file (append, create). Default is append

blocking_timeout
  The number of seconds the process will be blocking for a connection (disable = 0). Default is 30

tls
  Enable Transport Layer Security (TLS). Default is false

tls_cert_file
  Certificate file for TLS

tls_key_file
  Private key file for TLS

tls_ca_file
  Certificate Authority (CA) file for TLS

metrics_cert_file
  Certificate file for TLS for Prometheus metrics

metrics_key_file
  Private key file for TLS for Prometheus metrics

metrics_ca_file
  Certificate Authority (CA) file for TLS for Prometheus metrics

libev
  The libev backend to use. Valid options: auto, select, poll, epoll, iouring, devpoll and port. Default is auto

keep_alive
  Have SO_KEEPALIVE on sockets. Default is on

nodelay
  Have TCP_NODELAY on sockets. Default is on

non_blocking
  Have O_NONBLOCK on sockets. Default is on

backlog
  The backlog for listen(). Minimum 16. Default is 16

hugepage
  Huge page support. Default is try

pidfile
  Path to the PID file

update_process_title
  The behavior for updating the operating system process title. Allowed settings are: never (or off),
  does not update the process title; strict to set the process title without overriding the existing
  initial process title length; minimal to set the process title to the base description; verbose (or full)
  to set the process title to the full description. Please note that strict and minimal are honored
  only on those systems that do not provide a native way to set the process title (e.g., Linux).
  On other systems, there is no difference between strict and minimal and the assumed behaviour is minimal
  even if strict is used. never and verbose are always honored, on every system. On Linux systems the
  process title is always trimmed to 255 characters, while on system that provide a natve way to set the
  process title it can be longer. Default is verbose

The options for the PostgreSQL section are

host
  The address of the PostgreSQL instance. Mandatory

port
  The port of the PostgreSQL instance. Mandatory
  
user
  The user name for the replication role. Mandatory

data_dir
  The location of the data directory

wal_dir
  The location of the WAL directory

REPORTING BUGS
==============

pgexporter is maintained on GitHub at https://github.com/pgexporter/pgexporter

COPYRIGHT
=========

pgexporter is licensed under the 3-clause BSD License.

SEE ALSO
========

pgexporter(1), pgexporter-cli(1), pgexporter-admin(1)
