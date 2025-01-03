## Install pgexporter

This tutorial will show you how to do a simple installation of pgexporter.

At the end of this tutorial you will have Prometheus metrics from a PostgreSQL cluster.

### Preface

This tutorial assumes that you have an installation of PostgreSQL 13+ and pgexporter.

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf install -y postgresql12 postgresql12-server pgexporter
```

### Initialize cluster

```
export PATH=/usr/pgsql-12/bin:$PATH
initdb /tmp/pgsql
```

(`postgres` user)

### Remove default access

Remove

```
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

from `/tmp/pgsql/pg_hba.conf`

(`postgres` user)

### Add access for users and a database

Add

```
host    postgres         pgexporter      127.0.0.1/32            md5
host    postgres         pgexporter      ::1/128                 md5
```

to `/tmp/pgsql/pg_hba.conf`

Remember to check the value of `password_encryption` in `/tmp/pgsql/postgresql.conf`
to setup the correct authentication type.

(`postgres` user)

### Start PostgreSQL

```
pg_ctl  -D /tmp/pgsql/ start
```

(`postgres` user)

### Add users and a database

```
createuser -P pgexporter
```

with `pgexporter` as the password.

(`postgres` user)

### Verify access

For the user (standard) (using `pgexporter`)

```
psql -h localhost -p 5432 -U pgexporter postgres
\q
```

(`postgres` user)

### Add pgexporter user

```
sudo su -
useradd -ms /bin/bash pgexporter
passwd pgexporter
exit
```

(`postgres` user)

### Create pgexporter configuration

Switch to the pgexporter user

```
sudo su -
su - pgexporter
```

Add the master key and create vault

```
pgexporter-admin master-key
pgexporter-admin -f pgexporter_users.conf -U pgexporter -P pgexporter user add
```

You have to choose a password for the master key - remember it !

Create the `pgexporter.conf` configuration

```
cat > pgexporter.conf
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

and press `Ctrl-D`

(`postgres` user)

### Start pgexporter

```
pgexporter -c pgexporter.conf -u pgexporter_users.conf
```

(`pgexporter` user)

### Metrics Collectors

|Name|Metrics Collector|
|---|---|
|general|General|
|db|pg_database|
|locks|pg_locks|
|replication|pg_replication_slots|
|stat_bgwriter|pg_stat_bgwriter|
|stat_db|pg_stat_database|
|stat_conflicts|pg_stat_database_conflicts|
|settings|pg_settings|
|extension|[pgexporter_ext](https://github.com/pgexporter/pgexporter_ext) metric collector|

The metrics exposed by the corresponding metrics collectors can be found [here](https://pgexporter.github.io/metrics.html)

Some metrics collectors can be enabled using:
```sh
pgexporter -c pgexporter.conf -u pgexporter_users.conf -C name_1,name_2,...,name_n

# or

pgexporter -c pgexporter.conf -u pgexporter_users.conf --collectors name_1,name_2,...,name_n
```
where `name_1`, `name_2` and `name_n` can be one of the `names` of metric collectors.

eg.
```sh
pgexporter -c pgexporter.conf -u pgexporter_users.conf -C db,locks,replication
```

By default all metrics are enabled. But, if `-C` or `--collectors` is specified, then all of the default metrics collectors except `general` are turned off and the user has to specify which they want to enable.

### View metrics

In another terminal

```
curl http://localhost:5002/metrics
```

(`pgexporter` user)

### Shell completion

There is a minimal shell completion support for `pgexporter-cli` and `pgexporter-admin`. If you are running such commands from a Bash or Zsh, you can take some advantage of command completion.


#### Installing command completions in Bash

There is a completion script into `contrib/shell_comp/pgexporter_comp.bash` that can be used
to help you complete the command line while you are typing.

It is required to source the script into your current shell, for instance
by doing:

``` shell
source contrib/shell_comp/pgexporter_comp.bash
```

At this point, the completions should be active, so you can type the name of one the commands between `pgexporter-cli` and `pgexporter-admin` and hit `<TAB>` to help the command line completion.

#### Installing the command completions on Zsh

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
