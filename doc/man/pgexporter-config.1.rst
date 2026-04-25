=================
pgexporter-config
=================

--------------------------------------
Configuration utility for pgexporter
--------------------------------------

:Manual section: 1

SYNOPSIS
========

pgexporter-config [ -o OUTPUT_FILE ] [ -q ] [ -F ] [ -V ] [ -? ] [ COMMAND ]

DESCRIPTION
===========

pgexporter-config is a command-line utility used to generate and manage the configuration file for pgexporter.

OPTIONS
=======

-o, --output FILE
  Set the output file path. Default is ./pgexporter.conf.

-q, --quiet
  Quiet mode. Generate default configuration without interactive prompts (for init).

-F, --force
  Force overwrite if the output file already exists.

-V, --version
  Display version information.

-?, --help
  Display help.

COMMANDS
========

init
  Generate a new configuration file. By default, it runs interactively, asking for basic setup information.

get <file> <section> <key>
  Retrieve a configuration value from the specified file.

set <file> <section> <key> <value>
  Set or update a configuration value in the specified file. The file is updated atomically.

del <file> <section> [key]
  Delete a key or an entire section from the specified file.

ls <file> [section]
  List all sections in the file, or list all keys in a specific section.

EXAMPLES
========

Generate a new configuration interactively:

  $ pgexporter-config init

Generate a default configuration without prompts:

  $ pgexporter-config -q -o my_pgexporter.conf init

Update the metrics port:

  $ pgexporter-config set pgexporter.conf pgexporter metrics 5003

List all sections:

  $ pgexporter-config ls pgexporter.conf

REPORTING BUGS
==============

pgexporter is under active development. Please report any bugs at
https://github.com/pgexporter/pgexporter/issues

COPYRIGHT
=========

pgexporter is licensed under the 3-clause BSD License.

SEE ALSO
========

pgexporter(1), pgexporter-cli(1), pgexporter-admin(1), pgexporter.conf(5)
