# Install pgexporter

This tutorial will show you how to do a simple installation of pgexporter.

At the end of this tutorial you will have Prometheus metrics from a PostgreSQL cluster.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgexporter.

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf install -y postgresql10 postgresql10-server pgexporter
```

## Initialize cluster

```
export PATH=/usr/pgsql-10/bin:$PATH
initdb /tmp/pgsql
```

(`postgres` user)

## Remove default access

Remove

```
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

from `/tmp/pgsql/pg_hba.conf`

(`postgres` user)

## Add access for users and a database

Add

```
host    postgres         pgexporter      127.0.0.1/32            md5
host    postgres         pgexporter      ::1/128                 md5
```

to `/tmp/pgsql/pg_hba.conf`

Remember to check the value of `password_encryption` in `/tmp/pgsql/postgresql.conf`
to setup the correct authentication type.

(`postgres` user)

## Start PostgreSQL

```
pg_ctl  -D /tmp/pgsql/ start
```

(`postgres` user)

## Add users and a database

```
createuser -P pgexporter
```

with `pgexporter` as the password.

(`postgres` user)

## Verify access

For the user (standard) (using `pgexporter`)

```
psql -h localhost -p 5432 -U pgexporter postgres
\q
```

(`postgres` user)

## Add pgexporter user

```
sudo su -
useradd -ms /bin/bash pgexporter
passwd pgexporter
exit
```

(`postgres` user)

## Create pgexporter configuration

Switch to the pgexporter user

```
sudo su -
su - pgexporter
```

Add the master key and create vault

```
pgexporter-admin master-key
pgexporter-admin -f pgexporter_users.conf -U pgexporter -P pgexporter add-user
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

## Start pgexporter

```
pgexporter -c pgexporter.conf -u pgexporter_users.conf
```

(`pgexporter` user)

## View metrics

In another terminal

```
curl http://localhost:5002/metrics
```

(`pgexporter` user)
