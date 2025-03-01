# Developer guide

## Install PostgreSQL

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

**Fedora 41**

```sh
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/F-41-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

**RHEL 9.x / Rocky Linux 9.x**

**x86_64**

```sh
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
dnf config-manager --set-enabled crb
```

**aarch64**

```sh
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
rpm -Uvh https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-aarch64/pgdg-redhat-repo-latest.noarch.rpm
dnf config-manager --set-enabled crb
```

**PostgreSQL 17**

``` sh
dnf -qy module disable postgresql
dnf install -y postgresql17 postgresql17-server postgresql17-contrib postgresql17-libs postgresql17-devel
```

This will install PostgreSQL 17.

## Install pgexporter

### Pre-install

#### Basic dependencies

``` sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel libyaml libyaml-devel python3-docutils libatomic zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel
```

#### Generate user and developer guide

This process is optional. If you choose not to generate the PDF and HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

1. Download dependencies

    ``` sh
    dnf install pandoc texlive-scheme-basic
    ```

2. Download Eisvogel

    Use the command `pandoc --version` to locate the user data directory. On Fedora systems, this directory is typically located at `$HOME/.local/share/pandoc`.

    Download the `Eisvogel` template for `pandoc`, please visit the [pandoc-latex-template](https://github.com/Wandmalfarbe/pandoc-latex-template) repository. For a standard installation, you can follow the steps outlined below.

    ```sh
    wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/2.4.2/Eisvogel-2.4.2.tar.gz
    tar -xzf Eisvogel-2.4.2.tar.gz
    mkdir -p $HOME/.local/share/pandoc/templates
    mv eisvogel.latex $HOME/.local/share/pandoc/templates/
    ```

3. Add package for LaTeX

    Download the additional packages required for generating PDF and HTML files.

    ```sh
    dnf install 'tex(footnote.sty)' 'tex(footnotebackref.sty)' 'tex(pagecolor.sty)' 'tex(hardwrap.sty)' 'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' 'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' 'tex(titling.sty)' 'tex(csquotes.sty)' 'tex(zref-abspage.sty)' 'tex(needspace.sty)'
    ```

#### Generate API guide

This process is optional. If you choose not to generate the API HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

Download dependencies

    ``` sh
    dnf install graphviz doxygen
    ```

### Build

``` sh
cd /usr/local
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
make install
```

This will install [**pgexporter**](https://github.com/pgexporter/pgexporter) in the `/usr/local` hierarchy with the debug profile.

### Check version

You can navigate to `build/src` and execute `./pgexporter -?` to make the call. Alternatively, you can install it into `/usr/local/` and call it directly using:

``` sh
pgexporter -?
```

If you see an error saying `error while loading shared libraries: libpgexporter.so.0: cannot open shared object` running the above command. you may need to locate where your `libpgexporter.so.0` is. It could be in `/usr/local/lib` or `/usr/local/lib64` depending on your environment. Add the corresponding directory into `/etc/ld.so.conf`.

To enable these directories, you would typically add the following lines in your `/etc/ld.so.conf` file:

``` sh
/usr/local/lib
/usr/local/lib64
```

Remember to run `ldconfig` to make the change effective.

## Setup pgexporter

Let's give it a try. The basic idea here is that we will use two users: one is `postgres`, which will run PostgreSQL, and one is [**pgexporter**](https://github.com/pgexporter/pgexporter), which will run [**pgexporter**](https://github.com/pgexporter/pgexporter) to do backup of PostgreSQL.

In many installations, there is already an operating system user named `postgres` that is used to run the PostgreSQL server. You can use the command

``` sh
getent passwd | grep postgres
```

to check if your OS has a user named postgres. If not use

``` sh
useradd -ms /bin/bash postgres
passwd postgres
```

If the postgres user already exists, don't forget to set its password for convenience.

### 1. postgres

Open a new window, switch to the `postgres` user. This section will always operate within this user space.

``` sh
sudo su -
su - postgres
```

#### Initialize cluster

If you use dnf to install your postgresql, chances are the binary file is in `/usr/bin/`

``` sh
export PATH=/usr/bin:$PATH
initdb /tmp/pgsql
```

#### Add access for users and a database

Add new lines to `/tmp/pgsql/pg_hba.conf`

``` ini
local   postgres        pgexporter                             scram-sha-256
host    postgres        pgexporter     127.0.0.1/32            scram-sha-256
host    postgres        pgexporter     ::1/128                 scram-sha-256
```

#### Set password_encryption

Set `password_encryption` value in `/tmp/pgsql/postgresql.conf` to be `scram-sha-256`

``` sh
password_encryption = scram-sha-256
```

For version 13, the default is `md5`, while for version 14 and above, it is `scram-sha-256`. Therefore, you should ensure that the value in `/tmp/pgsql/postgresql.conf` matches the value in `/tmp/pgsql/pg_hba.conf`.

#### Start PostgreSQL

``` sh
pg_ctl -D /tmp/pgsql/ start
```

Here, you may encounter issues such as the port being occupied or permission being denied. If you experience a failure, you can go to `/tmp/pgsql/log` to check the reason.

You can use

``` sh
pg_isready
```

to test

#### Add users and a database

``` sh
psql postgres
CREATE ROLE pgexporter WITH NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION LOGIN PASSWORD 'secretpassword';
GRANT pg_monitor TO pgexporter;
\q
```

#### Verify access

For the user `pgexporter` (standard) use `secretpassword`

``` sh
psql -h localhost -p 5432 -U pgexporter postgres
\q
```

#### Add pgexporter user

``` sh
sudo su -
useradd -ms /bin/bash pgexporter
passwd pgexporter
exit
```

### 2. pgexporter

Open a new window, switch to the `pgexporter` user. This section will always operate within this user space.

``` sh
sudo su -
su - pgexporter
```

#### Create pgexporter configuration

Add the master key

``` sh
pgexporter-admin master-key
```

You have to choose a password for the master key and it must be at least 8 characters - remember it!

then create vault

``` sh
pgexporter-admin -f pgexporter_users.conf -U pgexporter -P secretpassword user add
``` 

Input the replication user and its password to grant [**pgexporter**](https://github.com/pgexporter/pgexporter) access to the database. Ensure that the information is correct.

Create the `pgexporter.conf` configuration file to use when running [**pgexporter**](https://github.com/pgexporter/pgexporter).

``` ini
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

In our main section called `[pgexporter]` we setup [**pgexporter**](https://github.com/pgexporter/pgexporter) to listen on all network addresses. We will enable Prometheus metrics on port 5002.
Logging will be performed at `info` level and put in a file called `/tmp/pgexporter.log`. Last we specify the location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our PostgreSQL instance. In this case it is running on localhost on port 5432 and we will use the pgexporter user account to connect.

#### Start pgexporter

``` sh
pgexporter -c pgexporter.conf -u pgexporter_users.conf
```

#### Shutdown pgexporter

``` sh
pgexporter-cli -c pgexporter.conf shutdown
```

## Basic git guide

Here are some links that will help you

* [How to Squash Commits in Git](https://www.git-tower.com/learn/git/faq/git-squash)
* [ProGit book](https://github.com/progit/progit2/releases)

### Start by forking the repository

This is done by the "Fork" button on GitHub.

### Clone your repository locally

This is done by

```sh
git clone git@github.com:<username>/pgexporter.git
```

### Add upstream

Do

```sh
cd pgexporter
git remote add upstream https://github.com/pgexporter/pgexporter.git
```

### Do a work branch

```sh
git checkout -b mywork main
```

### Make the changes

Remember to verify the compile and execution of the code

### AUTHORS

Remember to add your name to

```
AUTHORS
doc/manual/97-acknowledgement.md
doc/manual/advanced/97-acknowledgement.md
```

in your first pull request

### Multiple commits

If you have multiple commits on your branch then squash them

``` sh
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

### Rebase

Always rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

### Force push

When you are done with your changes force push your branch

``` sh
git push -f origin mywork
```

and then create a pull requests for it

### Repeat

Based on feedback keep making changes, squashing, rebasing and force pushing

### Undo

Normally you can reset to an earlier commit using `git reset <commit hash> --hard`. 
But if you accidentally squashed two or more commits, and you want to undo that, 
you need to know where to reset to, and the commit seems to have lost after you rebased. 

But they are not actually lost - using `git reflog`, you can find every commit the HEAD pointer
has ever pointed to. Find the commit you want to reset to, and do `git reset --hard`.
