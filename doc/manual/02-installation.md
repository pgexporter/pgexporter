\newpage

# Installation

## Fedora

You need to add the [PostgreSQL YUM repository](https://yum.postgresql.org/), for example for Fedora 39

```
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/F-39-x86_64/pgdg-fedora-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y pgexporter
```

* [PostgreSQL YUM](https://yum.postgresql.org/howto/)
* [Linux downloads](https://www.postgresql.org/download/linux/redhat/)

## RHEL 9 / RockyLinux 9

```
dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

and do the install via

```
dnf install -y pgexporter
```

## Compiling the source

We recommend using Fedora to test and run [**pgexporter**][pgexporter], but other Linux systems and FreeBSD are also supported.

[**pgexporter**][pgexporter] requires

* [gcc 8+](https://gcc.gnu.org) (C17)
* [cmake](https://cmake.org)
* [make](https://www.gnu.org/software/make/)
* [libev](http://software.schmorp.de/pkg/libev.html)
* [OpenSSL](http://www.openssl.org/)
* [systemd](https://www.freedesktop.org/wiki/Software/systemd/)
* [libyaml](https://pyyaml.org/wiki/LibYAML)

```sh
dnf install git gcc cmake make libev libev-devel \
            openssl openssl-devel \
            systemd systemd-devel \
            python3-docutils libatomic \
            libyaml libyaml-devel
            zlib zlib-devel \
            libzstd libzstd-devel \
            libasan libasan-static \
            lz4 lz4-devel \
            bzip2 bzip2-devel
```

Alternative [clang 8+](https://clang.llvm.org/) can be used.


### RHEL / RockyLinux

On RHEL / Rocky, before you install the required packages some additional repositoriesneed to be enabled or installed first.

First you need to install the subscription-manager

``` sh
dnf install subscription-manager
```

It is ok to disregard the registration and subscription warning.

Otherwise, if you have a Red Hat corporate account (you need to specify the company/organization name in your account), you can register using

``` sh
subscription-manager register --username <your-account-email-or-login> --password <your-password> --auto-attach
```

Then install the EPEL repository,

``` sh
dnf install epel-release
```

Then to enable CodeReady Builder

``` sh
dnf config-manager --set-enabled codeready-builder-for-rhel-9-rhui-rpms
dnf config-manager --set-enabled crb
dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm
```

Then use the `dnf` command for [**pgexporter**][pgexporter] to install the required packages.


### FreeBSD

On FreeBSD, `pkg` is used instead of `dnf` or `yum`.

Use `pkg install <package name>` to install the following packages

``` sh
git gcc cmake libev openssl py39-docutils libyaml
```

### Build

#### Release build

The following commands will install [**pgexporter**][pgexporter] in the `/usr/local` hierarchy.

```sh
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

See [RPM](https://github.com/pgexporter/pgexporter/blob/main/doc/RPM.md) for how to build a RPM of [**pgexporter**][pgexporter].

#### Debug build

The following commands will create a `DEBUG` version of [**pgexporter**][pgexporter].

```sh
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Compiling the documentation

[**pgexporter**][pgexporter]'s documentation requires

* [pandoc](https://pandoc.org/)
* [texlive](https://www.tug.org/texlive/)

```sh
dnf install pandoc texlive-scheme-basic \
            'tex(footnote.sty)' 'tex(footnotebackref.sty)' \
            'tex(pagecolor.sty)' 'tex(hardwrap.sty)' \
            'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' \
            'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' \
            'tex(titling.sty)' 'tex(csquotes.sty)' \
            'tex(zref-abspage.sty)' 'tex(needspace.sty)'
```

You will need the `Eisvogel` template as well which you can install through

```sh
wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/v3.2.0/Eisvogel-3.2.0.tar.gz
tar -xzf Eisvogel-3.2.0.tar.gz
mkdir -p ~/.local/share/pandoc/templates
mv Eisvogel-3.2.0/eisvogel.latex ~/.local/share/pandoc/templates/
```

where `$HOME` is your home directory.

### Generate API guide

This process is optional. If you choose not to generate the API HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

Download dependencies

``` sh
dnf install graphviz doxygen
```

### Build

These packages will be detected during `cmake` and built as part of the main build.
