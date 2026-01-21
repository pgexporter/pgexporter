# pgexporter Grafana Dashboards

Production-ready PostgreSQL monitoring dashboards for Grafana 12+, using `pgexporter` metrics via Prometheus.

## Quick Start

```bash
# 1. Ensure pgexporter is running with metrics enabled on port 5002
#    In your pgexporter.conf:
#    metrics = 5002

# 2. Start the monitoring stack
cd contrib/grafana
docker compose up -d

# 3. Access Grafana
#    URL: http://localhost:3000
#    Username: admin
#    Password: admin
```

Dashboards are automatically provisioned - no manual import needed.

---

## Features

| Feature | Description |
|---------|-------------|
| **Server Filter** | Dropdown to select specific PostgreSQL servers |
| **Database Filter** | Dropdown to filter metrics by database name |
| **Dashboard Links** | One-click navigation between version dashboards |
| **Panel Descriptions** | Hover over `(i)` icons for metric explanations |
| **Alert Thresholds** | Visual warnings (e.g., Cache Hit < 90% = red) |
| **Grafana 12+** | Uses `schemaVersion: 39` for full compatibility |

---

## Dashboards by PostgreSQL Version

| Dashboard File | PostgreSQL Version | Additional Features |
|----------------|-------------------|---------------------|
| `postgresql_dashboard_pg13.json` | 13.x | Core metrics only |
| `postgresql_dashboard_pg14.json` | 14.x | + Memory contexts |
| `postgresql_dashboard_pg15.json` | 15.x | + Memory contexts |
| `postgresql_dashboard_pg16.json` | 16.x | + pg_stat_io metrics |
| `postgresql_dashboard_pg17.json` | 17.x | + Wait events |
| `postgresql_dashboard_pg18.json` | 18.x | + Wait events |

### Panels Included in All Dashboards

- **System Health**: Primary/Replica status, connection count, database sizes
- **Performance**: Cache hit ratio (with thresholds), tuple operations, locks
- **Query Analysis**: Top executed queries, slowest queries, highest WAL usage
- **Replication**: WAL archiver status, replication slots
- **I/O Statistics**: Disk reads, buffer writes, checkpoints

---

## File Structure

```
contrib/grafana/
├── README.md                          # This file
├── TESTING.md                         # Testing checklist
├── docker-compose.yml                 # Grafana + Prometheus stack
├── prometheus.yml                     # Prometheus scrape configuration
├── postgresql_dashboard_pg*.json      # Dashboard definitions (6 files)
└── provisioning/
    ├── dashboards/
    │   └── dashboards.yml             # Dashboard auto-provisioning
    └── datasources/
        └── prometheus.yml             # Prometheus datasource config
```

### File Explanations

#### `docker-compose.yml`

Defines two services:

**Prometheus** (`prom/prometheus:latest`):
- Scrapes metrics from pgexporter
- Stores time-series data
- Accessible at http://localhost:9090

**Grafana** (`grafana/grafana:12.0.0`):
- Displays dashboards
- Auto-provisions datasource and dashboards
- Accessible at http://localhost:3000

Key configuration:
```yaml
extra_hosts:
  - "host.docker.internal:host-gateway"  # Allows container to reach host
```

#### `prometheus.yml`

Prometheus scrape configuration:

```yaml
scrape_configs:
  - job_name: 'pgexporter'
    scrape_timeout: 15s                        # Extended for large datasets
    static_configs:
      - targets: ['host.docker.internal:5002'] # pgexporter on host machine
```

**Important**: `host.docker.internal` resolves to your host machine, allowing Prometheus (running in Docker) to scrape pgexporter (running on the host).

#### `provisioning/datasources/prometheus.yml`

Configures the Prometheus datasource for Grafana:

```yaml
datasources:
  - name: Prometheus
    type: prometheus
    url: http://prometheus:9090    # Uses Docker network name
    uid: prometheus                # Referenced as ${DS_PROMETHEUS} in dashboards
    isDefault: true
```

#### `provisioning/dashboards/dashboards.yml`

Tells Grafana where to find dashboard JSON files:

```yaml
providers:
  - name: 'pgexporter'
    folder: 'pgexporter'           # Dashboards appear under this folder
    options:
      path: /var/lib/grafana/dashboards/pgexporter
```

---

## Setup Options

### Option 1: Docker Compose (Recommended)

```bash
# Start the stack
docker compose up -d

# Check status
docker compose ps

# View logs
docker compose logs -f

# Stop the stack
docker compose down
```

### Option 2: Manual Setup

1. **Configure pgexporter** (`pgexporter.conf`):
   ```ini
   metrics = 5002
   ```

2. **Configure Prometheus** (`prometheus.yml`):
   ```yaml
   scrape_configs:
     - job_name: 'pgexporter'
       scrape_timeout: 15s
       static_configs:
         - targets: ['localhost:5002']
   ```

3. **Import Dashboard**:
   - Open Grafana → Dashboards → Import
   - Upload the JSON file for your PostgreSQL version
   - Select your Prometheus datasource

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| **No data in panels** | pgexporter not running | Run `curl http://localhost:5002/metrics` to verify |
| **Datasource error** | Wrong datasource selected | Select "Prometheus" during import |
| **Prometheus "target down"** | Firewall blocking port 5002 | Ensure pgexporter is listening on 0.0.0.0:5002 |
| **Query metrics empty** | `pg_stat_statements` not enabled | Run `CREATE EXTENSION pg_stat_statements` |
| **Scrape timeout** | Too many databases | Increase `scrape_timeout` in prometheus.yml |
| **Filters not populated** | No metrics available | Wait 30 seconds for data collection |

### Verify Metrics Are Available

```bash
# Check pgexporter is serving metrics
curl -s http://localhost:5002/metrics | head -20

# Check Prometheus is scraping
curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets[].health'
```

---

## Contributing

When modifying dashboards:

1. **Use `${DS_PROMETHEUS}`** for all datasource references
2. **Maintain `schemaVersion: 39`** for Grafana 12 compatibility
3. **Add descriptions** to new panels explaining the metric
4. **Include units** (bytes, ops, ms, percent) in field config
5. **Test with Docker Compose** before committing:
   ```bash
   docker compose down && docker compose up -d
   ```

---

## Related Files

- **YAML Collectors**: `contrib/yaml/postgresql-*.yaml` - Define which PostgreSQL metrics to collect
- **Configuration**: See main pgexporter documentation for `pgexporter.conf` options
