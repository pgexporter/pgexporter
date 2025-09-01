# Prometheus Metrics Documentation Generator

This Python script generates comprehensive documentation for pgexporter's Prometheus metrics. It fetches live metrics from your running pgexporter instance and enriches them with detailed descriptions from an extra info file.

## Requirements

*   **Python 3.6+** 
*   **requests library:** Install with `pip install requests`
*   **Running pgexporter:** Your pgexporter service must be running and exposing metrics

## Usage

```bash
./prometheus.py <port> <extra_info_file> [options]
```

**Arguments:**
*   `<port>`: Port where pgexporter is exposing metrics (usually 5002)
*   `<extra_info_file>`: File containing metric descriptions (typically `extra.info`)

**Options:**
*   `--manual`: Generate simple bullet-point format (like the user manual)
*   `--md`: Generate detailed markdown with examples and descriptions  
*   `--html`: Generate HTML documentation with styling
*   `--toc`: Include table of contents
*   Default (no options): Generates detailed markdown + HTML + TOC

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

## Examples

Generate full documentation with examples and descriptions:
```bash
./prometheus.py 5002 extra.info
```

Generate just markdown in simple bullet-point format:
```bash  
./prometheus.py 5002 extra.info --manual
```

Generate detailed markdown with table of contents:
```bash
./prometheus.py 5002 extra.info --md --toc
```

Generate both HTML and markdown:
```bash
./prometheus.py 5002 extra.info --md --html
```