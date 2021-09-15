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

## is-alive
Is pgexporter alive

Command

```
pgexporter-cli is-alive
```

Example

```
pgexporter-cli is-alive
```

## stop
Stop pgexporter

Command

```
pgexporter-cli stop
```

Example

```
pgexporter-cli stop
```

## status
Status of pgexporter

Command

```
pgexporter-cli status
```

Example

```
pgexporter-cli status
```

## details
Detailed status of pgexporter

Command

```
pgexporter-cli details
```

Example

```
pgexporter-cli details
```

## reload
Reload the configuration

Command

```
pgexporter-cli reload
```

Example

```
pgexporter-cli reload
```

## reset
Reset the Prometheus statistics
Command

```
pgexporter-cli reset
```

Example

```
pgexporter-cli reset
```
