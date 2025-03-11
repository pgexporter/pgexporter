# pgexporter-cli user guide

```
pgexporter-cli [ -c CONFIG_FILE ] [ COMMAND ]

-c, --config CONFIG_FILE Set the path to the pgexporter.conf file
-h, --host HOST          Set the host name
-p, --port PORT          Set the port number
-U, --user USERNAME      Set the user name
-P, --password PASSWORD  Set the password
-L, --logfile FILE       Set the log file
-v, --verbose            Output text string of result
-V, --version            Display version information
-?, --help               Display help
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

## clear prometheus
Clear the Prometheus statistics

Command

```
pgexporter-cli clear prometheus
```
