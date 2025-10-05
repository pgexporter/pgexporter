ARG OS_VERSION=9

FROM almalinux:$OS_VERSION-minimal

RUN microdnf upgrade -y
RUN microdnf install -y git gcc cmake make rpm-build rpm-devel rpmlint diffutils patch rpmdevtools chrpath
RUN microdnf install --enablerepo=crb -y check-devel zlib-devel bzip2-devel lz4-devel libev-devel openssl-devel python3-docutils doxygen libyaml-devel lz4 systemd-devel
RUN microdnf clean all && rm -rf /var/cache/yum
WORKDIR /root
RUN rpmdev-setuptree

COPY contrib/docker/rpm/build.sh /root/build.sh

RUN chmod +x /root/build.sh
COPY . /root/pgexporter
RUN rm -rf /root/pgexporter/build /root/pgexporter/cmake-build-debug
