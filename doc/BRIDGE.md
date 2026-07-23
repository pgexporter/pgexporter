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
be aggregated. Prefix an endpoint with `https://` to scrape it over TLS, e.g.
`bridge_endpoints = localhost:5001, https://localhost:5002`.

### Inbound TLS

The bridge (and bridge/JSON) listener can serve HTTPS the same way the
`/metrics` endpoint does. Add the following to `pgexporter.conf` to enable it:

```ini
[pgexporter]

bridge_cert_file = /path/to/server.crt
bridge_key_file = /path/to/server.key
bridge_ca_file = /path/to/ca.crt
```

`bridge_ca_file` is optional. If `bridge_cert_file`/`bridge_key_file` are not
set, or the referenced files can't be found, the bridge falls back to plain HTTP.

### Outbound TLS (scraping a `https://` endpoint)

If a `bridge_endpoints` entry (or the local `/metrics` endpoint the console
falls back to) is `https://`-prefixed, the scrape connects over TLS. To verify
the target's certificate, set:

```ini
[pgexporter]

scrape_ca_file = /path/to/ca.crt
```

`scrape_ca_file` is optional — without it, the scrape still succeeds over TLS,
just without verifying the target's certificate.

If the target itself enforces mutual TLS (i.e. it has its own CA file
configured, forcing client-certificate verification), also set a client
certificate/key for the scrape to present:

```ini
[pgexporter]

scrape_cert_file = /path/to/client.crt
scrape_key_file = /path/to/client.key
```

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
