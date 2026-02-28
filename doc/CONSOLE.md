# Web console

The pgexporter web console is a lightweight HTTP UI for monitoring PostgreSQL
metrics in real time. It displays metrics organized by category, with filtering
options and multiple view modes to help you drill down into the data you need.

## Enable the console

Add a console port to `pgexporter.conf`:

```ini
[pgexporter]
host = 127.0.0.1
metrics = 5002
console = 5003
```

The console requires the metrics endpoint to be enabled. Start pgexporter:

```sh
pgexporter -c /etc/pgexporter/pgexporter.conf
```

## Open the console

Navigate to your console endpoint:

```
http://localhost:5003/
```

## Console flow & pages

### 1. Home page (overview)

When you first load the console, you see the **home page** with:

- **Service header** showing pgexporter service status (Running or Unavailable)
- **Version** of pgexporter
- **Category selector** dropdown to choose which metric group to view
- **View selector** (Simple or Detailed mode)
- **Server filter** dropdown to choose which PostgreSQL servers to display
- **Metrics table** showing the selected category
- **Category heading** link (clickable) that leads to the detailed category page

### 2. Home page—simple view

The default simple view shows:
- **Metric name** (column 1)
- **Value** (column 2)
- Labels are hidden for a clean, summary view

![Web console home page in simple view](../images/console_home_simple.png)

### 3. Home page—detailed view

Toggle to detailed view to see:
- **Metric name** (column 1)
- **Type** (gauge, counter, histogram, etc.) — (column 2)
- **Value** (column 3)
- **Labels** (column 4) — e.g., `database=mydb, server=primary`

![Web console home page in detail view](../images/console_home_detail.png)

### 4. Category organization

Metrics are automatically organized into **categories** based on shared prefixes:
- `pg_stat_*` → one category
- `pg_connection_*` → another category
- etc.

Use the **Category selector** to switch between categories. The page displays all
metrics in the selected category.

### 5. Server filter

The **Server filter** dropdown:
- Shows all configured PostgreSQL servers
- Allows multi-select (check/uncheck each server)
- The metrics table updates to show rows only from selected servers

![Web console home page server filter](../images/console_home_server_filter.png)

### 6. Detailed category page

Click on the **category heading** (e.g., "pg_stat_statements") on the home page
to open the **detail page** for that category.

On the detail page you see:
- All metrics in that category organized by their **labels**
- Each unique label combination gets its own **column**
- For example: if metrics have labels `(database=mydb, user=alice)` and `(database=testdb, user=bob)`,
  you see separate columns for each

![Web console category detail page](../images/console_category_detail.png)

## API endpoints

- `/` — Main console (home page)
- `/api` — JSON endpoint with all metrics (useful for scripting)
- `/detail?cat=N` — Detailed view for category N

## Theme toggle

Click the theme button (moon/sun icon) in the top right to switch between:
- **Light mode** (default, white background)
- **Dark mode** (dark background)

Theme preference is saved in your browser's local storage.

## Service status values

The header shows:
- **Running** — pgexporter management service is reachable
- **Unavailable** — pgexporter management service is not reachable

## Troubleshooting

- **No metrics displayed?**
  - Ensure `metrics` port is enabled in `pgexporter.conf`
  - Verify the Prometheus endpoint is reachable on the configured host

- **Service shows "Unavailable"?**
  - Check that `unix_socket_dir` is writable
  - Confirm the directory path in `pgexporter.conf` is accessible to the pgexporter process

- **Unable to access the console?**
  - Verify the console port is not blocked by firewall
  - Check that the console host/port in `pgexporter.conf` matches your URL
