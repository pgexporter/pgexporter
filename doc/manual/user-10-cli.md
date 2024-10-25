\newpage

# Command line interface

``` sh
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

## conf reload
Reload the configuration

Command

```
pgexporter-cli conf reload
```

## clear prometheus
Clear the Prometheus statistics

Command

```
pgexporter-cli clear prometheus
```

## Shell completions

There is a minimal shell completion support for `pgexporter-cli`.

Please refer to the [Install pgexporter][t_install] tutorial for detailed information about how to enable and use shell completions.
