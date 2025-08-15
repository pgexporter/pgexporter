\newpage

# Installation

## Rocky Linux 9.x

We can download the [Rocky Linux](https://www.rockylinux.org/) distruction from their web site

```
https://rockylinux.org/download
```

The installation and setup is beyond the scope of this guide.

Ideally, you would use dedicated user accounts to run [**PostgreSQL**][postgresql] and [**pgexporter**][pgexporter]

```
useradd postgres
usermod -a -G wheel postgres
useradd pgexporter
usermod -a -G wheel pgexporter
```

Add a configuration directory for [**pgexporter**][pgexporter]

```
mkdir /etc/pgexporter
chown -R pgexporter:pgexporter /etc/pgexporter
```

and lets open the ports in the firewall that we will need

```
firewall-cmd --permanent --zone=public --add-port=5002/tcp
```

## PostgreSQL 17

We will install PostgreSQL 17 from the official [YUM repository][yum] with the community binaries,

**x86_64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**aarch64**

```
dnf -qy module disable postgresql
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y postgresql17 postgresql17-server postgresql17-contrib
```

First, we will update `~/.bashrc` with

```
cat >> ~/.bashrc
export PGHOST=/tmp
export PATH=/usr/pgsql-17/bin/:$PATH
```

then Ctrl-d to save, and

```
source ~/.bashrc
```

to reload the Bash environment.

Then we can do the PostgreSQL initialization

```
mkdir DB
initdb -k DB
```

and update configuration - for a 8 GB memory machine.

**postgresql.conf**
```
listen_addresses = '*'
port = 5432
max_connections = 100
unix_socket_directories = '/tmp'
password_encryption = scram-sha-256
shared_buffers = 2GB
huge_pages = try
max_prepared_transactions = 100
work_mem = 16MB
dynamic_shared_memory_type = posix
wal_level = replica
wal_log_hints = on
max_wal_size = 16GB
min_wal_size = 2GB
log_destination = 'stderr'
logging_collector = on
log_directory = 'log'
log_filename = 'postgresql.log'
log_rotation_age = 0
log_rotation_size = 0
log_truncate_on_rotation = on
log_line_prefix = '%p [%m] [%x] '
log_timezone = UTC
datestyle = 'iso, mdy'
timezone = UTC
lc_messages = 'en_US.UTF-8'
lc_monetary = 'en_US.UTF-8'
lc_numeric = 'en_US.UTF-8'
lc_time = 'en_US.UTF-8'
shared_preload_libraries = 'pgexporter_ext'
```

**pg_hba.conf**
```
local   postgres      pgexporter                scram-sha-256
local   all           all                       trust
host    postgres      pgexporter  127.0.0.1/32  scram-sha-256
host    postgres      pgexporter  ::1/128       scram-sha-256
```

Please, check with other sources in order to create a setup for your local setup.

Now, we are ready to start PostgreSQL

```
pg_ctl -D DB -l /tmp/ start
```

Lets connect, add the pgexporter user

```
psql postgres
CREATE ROLE pgexporter WITH NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION LOGIN PASSWORD 'pgexporter';
GRANT pg_monitor TO pgexporter;
\q
```

## pgexporter

We will install [**pgexporter**][pgexporter] from the official [YUM repository][yum] as well,

```
dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y pgexporter pgexporter_ext
```

Now, we will add the [**pgexporter_ext**][pgexporter_ext] extension

```
psql postgres
CREATE EXTENSION 'pgexporter_ext';
\q
```

First, we will need to create a master security key for the [**pgexporter**][pgexporter] installation, by

```
pgexporter-admin -g master-key
```

By default, this will ask for a key interactively. Alternatively, a key can be provided using either the
`--password` command line argument, or the `PGEXPORTER_PASSWORD` environment variable. Note that passing the
key using the command line might not be secure.

Then we will create the configuration for [**pgexporter**][pgexporter],

```
cat > /etc/pgexporter/pgexporter.conf
[pgexporter]
host = *
metrics = 5002

log_type = file
log_level = info
log_path = /tmp/pgexporter.log

unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
user = pgexporter
```

and end with a Ctrl-d to save the file.

Then, we will create the user configuration,

```
pgexporter-admin -f /etc/pgexporter/pgexporter_users.conf -U pgexporter -P pgexporter user add
```

Lets create the base directory, and start [**pgexporter**][pgexporter] now, by

```
pgexporter -d
```

## Shell completions

### Bash

There is a completion script into `contrib/shell_comp/pgexporter_comp.bash` that can be used
to help you complete the command line while you are typing.

It is required to source the script into your current shell, for instance
by doing:

``` shell
source contrib/shell_comp/pgexporter_comp.bash
```

At this point, the completions should be active, so you can type the name of one the commands between `pgexporter-cli` and `pgexporter-admin` and hit `<TAB>` to help the command line completion.

### Zsh

In order to enable completion into `zsh` you first need to have `compinit` loaded;
ensure your `.zshrc` file contains the following lines:

``` shell
autoload -U compinit
compinit
```

and add the sourcing of the `contrib/shell_comp/pgexporter_comp.zsh` file into your `~/.zshrc`
also associating the `_pgexporter_cli` and `_pgexporter_admin` functions
to completion by means of `compdef`:

``` shell
source contrib/shell_comp/pgexporter_comp.zsh
compdef _pgexporter_cli    pgexporter-cli
compdef _pgexporter_admin  pgexporter-admin
```

If you want completions only for one command, e.g., `pgexporter-admin`, remove the `compdef` line that references the command you don't want to have automatic completion.
At this point, digit the name of a `pgexporter-cli` or `pgexporter-admin` command and hit `<TAB>` to trigger the completion system.
