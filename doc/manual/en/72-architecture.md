\newpage

## Architecture

### Overview

[**pgexporter**][pgexporter] use a process model (`fork()`), where each process handles one Prometheus request to
[PostgreSQL][postgresql].

The main process is defined in [main.c][main_c].

### Shared memory

A memory segment ([shmem.h][shmem_h]) is shared among all processes which contains the [**pgexporter**][pgexporter]
state containing the configuration and the list of servers.

The configuration of [**pgexporter**][pgexporter] (`configuration_t`) and the configuration of the servers (`server_t`)
is initialized in this shared memory segment. These structs are all defined in [pgexporter.h][pgexporter_h].

The shared memory segment is created using the `mmap()` call.

### Network and messages

All communication is abstracted using the `message_t` data type defined in [messge.h][message_h].

Reading and writing messages are handled in the [message.h][message_h] ([message.c][message_c])
files.

Network operations are defined in [network.h][network_h] ([network.c][network_c]).

### Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the process.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h][memory_h] ([memory.c][memory_c]).

### Management

[**pgexporter**][pgexporter]  has a management interface which defines the administrator abilities that can be performed when it is running.
This include for example taking a backup. The `pgexporter-cli` program is used for these operations ([cli.c](../src/cli.c)).

The management interface is defined in [management.h](../src/include/management.h). The management interface
uses its own protocol which uses JSON as its foundation.

#### Write

The client sends a single JSON string to the server,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

#### Read

The server sends a single JSON string to the client,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

The client sends to the server a single JSON documents,

| Field         | Type   | Description                     |
| :------------ | :----- | :------------------------------ |
| `compression` | uint8  | The compression type            |
| `encryption`  | uint8  | The encryption type             |
| `length`      | uint32 | The length of the JSON document |
| `json`        | String | The JSON document               |

#### Remote management

The remote management functionality uses the same protocol as the standard management method.

However, before the management packet is sent the client has to authenticate using SCRAM-SHA-256 using the
same message format that PostgreSQL uses, e.g. StartupMessage, AuthenticationSASL, AuthenticationSASLContinue,
AuthenticationSASLFinal and AuthenticationOk. The SSLRequest message is supported.

The remote management interface is defined in [remote.h][remote_h] ([remote.c][remote_c]).

### libev

[libev][libev] is used to handle network interactions, which is "activated"
upon an `EV_READ` event.

Each process has its own event loop, such that the process only gets notified when data related only to that process
is ready. The main loop handles the system wide "services" such as idle timeout checks and so on.

### Signals

The main process of [**pgexporter**][pgexporter] supports the following signals `SIGTERM`, `SIGINT` and `SIGALRM`
as a mechanism for shutting down. The `SIGABRT` is used to request a core dump (`abort()`).
The `SIGHUP` signal will trigger a reload of the configuration.

It should not be needed to use `SIGKILL` for [**pgexporter**][pgexporter]. Please, consider using `SIGABRT` instead, and share the
core dump and debug logs with the [**pgexporter**][pgexporter] community.

### Reload

The `SIGHUP` signal will trigger a reload of the configuration.

However, some configuration settings requires a full restart of [**pgexporter**][pgexporter] in order to take effect. These are

* `hugepage`
* `libev`
* `log_path`
* `log_type`
* `unix_socket_dir`
* `pidfile`

The configuration can also be reloaded using `pgexporter-cli -c pgexporter.conf conf reload`. The command is only supported
over the local interface, and hence doesn't work remotely.

### Prometheus

pgexporter has support for [Prometheus][[prometheus] when the `metrics` port is specified.

    The module serves two endpoints

* `/` - Overview of the functionality (`text/html`)
* `/metrics` - The metrics (`text/plain`)

All other URLs will result in a 403 response.

The metrics endpoint supports `Transfer-Encoding: chunked` to account for a large amount of data.

The implementation is done in [prometheus.h][prometheus_h] and
[prometheus.c][prometheus_c].

### Logging

Simple logging implementation based on a `atomic_schar` lock.

The implementation is done in [logging.h][logging_h] and
[logging.c][logging_c].

| Level | Description |
| :------- | :------ |
| TRACE | Information for developers including values of variables |
| DEBUG | Higher level information for developers - typically about flow control and the value of key variables |
| INFO | A user command was successful or general health information about the system |
| WARN | A user command didn't complete correctly so attention is needed |
| ERROR | Something unexpected happened - try to give information to help identify the problem |
| FATAL | We can't recover - display as much information as we can about the problem and `exit(1)` |


### Protocol

The protocol interactions can be debugged using [Wireshark][wireshark] or
[pgprtdbg][pgprtdbg].
