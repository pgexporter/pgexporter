==============
pgexporter-cli
==============

-----------------------------------
Command line utility for pgexporter
-----------------------------------

:Manual section: 1

SYNOPSIS
========

pgexporter-cli [ -c CONFIG_FILE ] [ COMMAND ]

DESCRIPTION
===========

pgexporter-cli is a command line utility for pgexporter.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgexporter.conf file

-h, --host HOST
  Set the host name

-p, --port PORT
  Set the port number

-U, --user USERNAME
  Set the user name

-P, --password PASSWORD
  Set the password

-L, --logfile FILE
  Set the logfile

-v, --verbose
  Output text string of result

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

ping
  Is pgexporter alive

shutdown
  Shutdown pgexporter

status
  Status of pgexporter

status details
  Detailed status of pgexporter

conf reload
  Reload the configuration from disk. Structural parameter changes require a restart.

conf [ls]
  To print the configurations used

conf [get]
  To obtain information about a runtime configuration value

conf set <key> <value>
  To modify a runtime configuration value. Returns structured feedback:
  success, restart_required, no_change, invalid_value, or unknown_key.

clear prometheus
  Clear the Prometheus statistics

REPORTING BUGS
==============

pgexporter is maintained on GitHub at https://github.com/pgexporter/pgexporter

COPYRIGHT
=========

pgexporter is licensed under the 3-clause BSD License.

SEE ALSO
========

pgexporter.conf(5), pgexporter(1), pgexporter-admin(1)
