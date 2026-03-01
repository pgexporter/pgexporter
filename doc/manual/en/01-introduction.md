\newpage

# Introduction

[**pgexporter**][pgexporter] is a [Prometheus][prometheus] exporter for [PostgreSQL][postgresql].

[**pgexporter**][pgexporter] will connect to one or more [PostgreSQL][postgresql] instances and let you monitor
their operation.

See [Metrics][metrics] for a list of currently available metrics.

## Features

* Prometheus exporter
* Remote management
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

## Platforms

The supported platforms are

* [Fedora][fedora] 39+
* [RHEL][rhel] 9 / RockyLinux 9
* [FreeBSD][freebsd]
* [OpenBSD][openbsd]

## Migration

### From 0.7.x to 0.8.0

#### Vault Encryption

The key derivation for vault file encryption has been upgraded to
`PKCS5_PBKDF2_HMAC` (SHA-256, random 16-byte salt, 600,000 iterations).

This is a **breaking change**. Existing vault files encrypted with the
old method cannot be decrypted by version 0.8.0.

**Action required:**

1. Stop pgexporter
2. Delete the existing master key:
   ```
   rm ~/.pgexporter/master.key
   ```
3. Regenerate the master key:
   ```
   pgexporter-admin master-key
   ```
4. Restart pgexporter
