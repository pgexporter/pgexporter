# Configuration Generator

`pgexporter-config` is a utility tool that helps users generate and manage their `pgexporter` configuration files. It is particularly useful for new users to set up a base configuration or for automation scripts to modify existing settings safely.

## Overview

The tool provides several commands to interact with the configuration:

*   **init**: Interactive or automatic configuration generation.
*   **get**: Retrieve a value from the configuration.
*   **set**: Modify or add a value to the configuration.
*   **del**: Remove a key or a section.
*   **ls**: List sections or keys in a section.

## Usage

### Initializing a Configuration

The most common use case is generating a fresh configuration:

Command:
```
pgexporter-config init
```

This will guide you through the process of defining the listener address, metrics port, logging, and your first PostgreSQL server.

For automated setups, you can use the quiet mode:

Example:
```
pgexporter-config -q -o pgexporter.conf init
```

### Modifying the Configuration

Instead of manually editing the `ini` file and potentially making syntax errors, you can use the `set` command:

Example:
```
pgexporter-config set pgexporter.conf pgexporter log_level debug
```

This command ensures that:
1.  The file is updated atomically.
2.  Comments and formatting other than the modified line are preserved.
3.  Permissions are set to `0600`.

### Troubleshooting

If you need to verify what keys are available in a section:

Example:
```
pgexporter-config ls pgexporter.conf pgexporter
```

Or to check a specific value:

Example:
```
pgexporter-config get pgexporter.conf pgexporter metrics
```

## Security

`pgexporter-config` follows the same security principles as `pgexporter`:
*   It refuses to run as `root` to prevent misconfiguration of system files.
*   It maintains strict file permissions (`0600`) on all files it touches.
*   It uses `fsync()` to ensure data integrity during writes.
