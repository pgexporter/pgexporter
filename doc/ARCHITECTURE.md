# pgexporter architecture

## Overview

`pgexporter` use a process model (`fork()`), where each process handles one Prometheus request to
[PostgreSQL](https://www.postgresql.org).

The main process is defined in [main.c](../src/main.c).

## Shared memory

A memory segment ([shmem.h](../src/include/shmem.h)) is shared among all processes which contains the `pgexporter`
state containing the configuration and the list of servers.

The configuration of `pgexporter` (`configuration_t`) and the configuration of the servers (`server_t`)
is initialized in this shared memory segment. These structs are all defined in [pgexporter.h](../src/include/pgexporter.h).

The shared memory segment is created using the `mmap()` call.

## Network and messages

All communication is abstracted using the `message_t` data type defined in [messge.h](../src/include/message.h).

Reading and writing messages are handled in the [message.h](../src/include/message.h) ([message.c](../src/libpgexporter/message.c))
files.

Network operations are defined in [network.h](../src/include/network.h) ([network.c](../src/libpgexporter/network.c)).

## Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the process.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h](../src/include/memory.h) ([memory.c](../src/libpgexporter/memory.c)).

## Management

`pgexporter` has a management interface which defines the administrator abilities that can be performed when it is running.
This include for example taking a backup. The `pgexporter-cli` program is used for these operations ([cli.c](../src/cli.c)).

The management interface use Unix Domain Socket for communication.

The management interface is defined in [management.h](../src/include/management.h). The management interface
uses its own protocol which always consist of a header

| Field      | Type | Description |
|------------|------|-------------|
| `id` | Byte | The identifier of the message type |

The rest of the message is depending on the message type.

### Remote management

The remote management functionality uses the same protocol as the standard management method.

However, before the management packet is sent the client has to authenticate using SCRAM-SHA-256 using the
same message format that PostgreSQL uses, e.g. StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal and AuthenticationOk. The SSLRequest message is supported.

The remote management interface is defined in [remote.h](../src/include/remote.h) ([remote.c](../src/libpgexporter/remote.c)).

## libev usage

[libev](http://software.schmorp.de/pkg/libev.html) is used to handle network interactions, which is "activated"
upon an `EV_READ` event.

Each process has its own event loop, such that the process only gets notified when data related only to that process
is ready. The main loop handles the system wide "services" such as idle timeout checks and so on.

## Signals

The main process of `pgexporter` supports the following signals `SIGTERM`, `SIGINT` and `SIGALRM`
as a mechanism for shutting down. The `SIGABRT` is used to request a core dump (`abort()`).
The `SIGHUP` signal will trigger a reload of the configuration.

It should not be needed to use `SIGKILL` for `pgexporter`. Please, consider using `SIGABRT` instead, and share the
core dump and debug logs with the `pgexporter` community.

## Reload

The `SIGHUP` signal will trigger a reload of the configuration.

However, some configuration settings requires a full restart of `pgexporter` in order to take effect. These are

* `hugepage`
* `libev`
* `log_path`
* `log_type`
* `unix_socket_dir`
* `pidfile`

The configuration can also be reloaded using `pgexporter-cli -c pgexporter.conf conf reload`. The command is only supported
over the local interface, and hence doesn't work remotely.

## Prometheus

pgexporter has support for [Prometheus](https://prometheus.io/) when the `metrics` port is specified.

The module serves two endpoints

* `/` - Overview of the functionality (`text/html`)
* `/metrics` - The metrics (`text/plain`)

All other URLs will result in a 403 response.

The metrics endpoint supports `Transfer-Encoding: chunked` to account for a large amount of data.

The implementation is done in [prometheus.h](../src/include/prometheus.h) and
[prometheus.c](../src/libpgexporter/prometheus.c).

## Logging

Simple logging implementation based on a `atomic_schar` lock.

The implementation is done in [logging.h](../src/include/logging.h) and
[logging.c](../src/libpgexporter/logging.c).

## Protocol

The protocol interactions can be debugged using [Wireshark](https://www.wireshark.org/) or
[pgprtdbg](https://github.com/jesperpedersen/pgprtdbg).
