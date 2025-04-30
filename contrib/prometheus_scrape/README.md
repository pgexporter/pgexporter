# Prometheus Metrics Documentation Generator

This script automatically generates documentation for Prometheus metrics exposed on `localhost`. It fetches metrics from a specified port and combines them with additional information from an extra file to produce both Markdown (`prometheus.md`) and HTML (`prometheus.html`) documentation files.

## Requirements

*   **Bash:** The script is written in Bash.
*   **Standard GNU/Linux Utilities:** Requires common tools like `curl`, `awk`, `sed`, `grep`, `sort`, `printf`, `cat`, `rm`, `cut`, `tr`, `paste`. These are typically available on most Linux distributions.
*   **Running Service:** The service exposing Prometheus metrics must be running and accessible on the specified port on `localhost`.

## Usage

Execute the script from your terminal, providing the port number and the path to the extra information file as arguments:

```bash
./prometheus.sh <port> <extra_info_file>
```

**Arguments:**

*   `<port>`: The TCP port number on `localhost` where the Prometheus metrics endpoint (`/metrics`) is available.
*   `<extra_info_file>`: The path to a text file containing supplementary descriptions and details for the metrics.

## `extra_info_file` Format

This file provides additional context that isn't typically available in the raw Prometheus metrics output. The script expects this file to follow a specific format:

*   Each metric block **must start** with the full metric name on its own line (e.g., `pgexporter_pg_stat_database_xact_commit`). This name must exactly match the metric name in the Prometheus output.
*   Lines immediately following the metric name that start with `+ ` (plus sign followed by a space) are treated as the **Description**. Multiple `+ ` lines are concatenated (with spaces) into a single description paragraph in the output.
*   Lines starting with `* ` (asterisk followed by a space) are treated as **Attribute Details**. These lines *must* follow the format `* Key: Value`.
    *   The text between the `* ` and the first colon (`:`) becomes the "Attribute" column in the output table.
    *   The text after the first colon becomes the "Value" column.
    *   Leading/trailing whitespace around the Key and Value is trimmed.
    *   If no `* Key: Value` lines are found for a metric, the "Attributes" section will be omitted from the output.
*   Blocks for different metrics are implicitly separated by the next line starting with a metric name.

**Note:** Lines starting with `Version:` are ignored by the current version of the script's output generation, although the internal processing might still parse them.

**Example `extra.info` content:**

```
pgexporter_state
+ Provides the operational status of the pgexporter service itself, indicating if it's running (1) or stopped/failed (0).
* 1: Running
* 0: Stopped or encountered a fatal error during startup/runtime

pgexporter_pg_locks_count
+ Monitors the number of locks of different types on each database.
* server: The configured name/identifier for the PostgreSQL server.
* database: The database being monitored.
* mode: The lock mode (e.g., accesssharelock, rowexclusivelock).
* Note: High values may indicate contention.

pgexporter_logging_info
+ Counts the total number of informational (INFO level) log messages produced by pgexporter since its last startup.
```

## Output

The script generates two files in the **current directory** (the directory where you run the script):

1.  **`prometheus.md`**: A Markdown file containing the documentation.
2.  **`prometheus.html`**: An HTML file with the same documentation, including a table of contents with basic styling.

Each metric entry in the output files follows this structure:

*   Metric Name (as heading)
*   Combined Prometheus Help text and Type (e.g., `pg_stat_database_blks_read Type is counter.`)
*   Description (from the `+` lines in `extra.info`)
*   Attributes Table (generated *only* if `* Key: Value` lines were present in `extra.info`)
*   Example (a sample raw metric line, preferably from the `postgres` database if available, otherwise the first one found)

## Example Usage

To generate documentation from metrics exposed on port `5002` using the file `extra.info` located in the current directory:

```bash
./prometheus.sh 5002 extra.info
```

This will create `prometheus.md` and `prometheus.html` in the current directory.

**Note**: By default the metrics are extracted from `postgres` for generating the example section. The `extra.info` files comes packaged with the core metrics that **pgexporter** supports but the document generation may skip metrics if they are not exposed by prometheus. This may occur due to lower Postgres versions or due to missing extensions which are not pulled by prometheus.