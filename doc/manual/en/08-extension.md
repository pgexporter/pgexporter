\newpage

# Extension

[**pgexporter_ext**][pgexporter_ext] is a [PostgreSQL][postgresql] extension that provides additional
information about the environment.

## Features

[**pgexporter_ext**][pgexporter_ext] provides

* OS information
* CPU information
* Memory information
* Network information
* Load average metrics
* Disk space metrics

and is supported on

* Fedora
* RHEL 9 / RockyLinux 9

based systems.

## Installation

[**pgexporter_ext**][pgexporter_ext] can be installed through the YUM repository of [PostgreSQL][postgresql]
so,

```
dnf install -y pgexporter_ext_17
```

for example if your [PostgreSQL][postgresql] version is 17.

## Compiling the source

You can also compile the source code of [**pgexporter_ext**][pgexporter_ext] by

```
dnf install git gcc cmake make postgresql-devel
```

or

```
dnf install git gcc cmake make postgresql-server-devel
```

and then do

```
git clone https://github.com/pgexporter/pgexporter_ext.git
cd pgexporter_ext
mkdir build
cd build
cmake ..
make
sudo make install
```

## Configuration

First of all, make sure that [**pgexporter_ext**][pgexporter_ext] is installed by using

```
ls `pg_config --libdir`/pgexporter_ext*
```

You should see

```
/path/to/postgresql/lib/pgexporter_ext.so  /path/to/postgresql/lib/pgexporter_ext.so.0.2.4
```

Then, you have to change the `postgresql.conf` file to enable the extension with

```
shared_preload_libraries = 'pgexporter_ext'
```

and restart/reload the configuration.

Next, activate the extension in the `postgres` database,

```
psql postgres
CREATE EXTENSION pgexporter_ext;
```

The `pgexporter` user must have the `pg_monitor` role to access to the functions in the extension.

```
GRANT pg_monitor TO pgexporter;
```

[**pgexporter**][pgexporter] is now able to use the extended functionality of [**pgexporter_ext**][pgexporter_ext].
