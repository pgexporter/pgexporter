Name:          pgexporter
Version:       0.8.0
Release:       1%{dist}
Summary:       Prometheus exporter for PostgreSQL
License:       BSD
URL:           https://github.com/pgexporter/pgexporter
Source0:       %{name}-%{version}.tar.gz

BuildRequires: gcc cmake make python3-docutils zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel libev libev-devel
BuildRequires: openssl openssl-devel systemd systemd-devel libyaml libyaml-devel liburing-devel
Requires:      libev openssl systemd libyaml zlib libzstd lz4 bzip2 liburing

%description
Prometheus exporter for PostgreSQL

%prep
%setup -q

%build

%{__mkdir} build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DDOCS=OFF ..
%{__make}

%install

%{__mkdir} -p %{buildroot}%{_sysconfdir}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_libdir}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/etc
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/images
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/manual/en
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/man
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/shell_comp
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/yaml
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/json
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana/provisioning/dashboards
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana/provisioning/datasources
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/views
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/prometheus_scrape
%{__mkdir} -p %{buildroot}%{_mandir}/man1
%{__mkdir} -p %{buildroot}%{_mandir}/man5
%{__mkdir} -p %{buildroot}%{_sysconfdir}/pgexporter
%{__mkdir} -p %{buildroot}%{_datadir}/%{name}/extensions

%{__install} -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
%{__install} -m 644 %{_builddir}/%{name}-%{version}/CODE_OF_CONDUCT.md %{buildroot}%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CLI.md %{buildroot}%{_docdir}/%{name}/CLI.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/YAML.md %{buildroot}%{_docdir}/%{name}/YAML.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/DEVELOPERS.md %{buildroot}%{_docdir}/%{name}/DEVELOPERS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/TEST.md %{buildroot}%{_docdir}/%{name}/TEST.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/JSON.md %{buildroot}%{_docdir}/%{name}/JSON.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/BRIDGE.md %{buildroot}%{_docdir}/%{name}/BRIDGE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/VIEWS.md %{buildroot}%{_docdir}/%{name}/VIEWS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/SPONSORS.md %{buildroot}%{_docdir}/%{name}/SPONSORS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgexporter.service %{buildroot}%{_docdir}/%{name}/etc/pgexporter.service
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/images/* %{buildroot}%{_docdir}/%{name}/images/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/* %{buildroot}%{_docdir}/%{name}/manual/en/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/man/*.rst %{buildroot}%{_docdir}/%{name}/man/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgexporter_comp.bash %{buildroot}%{_docdir}/%{name}/shell_comp/pgexporter_comp.bash
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgexporter_comp.zsh %{buildroot}%{_docdir}/%{name}/shell_comp/pgexporter_comp.zsh
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/yaml/*.yaml %{buildroot}%{_docdir}/%{name}/yaml/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/prometheus_scrape/extra.info %{buildroot}%{_docdir}/%{name}/prometheus_scrape/extra.info
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/prometheus_scrape/prometheus.py %{buildroot}%{_docdir}/%{name}/prometheus_scrape/prometheus.py
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/prometheus_scrape/README.md %{buildroot}%{_docdir}/%{name}/prometheus_scrape/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/json/*.json %{buildroot}%{_docdir}/%{name}/json/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/*.json %{buildroot}%{_docdir}/%{name}/grafana/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/*.yml %{buildroot}%{_docdir}/%{name}/grafana/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/provisioning/dashboards/*.yml %{buildroot}%{_docdir}/%{name}/grafana/provisioning/dashboards/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/provisioning/datasources/*.yml %{buildroot}%{_docdir}/%{name}/grafana/provisioning/datasources/
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/README.md %{buildroot}%{_docdir}/%{name}/grafana/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/TESTING.md %{buildroot}%{_docdir}/%{name}/grafana/TESTING.md

%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/views/pg_system_views_extractor.sql %{buildroot}%{_docdir}/%{name}/views/pg_system_views_extractor.sql
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/pg_stat_statements.yaml %{buildroot}%{_datadir}/%{name}/extensions/pg_stat_statements.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/pg_buffercache.yaml %{buildroot}%{_datadir}/%{name}/extensions/pg_buffercache.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/pgcrypto.yaml %{buildroot}%{_datadir}/%{name}/extensions/pgcrypto.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/postgis.yaml %{buildroot}%{_datadir}/%{name}/extensions/postgis.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/postgis_raster.yaml %{buildroot}%{_datadir}/%{name}/extensions/postgis_raster.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/postgis_topology.yaml %{buildroot}%{_datadir}/%{name}/extensions/postgis_topology.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/timescaledb.yaml %{buildroot}%{_datadir}/%{name}/extensions/timescaledb.yaml
%{__install} -m 644 %{_builddir}/%{name}-%{version}/extensions/vector.yaml %{buildroot}%{_datadir}/%{name}/extensions/vector.yaml

%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgexporter.conf %{buildroot}%{_sysconfdir}/pgexporter/pgexporter.conf

%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgexporter.1 %{buildroot}%{_mandir}/man1/pgexporter.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgexporter-admin.1 %{buildroot}%{_mandir}/man1/pgexporter-admin.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgexporter-cli.1 %{buildroot}%{_mandir}/man1/pgexporter-cli.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgexporter.conf.5 %{buildroot}%{_mandir}/man5/pgexporter.conf.5

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgexporter %{buildroot}%{_bindir}/pgexporter
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgexporter-cli %{buildroot}%{_bindir}/pgexporter-cli
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgexporter-admin %{buildroot}%{_bindir}/pgexporter-admin

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/libpgexporter.so.%{version} %{buildroot}%{_libdir}/libpgexporter.so.%{version}

chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgexporter
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgexporter-cli
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgexporter-admin

cd %{buildroot}%{_libdir}/
%{__ln_s} -f libpgexporter.so.%{version} libpgexporter.so.0
%{__ln_s} -f libpgexporter.so.0 libpgexporter.so

%files
%license %{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/ARCHITECTURE.md
%{_docdir}/%{name}/BRIDGE.md
%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{_docdir}/%{name}/CLI.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/DEVELOPERS.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/JSON.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/SPONSORS.md
%{_docdir}/%{name}/TEST.md
%{_docdir}/%{name}/VIEWS.md
%{_docdir}/%{name}/YAML.md
%{_docdir}/%{name}/etc/pgexporter.service
%{_docdir}/%{name}/images/
%{_docdir}/%{name}/manual/en/
%{_docdir}/%{name}/man/*.rst
%{_docdir}/%{name}/shell_comp/pgexporter_comp.bash
%{_docdir}/%{name}/shell_comp/pgexporter_comp.zsh
%{_docdir}/%{name}/yaml/
%{_docdir}/%{name}/json/
%{_docdir}/%{name}/grafana/

%{_docdir}/%{name}/views/pg_system_views_extractor.sql
%{_docdir}/%{name}/prometheus_scrape/extra.info
%{_docdir}/%{name}/prometheus_scrape/prometheus.py
%{_docdir}/%{name}/prometheus_scrape/README.md
%{_datadir}/%{name}/extensions/pg_stat_statements.yaml
%{_datadir}/%{name}/extensions/pg_buffercache.yaml
%{_datadir}/%{name}/extensions/pgcrypto.yaml
%{_datadir}/%{name}/extensions/postgis.yaml
%{_datadir}/%{name}/extensions/postgis_raster.yaml
%{_datadir}/%{name}/extensions/postgis_topology.yaml
%{_datadir}/%{name}/extensions/timescaledb.yaml
%{_datadir}/%{name}/extensions/vector.yaml
%{_mandir}/man1/pgexporter.1*
%{_mandir}/man1/pgexporter-admin.1*
%{_mandir}/man1/pgexporter-cli.1*
%{_mandir}/man5/pgexporter.conf.5*
%config %{_sysconfdir}/pgexporter/pgexporter.conf
%{_bindir}/pgexporter
%{_bindir}/pgexporter-cli
%{_bindir}/pgexporter-admin
%{_libdir}/libpgexporter.so
%{_libdir}/libpgexporter.so.0
%{_libdir}/libpgexporter.so.%{version}

%changelog
