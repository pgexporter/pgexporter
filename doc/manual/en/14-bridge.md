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

## Bridge/JSON

The bridge has an optional component that will server a JSON presentation of the bridge data.

The bridge/json requires the bridge to be enabled, and invoked to serve data.

### Configuration

In order to enable the bridge add the following to `pgexporter.conf`

```ini
[pgexporter]

bridge_json = 5004
```

### Access

The bridge/json component acts a JSON endpoint and you can access the bridge by

```sh
curl http://localhost:5004/metrics
```

### Cache

The bridge/json has a cache enabled by default, and is mandatory.

The cache can be configured using the following settings

* `bridge_json_cache_max_size`

If the JSON representation can't fit in the cache an empty JSON object is returned.
