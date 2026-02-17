\newpage

# Grafana Dashboard

## Prometheus Configuration

### Installation
First of all, you should install Prometheus in your pgexporter server. Download one of the binaries from the [Prometheus Downloads](https://prometheus.io/download/), say `prometheus-3.5.0.linux-amd64.tar.gz` (may be different for you, but the following instructions are based off of this).

Unzip the tarball:
```sh
$ tar -xzvf prometheus-3.5.0.linux-amd64.tar.gz
```

(OPTIONAL) Putting the binaries in a location included in `PATH` so that it's convenient to use `$ prometheus` (choosing `/usr/local/bin` for this):
```sh
$ sudo mv prometheus-3.5.0.linux-amd64/prometheus /usr/local/bin
$ sudo mv prometheus-3.5.0.linux-amd64/promtool /usr/local/bin
```

### Configuration

After you successfully installed Prometheus, you should replace `prometheus.yml` with the content below to configure how to query your pgexporter metrics (assuming `pgexporter` runs on port `5002` on `localhost`):
```yml
scrape_configs:
  - job_name: 'pgexporter'
    metrics_path: '/metrics'
    static_configs:
      - targets: ['localhost:5002']
```

Run Prometheus using:
```sh
$ prometheus
```
(assuming there is a valid `./prometheus.yaml`).


Then the Prometheus service will query your pgexporter metrics every 15 seconds and package them as time-series data. You can query your pgexporter metrics and watch their changes as time passed in Prometheus web page (default port is `9090`).

![image](../../images/prometheus_metrics.png)

## Grafana Dashboard

Although Prometheus provides capacity of querying and monitoring metrics, we can not customize graphs for each metric and provide a unified view. As a result, we use Grafana to help us manage all graphs together.

### Quick Start (Docker Compose)

For a complete, pre-configured environment, we recommended using the Docker Compose setup provided in `contrib/grafana`. This automatically starts Grafana 12+ and Prometheus, and provisions all dashboards.

1.  Navigate to the directory:
    ```sh
    $ cd contrib/grafana
    ```

2.  Start the stack:
    ```sh
    $ docker compose up -d
    ```

3.  Access Grafana at `http://localhost:3000` (User: `admin`, Password: `admin`).

#### All dashboards are automatically provisioned, so no manual import is needed.

### Installation (Manual)

First of all, we should install Grafana in the computer you need to monitor pgexporter metrics. Installation instructions for your preferred operating system is provided in the official [Grafana Installation page](https://grafana.com/docs/grafana/latest/setup-grafana/installation/):

For example, on Fedora it is:
```sh
$ wget -q -O gpg.key https://rpm.grafana.com/gpg.key
$ sudo rpm --import gpg.key
```

Create and edit file `grafana.repo`:
```sh
$ sudo nano /etc/yum.repos.d/grafana.repo
```
Contents of the file:
```
[grafana]
name=grafana
baseurl=https://rpm.grafana.com
repo_gpgcheck=1
enabled=1
gpgcheck=1
gpgkey=https://rpm.grafana.com/gpg.key
sslverify=1
sslcacert=/etc/pki/tls/certs/ca-bundle.crt
```
Save and Exit.

```sh
$ sudo dnf install grafana
```

Start the daemon:
```sh
$ sudo systemctl start grafana-server
$ sudo systemctl status grafana-server
```
Verify the status is "active (running)".

#### Configuration
You can now browse Grafana web page with default port `3000`, default user `admin` and default password `admin`.

![image](../../images/grafana_login.png)

Set the new password.

![image](../../images/grafana_home.png)

Then you can create Prometheus data source of pgexporter. Click Menu -> Connections -> Data Sources -> "Add data source" -> Prometheus.

![image](../../images/grafana_menu.png)

![image](../../images/grafana_add_data_source.png)

![image](../../images/grafana_add_data_source_prometheus.png)

Then configure the data source and then "Save & test":

![image](../../images/grafana_add_prometheus.png)

Then you can explore dashboards with queries in "Expore" (Home -> Explore):

![image](../../images/grafana_add_dashboard.png)

Then "Run Query":

![image](../../images/grafana_graph.png)

### Importing pgexporter dashboard

You could also use pgexporter dashboards which are available in [contrib/grafana](../../contrib/grafana/) directory. Navigate to **Home -> Dashboards -> New -> Import**.

![image](../../images/grafana_import_dashboard.png)

We provide 6 version-specific dashboards to support the unique features of each PostgreSQL version:

*   `postgresql_dashboard_pg13.json`
*   `postgresql_dashboard_pg14.json` (+ Memory Contexts)
*   `postgresql_dashboard_pg15.json` (+ Memory Contexts)
*   `postgresql_dashboard_pg16.json` (+ pg_stat_io)
*   `postgresql_dashboard_pg17.json` (+ Wait Events)
*   `postgresql_dashboard_pg18.json` (+ Wait Events)

Select **"Upload dashboard as JSON file"**, choose the file matching your PostgreSQL version from the [contrib/grafana](../../contrib/grafana/) directory, select your Prometheus datasource, and click **"Import"**.

You will now be able to view important metrics which pgexporter provides.

![image](../../images/grafana_pgexporter_dashboard.png)

The dashboard organizes metrics into logical sections:

#### System Health
Instance Status (Primary/Replica), Database Connections, Database Sizes, Transaction Rates, Database Locks.

![image](../../images/grafana_pgexporter_dashboard_system_health.png)

#### Performance Indicators
Cache Hit Ratio, Disk Reads, Tuple Operations (Select/Insert/Update/Delete), Background Writer Activity, Deadlocks & Conflicts.

![image](../../images/grafana_pgexporter_dashboard_perf_indicator.png)

#### Query Analysis
Most Executed Queries, Slowest Queries, Highest WAL Usage Queries, Index Usage (requires `pg_stat_statements`).

![image](../../images/grafana_pgexporter_dashboard_queryanalysis.png)

#### Memory & I/O (Version Dependent)
*   **PG14+**: Memory Usage by Context (Used/Free/Total)
*   **PG16+**: Detailed I/O statistics (Reads/Writes/Extends per backend type)
*   **PG17+**: Wait Events analysis (Pie chart breakdown by type)

![image](../../images/grafana_pgexporter_dashboard_memory.png)


#### WAL & Replication
WAL Generation, Replication Status, WAL Archiving, Transaction ID Wraparound (Database Age).

![image](../../images/grafana_pgexporter_dashboard_wal_replication.png)

### Switching Datasources

If you have configured multiple Prometheus datasources (e.g., for different PostgreSQL servers), you can easily switch between them using the datasource dropdown at the top of the dashboard.

![image](../../images/grafana_datasource_select.png)

Select the desired datasource from the dropdown to view metrics from that specific server.
