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

management
  The remote management port. Default is 0 (disabled)

cache
  Cache connection. Default is on

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level (fatal, error, warn, info, debug1, ..., debug5). Default is info

log_path
  The log file location. Default is pgexporter.log

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

libev
  The libev backend to use. Valid options: auto, select, poll, epoll, iouring, devpoll and port. Default is auto

buffer_size
  The network buffer size (SO_RCVBUF and SO_SNDBUF). Default is 65535

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
