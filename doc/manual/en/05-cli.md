\newpage

# Command line interface

``` sh
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

## ping
Is pgexporter alive

Command

```
pgexporter-cli ping
```

## shutdown
Shutdown pgexporter

Command

```
pgexporter-cli shutdown
```

## status
Status of pgexporter

Command

```
pgexporter-cli status
```

## status details
Detailed status of pgexporter

Command

```
pgexporter-cli status details
```

## conf

Manage the configuration

Command

``` sh
pgexporter-cli conf [reload | ls | get | set]
```

Subcommand

- `reload`: Reload configuration
- `ls` : To print the configurations used
- `get <config_key>` : To obtain information about a runtime configuration value
- `set <config_key> <config_value>` : To modify the runtime configuration value

Example

``` sh
pgexporter-cli conf reload
pgexporter-cli conf ls
pgexporter-cli conf get primary.host
pgexporter-cli conf set encryption aes-256-cbc
```

**conf get**

Get the value of a runtime configuration key, or the entire configuration.

- If you provide a `<config_key>`, you get the value for that key.
  - For main section keys, you can use either just the key (e.g., `host`) or with the section (e.g., `pgexporter.host`).
  - For server section keys, use the server name as the section (e.g., `server.primary.host`, `server.myserver.port`).
- If you run `pgexporter-cli conf get` without any key, the complete configuration will be output.

Examples

```sh
pgexporter-cli conf get
pgexporter-cli conf get host
pgexporter-cli conf get pgexporter.host
pgexporter-cli conf get server.primary.host
pgexporter-cli conf get server.myserver.port
```

## clear prometheus
Clear the Prometheus statistics

Command

```
pgexporter-cli clear prometheus
```

## Shell completions

There is a minimal shell completion support for `pgexporter-cli`.

Please refer to **Installation** for detailed information about how to enable and use shell completions.
