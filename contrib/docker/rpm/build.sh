#!/bin/bash
set -euxo pipefail

mkdir -p ~/pgexporter/build
cd ~/pgexporter/build
cmake -DCMAKE_BUILD_TYPE=Release ..
make package_source
VERSION=$(grep -Po "Version:\s*\K(\d+\.\d+\.\d+)" ~/pgexporter/pgexporter.spec)
cp pgexporter-$VERSION.tar.gz ~/rpmbuild/SOURCES/$VERSION.tar.gz
cd ~/pgexporter
QA_RPATHS=0x0001 rpmbuild -bb pgexporter.spec
