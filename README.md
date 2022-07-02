# pgexporter

`pgexporter` is a [Prometheus](https://prometheus.io/) exporter for [PostgreSQL](https://www.postgresql.org).

`pgexporter` will connect to one or more [PostgreSQL](https://www.postgresql.org) instances and let you monitor
their operation.

See [Metrics](https://pgexporter.github.io/metrics.html) for a list of currently available metrics.

## Features

* Prometheus exporter
* Remote management
* Transport Layer Security (TLS) v1.2+ support
* Daemon mode
* User vault

See [Getting Started](./doc/GETTING_STARTED.md) on how to get started with `pgexporter`.

See [Configuration](./doc/CONFIGURATION.md) on how to configure `pgexporter`.

## Overview

`pgexporter` makes use of

* Process model
* Shared memory model across processes
* [libev](http://software.schmorp.de/pkg/libev.html) for fast network interactions
* [Atomic operations](https://en.cppreference.com/w/c/atomic) are used to keep track of state
* The [PostgreSQL](https://www.postgresql.org) native protocol
  [v3](https://www.postgresql.org/docs/11/protocol-message-formats.html) for its communication

See [Architecture](./doc/ARCHITECTURE.md) for the architecture of `pgexporter`.

## Tested platforms

* [Fedora](https://getfedora.org/) 32+
* [RHEL](https://www.redhat.com/en/technologies/linux-platforms/enterprise-linux) 8.x with
  [AppStream](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8/html/installing_managing_and_removing_user-space_components/using-appstream_using-appstream)

## Compiling the source

`pgexporter` requires

* [gcc 8+](https://gcc.gnu.org) (C17)
* [cmake](https://cmake.org)
* [make](https://www.gnu.org/software/make/)
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
* [rst2man](https://docutils.sourceforge.io/)
* [libyaml](https://pyyaml.org/wiki/LibYAML)

```sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel python3-docutils libyaml libyaml-devel
```

Alternative [clang 8+](https://clang.llvm.org/) can be used.

### Release build

The following commands will install `pgexporter` in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](./doc/RPM.md) for how to build a RPM of `pgexporter`.

### Debug build

The following commands will create a `DEBUG` version of `pgexporter`.

```sh
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

Remember to set the `log_level` configuration option to `debug5`.

## Contributing

Contributions to `pgexporter` are managed on [GitHub.com](https://github.com/pgexporter/pgexporter/)

* [Ask a question](https://github.com/pgexporter/pgexporter/discussions)
* [Raise an issue](https://github.com/pgexporter/pgexporter/issues)
* [Feature request](https://github.com/pgexporter/pgexporter/issues)
* [Code submission](https://github.com/pgexporter/pgexporter/pulls)

Contributions are most welcome !

Please, consult our [Code of Conduct](./CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/pgexporter/pgexporter/stargazers) on
[GitHub](https://github.com/pgexporter/pgexporter/) if you find it useful.

## License

[BSD-3-Clause](https://opensource.org/licenses/BSD-3-Clause)
