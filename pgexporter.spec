Name:          pgexporter
Version:       0.3.0
Release:       1%{dist}
Summary:       Prometheus exporter for PostgreSQL
License:       BSD
URL:           https://github.com/pgexporter/pgexporter
Source0:       https://github.com/pgexporter/pgexporter/archive/%{version}.tar.gz

BuildRequires: gcc cmake make python3-docutils
BuildRequires: libev libev-devel openssl openssl-devel systemd systemd-devel
Requires:      libev openssl systemd

%description
Prometheus exporter for PostgreSQL

%prep
%setup -q

%build

%{__mkdir} build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
%{__make}

%install

%{__mkdir} -p %{buildroot}%{_sysconfdir}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_libdir}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/etc
%{__mkdir} -p %{buildroot}%{_mandir}/man1
%{__mkdir} -p %{buildroot}%{_mandir}/man5
%{__mkdir} -p %{buildroot}%{_sysconfdir}/pgexporter

%{__install} -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
%{__install} -m 644 %{_builddir}/%{name}-%{version}/CODE_OF_CONDUCT.md %{buildroot}%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CLI.md %{buildroot}%{_docdir}/%{name}/CLI.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgexporter.service %{buildroot}%{_docdir}/%{name}/etc/pgexporter.service

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
%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{_docdir}/%{name}/CLI.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/etc/pgexporter.service
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
