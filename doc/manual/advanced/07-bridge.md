\newpage

# Bridge

pgexporter contains a bridge that can aggregate multiple Prometheus endpoints
into a single endpoint.

pgexporter can run in bridge-only mode where it only aggregates endpoints.

## Configuration

In order to enable the bridge add the following to `pgexporter.conf`

```ini
[pgexporter]

bridge = 5003
bridge_endpoints = localhost:5001, localhost:5002
```

The `bridge_endpoints` setting is a comma-separated list of endpoints that should
be aggregated.

## Access

The bridge acts a standard Prometheus endpoint, so you can access the bridge by

```sh
curl http://localhost:5003/metrics
```

## Cache

The bridge has a cache enabled by default.

The cache can be configured using the following settings

* `bridge_cache_max_age`
* `bridge_cache_max_size`

The cache can be disabled by setting `bridge_cache_max_size` to 0. By disabling the cache
each endpoint is scrapped upon each bridge invocation.
