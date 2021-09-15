==========
pgexporter
==========

----------------------------------
Prometheus exporter for PostgreSQL
----------------------------------

:Manual section: 1

SYNOPSIS
========

pgexporter [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]

DESCRIPTION
===========

Prometheus exporter for PostgreSQL

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgexporter.conf file

-u, --users USERS_FILE
  Set the path to the pgexporter_users.conf file

-A, --admins ADMINS_FILE
  Set the path to the pgexporter_admins.conf file

-d, --daemon
  Run as a daemon

-V, --version
  Display version information

-?, --help
  Display help

REPORTING BUGS
==============

pgexporter is maintained on GitHub at https://github.com/pgexporter/pgexporter

COPYRIGHT
=========

pgexporter is licensed under the 3-clause BSD License.

SEE ALSO
========

pgexporter.conf(5), pgexporter-cli(1), pgexporter-admin(1)
