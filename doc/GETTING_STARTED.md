# Getting started with pgexporter

First of all, make sure that `pgexporter` is installed and in your path by
using `pgexporter -?`. You should see

```
pgexporter 0.6.0
  Prometheus exporter for PostgreSQL

Usage:
  pgexporter [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE                      Set the path to the pgexporter.conf file
  -u, --users USERS_FILE                        Set the path to the pgexporter_users.conf file
  -A, --admins ADMINS_FILE                      Set the path to the pgexporter_admins.conf file
  -Y, --yaml METRICS_FILE_DIR                   Set the path to YAML file/directory
  -d, --daemon                                  Run as a daemon
  -C, --collectors NAME_1,NAME_2,...,NAME_N     Enable only specific collectors
  -V, --version                                 Display version information
  -?, --help                                    Display help

pgexporter: https://pgexporter.github.io/
Report bugs: https://github.com/pgexporter/pgexporter/issues
```

If you don't have `pgexporter` in your path see [README](../README.md) on how to
compile and install `pgexporter` in your system.

## Configuration

Lets create a simple configuration file called `pgexporter.conf` with the content

```
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

In our main section called `[pgexporter]` we setup `pgexporter` to listen on all
network addresses. We will enable Prometheus metrics on port 5002.
Logging will be performed at `info` level and put in a file called `/tmp/pgexporter.log`.
Last we specify the location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our
[PostgreSQL](https://www.postgresql.org) instance. In this case it is running
on `localhost` on port `5432` and we will use the `pgexporter` user account to connect.

The `pgexporter` user must have the `pg_monitor` role and have access to the `postgres` database,
so for example

```
CREATE ROLE pgexporter WITH NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION LOGIN PASSWORD 'secretpassword';
GRANT pg_monitor TO pgexporter;
```

and in `pg_hba.conf`

```
local   postgres        pgexporter                             scram-sha-256
host    postgres        pgexporter     127.0.0.1/32            scram-sha-256
host    postgres        pgexporter     ::1/128                 scram-sha-256
```

We will need a user vault for the `pgexporter` account, so the following commands will add
a master key, and the `pgexporter` password

```
pgexporter-admin master-key
pgexporter-admin -f pgexporter_users.conf user add
```

We are now ready to run `pgexporter`.

See [Configuration](./CONFIGURATION.md) for all configuration options.

## Running

We will run `pgexporter` using the command

```
pgexporter -c pgexporter.conf -u pgexporter_users.conf
```

If this doesn't give an error, then we are ready to use the Prometheus endpoint.

`pgexporter` is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending
the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

`pgexporter` has a run-time administration tool called `pgexporter-cli`.

You can see the commands it supports by using `pgexporter-cli -?` which will give

```
pgexporter-cli 0.6.0
  Command line utility for pgexporter

Usage:
  pgexporter-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE Set the path to the pgexporter.conf file
  -h, --host HOST          Set the host name
  -p, --port PORT          Set the port number
  -U, --user USERNAME      Set the user name
  -P, --password PASSWORD  Set the password
  -L, --logfile FILE       Set the log file
  -v, --verbose            Output text string of result
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  is-alive                 Is pgexporter alive
  stop                     Stop pgexporter
  status                   Status of pgexporter
  details                  Detailed status of pgexporter
  reload                   Reload the configuration
  reset                    Reset the Prometheus statistics
```

To stop pgexporter you would use

```
pgexporter-cli -c pgexporter.conf stop
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

`pgexporter` has an administration tool called `pgexporter-admin`, which is used to control user
registration with `pgexporter`.

You can see the commands it supports by using `pgexporter-admin -?` which will give

```
pgexporter-admin 0.6.0
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
```

In order to set the master key for all users you can use

```
pgexporter-admin -g master-key
```

The master key must be at least 8 characters.

Then use the other commands to add, update, remove or list the current user names, f.ex.

```
pgexporter-admin -f pgexporter_users.conf user add
```

## Next Steps

Next steps in improving pgexporter's configuration could be

* Update `pgexporter.conf` with the required settings for your system
* Enable Transport Layer Security v1.2+ (TLS) for administrator access

See [Configuration](./CONFIGURATION.md) for more information on these subjects.

## Tutorials

There are some short tutorials available to help you better understand and configure `pgexporter`:

- [Installing pgexporter](https://github.com/pgexporter/pgexporter/blob/main/doc/tutorial/01_install.md)
- [Custom metrics](https://github.com/pgexporter/pgexporter/blob/main/doc/tutorial/02_custom_metrics.md)
- [Grafana Dashboard](https://github.com/pgexporter/pgexporter/blob/main/doc/tutorial/03_grafana.md)
- [Using Transport Level Security](https://github.com/pgexporter/pgexporter/blob/main/doc/tutorial/04_tls.md)
- [Bridge](https://github.com/pgexporter/pgexporter/blob/main/doc/tutorial/05_bridge.md)

## Closing

The [pgexporter](https://github.com/pgexporter/pgexporter) community hopes that you find
the project interesting.

Feel free to

* [Ask a question](https://github.com/pgexporter/pgexporter/discussions)
* [Raise an issue](https://github.com/pgexporter/pgexporter/issues)
* [Submit a feature request](https://github.com/pgexporter/pgexporter/issues)
* [Write a code submission](https://github.com/pgexporter/pgexporter/pulls)

All contributions are most welcome !

Please, consult our [Code of Conduct](../CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgexporter/pgexporter/stargazers) on
[GitHub](https://github.com/pgexporter/pgexporter/) if you find it useful.
