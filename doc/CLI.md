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
pgexporter-cli conf set log_level debug
```

### conf get

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

### conf set

The `conf set` command modifies a runtime configuration parameter.

**Syntax:**
```
pgexporter-cli conf set <config_key> <config_value>
```

**Output:**

When executed, the `conf set` command returns a detailed result describing the outcome.

**Case 1: Success**
```
Configuration change applied
   New value: debug
```

**Case 2: Restart required**
Structural parameters (host, metrics, management, console, history, bridge,
unix_socket_dir, pidfile, ev_backend, hugepage, tls, log_type, bridge_json,
bridge_history, metrics_cache_max_size) require a full restart to take effect.

```
Configuration change requires restart. Current values preserved.
```

**Case 3: No change**
```
No change: log_level is already debug
```

**Case 4: Invalid value**
```
Configuration change failed
   Invalid value for configuration key 'log_level'
```

**Case 5: Unknown key**
```
Configuration change failed
   Unknown configuration key 'unknown_key'
```

### conf reload

Reload the configuration from disk.

```
Configuration reloaded successfully.
```

If structural parameters have been changed in the configuration file, reload
will report that a restart is required:

```
Configuration reload deferred: restart required for structural parameter changes.
The configuration file has been validated but changes have NOT been applied.
To apply: restart pgexporter.
```

## clear prometheus
Clear the Prometheus statistics

Command

```
pgexporter-cli clear prometheus
```
