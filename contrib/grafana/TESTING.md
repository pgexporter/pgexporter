# Testing pgexporter Grafana Dashboards

A comprehensive guide for testing the Grafana 12+ dashboards for pgexporter.

---

## Prerequisites

Before testing, ensure you have:

1. **Docker and Docker Compose** installed
2. **pgexporter running** with metrics enabled on port 5002
3. **PostgreSQL running** with pgexporter connected
4. **Web browser** (Chrome, Firefox, or Edge)

> **Note**: If pgexporter is not yet running, see the main pgexporter documentation for setup instructions.

---

## Quick Start

### Step 1: Start pgexporter

Ensure pgexporter is running with metrics enabled:

```bash
# Check pgexporter is serving metrics
curl http://localhost:5002/metrics

# You should see output like:
# # HELP pgexporter_postgresql_primary Is the instance a primary
# # TYPE pgexporter_postgresql_primary gauge
# pgexporter_postgresql_primary 1
```

### Step 2: Start the Monitoring Stack

```bash
cd contrib/grafana
docker compose up -d
```

### Step 3: Verify Services Are Running

```bash
docker compose ps

# Expected output:
# NAME                     STATUS
# pgexporter-grafana       Up
# pgexporter-prometheus    Up
```

### Step 4: Access the Dashboards

| Service | URL | Credentials |
|---------|-----|-------------|
| **Grafana** | http://localhost:3000 | admin / admin |
| **Prometheus** | http://localhost:9090 | None required |

---

## Testing Checklist

Use this checklist to verify all dashboard features work correctly.

###  Dashboard Provisioning

- [ ] **Folder exists**: "pgexporter" folder appears in Grafana sidebar
- [ ] **All dashboards present**: 6 dashboards (PG13-PG18) appear in the folder
- [ ] **No import errors**: Dashboards load without "JSON parsing error" messages
- [ ] **Home dashboard**: PG14 dashboard loads as the default home

### Template Variables (Dropdowns)

- [ ] **Server dropdown**: "Server" filter appears in dashboard header
  - Shows available PostgreSQL servers
  - "All" option works correctly
  - Filtering updates all panels
- [ ] **Database dropdown** (PG14+): "Database" filter appears
  - Shows available databases
  - Multi-select works (hold Ctrl/Cmd)

### Dashboard Navigation

- [ ] **Navigation link**: "pgexporter Dashboards" dropdown appears in header
- [ ] **Links work**: Clicking a dashboard link loads that dashboard
- [ ] **Variables preserved**: Time range and server selection persist when switching

### Panel Functionality

Test each row of panels:

**System Health Row:**
- [ ] Instance Status: Shows "Primary" (green) or "Replica" (red)
- [ ] Connections: Graph shows connection count over time
- [ ] Database Sizes: Shows size in appropriate units (KB, MB, GB)

**Performance Row:**
- [ ] Cache Hit Ratio: Gauge shows percentage with color thresholds
  - Green: ≥95%
  - Yellow: 90-95%
  - Red: <90%
- [ ] Transactions: Shows commits/rollbacks per second
- [ ] Tuple Operations: Shows SELECT/INSERT/UPDATE/DELETE rates

**Query Analysis Row (requires pg_stat_statements):**
- [ ] Most Executed Queries: Bar chart or table shows top queries
- [ ] Slowest Queries: Shows queries with longest execution time
  - Color thresholds visible (yellow >100ms, red >1s)
- [ ] Highest WAL: Shows queries generating most WAL

**Memory Row (PG14+ only):**
- [ ] Memory Usage by Context: Shows memory allocation breakdown
- [ ] Displays "Used" and "Free" columns

### Data Validation

- [ ] **Metrics present**: Panels show actual data, not "No data"
- [ ] **Units correct**: Bytes display as "MiB", time displays as "ms" or "s"
- [ ] **Refresh works**: Press Ctrl+R (or Cmd+R on macOS) to refresh; data updates
- [ ] **Time range**: Changing the time range (top right) updates all panels

### Error Scenarios

- [ ] **pgexporter stopped**: Panels show "No data" gracefully, no errors
- [ ] **Invalid server filter**: Selecting non-existent server shows empty panels

---

## Troubleshooting During Testing

### Problem: Dashboards not appearing in folder

**Check provisioning config:**
```bash
docker compose exec grafana cat /etc/grafana/provisioning/dashboards/dashboards.yml
```

**Check Grafana logs:**
```bash
docker compose logs grafana | grep -i error
```

### Problem: "No data" in all panels

**Verify Prometheus is scraping:**
```bash
# Check Prometheus targets
curl -s http://localhost:9090/api/v1/targets | jq '.data.activeTargets[] | {job: .labels.job, health: .health}'
```

Expected output:
```json
{"job": "pgexporter", "health": "up"}
```

**Verify metrics exist in Prometheus:**
```bash
# Query a specific metric
curl -s 'http://localhost:9090/api/v1/query?query=pgexporter_postgresql_primary' | jq '.data.result'
```

### Problem: Query panels are empty

The `pg_stat_statements` extension must be enabled:

```sql
-- Connect to PostgreSQL and run:
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- Verify it's working:
SELECT * FROM pg_stat_statements LIMIT 5;
```

### Problem: Memory panels are empty (PG14+)

Memory context metrics require the `pg_backend_memory_contexts` view:

```sql
-- Verify access:
SELECT * FROM pg_backend_memory_contexts LIMIT 5;
```

### Problem: Scrape timeout errors

If you see "context deadline exceeded" in Prometheus targets:

1. Edit `prometheus.yml`:
   ```yaml
   scrape_timeout: 30s  # Increase from 15s
   ```

2. Restart Prometheus:
   ```bash
   docker compose restart prometheus
   ```

---

## Cleanup

```bash
# Stop and remove containers
docker compose down

# Remove volumes too (if you want fresh start)
docker compose down -v
```

---

## Related Documentation

- [README.md](README.md) - Setup instructions and file explanations
- [pgexporter documentation](https://pgexporter.github.io/) - Main project docs
