# pgexporter rpm

`pgexporter` can be built into a RPM for [Fedora](https://getfedora.org/) systems.

## Requirements

```sh
# general RPM building dependencies
dnf install rpm-build rpm-devel rpmlint coreutils diffutils patch rpmdevtools chrpath
# CMAKE building dependencies
dnf install --enablerepo=crb gcc cmake make check-devel zlib-devel bzip2-devel lz4-devel libev-devel openssl-devel python3-docutils doxygen libyaml-devel
# specific RPM building dependencies
dnf install lz4 systemd-devel
```

## Setup RPM development

```sh
rpmdev-setuptree
```

## Create source package

```sh
git clone https://github.com/pgexporter/pgexporter.git
cd pgexporter
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make package_source
```

## Create RPM package

Being still in the `pgexporter/build` directory, execute the following.
Either define the `$VERSION` variable beforehand or replace it with the version you are building.

```sh
cp pgexporter-$VERSION.tar.gz ~/rpmbuild/SOURCES/$VERSION.tar.gz
cd ..
QA_RPATHS=0x0001 rpmbuild -bb pgexporter.spec
```

The resulting RPM will be located in `~/rpmbuild/RPMS/x86_64/`, if your architecture is `x86_64`.

## Docker container as build environment

If you would like to build the RPM in a prepared Docker container, see [contrib/docker/rpm/](../contrib/docker/rpm/).
