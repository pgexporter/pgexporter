================
pgexporter-admin
================

-------------------------------------
Administration utility for pgexporter
-------------------------------------

:Manual section: 1

SYNOPSIS
========

pgexporter-admin [ -f FILE ] [ COMMAND ]

DESCRIPTION
===========

pgexporter-admin is an administration utility for pgexporter.

OPTIONS
=======

-f, --file FILE
  Set the path to a user file

-U, --user USER
  Set the user name

-P, --password PASSWORD
  Set the password for the user

-g, --generate
  Generate a password

-l, --length
  Password length

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

master-key
  Create or update the master key. The master key will be created in the pgexporter user home directory under ~/.pgexporter

user add
  Add a user

user edit
  Edit a user

user user del
  Delete a user

user ls
  List all users

REPORTING BUGS
==============

pgexporter is maintained on GitHub at https://github.com/pgexporter/pgexporter

COPYRIGHT
=========

pgexporter is licensed under the 3-clause BSD License.

SEE ALSO
========

pgexporter.conf(5), pgexporter(1), pgexporter-cli(1)
