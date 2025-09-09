\newpage

# Quick start

Make sure that [**pgexporter**][pgexporter] is installed and in your path by using `pgexporter -?`. You should see

``` console
pgexporter 0.8.0
  Prometheus exporter for PostgreSQL

Usage:
  pgexporter [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE                  Set the path to the pgexporter.conf file
  -u, --users USERS_FILE                    Set the path to the pgexporter_users.conf file
  -A, --admins ADMINS_FILE                  Set the path to the pgexporter_admins.conf file
  -D, --directory DIRECTORY                 Set the configuration directory
  -Y, --yaml METRICS_FILE_DIR               Set the path to YAML file/directory
  -d, --daemon                              Run as a daemon
  -C, --collectors NAME_1,NAME_2,...,NAME_N Enable only specific collectors
  -V, --version                             Display version information
  -?, --help                                Display help

pgexporter: https://pgexporter.github.io/
Report bugs: https://github.com/pgexporter/pgexporter/issues
```

If you encounter any issues following the above steps, you can refer to the **Installation** chapter to see how to install or compile pgexporter on your system.

## Configuration

Lets create a simple configuration file called `pgexporter.conf` with the content

``` ini
[pgexporter]
host = *
metrics = 5002

log_type = file
log_level = info
log_path = /tmp/pgexporter.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
user = pgexporter
```

In our main section called `[pgexporter]` we setup [**pgexporter**][pgexporter] to listen on all network addresses. We will enable Prometheus metrics on port 5002. Logging will be performed at `info` level and put in a file called `/tmp/pgexporter.log`. Last we specify the location of the `unix_socket_dir` used for management operations and the path for the PostgreSQL command line tools.

Next we create a section called `[primary]` which has the information about our [PostgreSQL][postgresql] instance. In this case it is running on `localhost` on port `5432` and we will use the `pgexporter` user account to connect.

The `pgexporter` user must have the `pg_monitor` role and have access to the `postgres` database,
so for example

```
CREATE ROLE pgexporter WITH NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION LOGIN PASSWORD 'secretpassword';
GRANT pg_monitor TO pgexporter;
```

and in `pg_hba.conf`

```
local  postgres  pgexporter                scram-sha-256
host   postgres  pgexporter  127.0.0.1/32  scram-sha-256
host   postgres  pgexporter  ::1/128       scram-sha-256
```

The authentication type should be based on `postgresql.conf`'s `password_encryption` value.

We will need a user vault for the `pgexporter` account, so the following commands will add a master key, and the `pgexporter` password. The master key should be longer than 8 characters.

``` sh
pgexporter-admin master-key
pgexporter-admin -f pgexporter_users.conf user add
```

For scripted use, the master key and user password can be provided using the `PGEXPORTER_PASSWORD` environment variable.

We are now ready to run [**pgexporter**][pgexporter].

See the **Configuration** charpter for all configuration options.

## Running

We will run [**pgexporter**][pgexporter] using the command

``` sh
pgexporter -c pgexporter.conf -u pgexporter_users.conf
```

If this doesn't give an error, then we are ready to do backups.

[**pgexporter**][pgexporter] is stopped by pressing Ctrl-c (`^C`) in the console where you started it, or by sending the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

[**pgexporter**][pgexporter] has a run-time administration tool called `pgexporter-cli`.

You can see the commands it supports by using `pgexporter-cli -?` which will give

``` console
pgexporter-cli 0.8.0
  Command line utility for pgexporter

Usage:
  pgexporter-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE                       Set the path to the pgexporter.conf file
  -h, --host HOST                                Set the host name
  -p, --port PORT                                Set the port number
  -U, --user USERNAME                            Set the user name
  -P, --password PASSWORD                        Set the password
  -L, --logfile FILE                             Set the log file
  -v, --verbose                                  Output text string of result
  -V, --version                                  Display version information
  -F, --format text|json|raw                     Set the output format
  -C, --compress none|gz|zstd|lz4|bz2            Compress the wire protocol
  -E, --encrypt none|aes|aes256|aes192|aes128    Encrypt the wire protocol
  -?, --help                                     Display help

Commands:
  ping                     Check if pgexporter is alive
  shutdown                 Shutdown pgexporter
  status [details]         Status of pgexporter, with optional details
  conf <action>            Manage the configuration, with one of subcommands:
                           - 'reload' to reload the configuration
                           - 'ls' to print the configurations used
                           - 'get' to obtain information about a runtime configuration value
                           - 'set' to modify a configuration value;
  clear <what>             Clear data, with:
                           - 'prometheus' to reset the Prometheus statistics

pgexporter: https://pgexporter.github.io/
Report bugs: https://github.com/pgexporter/pgexporter/issues
```

To shutdown pgexporter you would use

```
pgexporter-cli -c pgexporter.conf shutdown
```

Check the outcome of the operations by verifying the exit code, like

```
echo $?
```

or by using the `-v` flag.

If pgexporter has both Transport Layer Security (TLS) and `management` enabled then `pgexporter-cli` can
connect with TLS using the files `~/.pgexporter/pgexporter.key` (must be 0600 permission),
`~/.pgexporter/pgexporter.crt` and `~/.pgexporter/root.crt`.

## Administration

[**pgexporter**][pgexporter] has an administration tool called `pgexporter-admin`, which is used to control user registration with [**pgexporter**][pgexporter].

You can see the commands it supports by using `pgexporter-admin -?` which will give

``` console
pgexporter-admin 0.8.0
  Administration utility for pgexporter

Usage:
  pgexporter-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  user <subcommand>       Manage a specific user, where <subcommand> can be
                          - add  to add a new user
                          - del  to remove an existing user
                          - edit to change the password for an existing user
                          - ls   to list all available users

pgexporter: https://pgexporter.github.io/
Report bugs: https://github.com/pgexporter/pgexporter/issues
```

In order to set the master key for all users you can use

``` sh
pgexporter-admin -g master-key
```

The master key must be at least 8 characters.

## Next Steps

Next steps in improving pgexporter's configuration could be

* Read the manual
* Update `pgexporter.conf` with the required settings for your system
* Enable Transport Layer Security v1.2+ (TLS) for administrator access

See [Configuration][configuration] for more information on these subjects.
