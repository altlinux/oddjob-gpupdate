%define _unpackaged_files_terminate_build 1

Name: oddjob-gpupdate
Version: 0.2.2
Release: alt1
Summary: An oddjob helper which applies group policy objects

Group: System/Servers
License: %bsdstyle
Url: https://github.com/altlinux/oddjob-gpupdate.git

Source: %name-%version.tar

Requires: oddjob

BuildRequires(pre): rpm-build-licenses

BuildRequires: xmlto
BuildRequires: libdbus-devel
BuildRequires: libxml2-devel
BuildRequires: libpam0-devel
BuildRequires: libselinux-devel

%description
This package contains the oddjob helper which can be used by the
pam_oddjob_gpupdate module to applies group policy objects at login-time.

%prep
%setup

%build
%autoreconf
%configure \
    --disable-static \
    --enable-pie \
    --enable-now \
    --with-selinux-acls \
    --with-selinux-labels
%make_build

%install
%makeinstall_std

mkdir -p %buildroot/%_lib/security
mv %buildroot%_libdir/security/pam_oddjob_gpupdate.so \
%buildroot/%_lib/security/
rm %buildroot%_libdir/security/pam_oddjob_gpupdate.la

%post
%post_service oddjobd

%preun
%preun_service oddjobd

%files
%doc COPYING src/gpupdatefor src/gpupdateforme
%_libexecdir/oddjob/gpupdate
/%_lib/security/pam_oddjob_gpupdate.so
%_mandir/*/pam_oddjob_gpupdate.*
%_mandir/*/oddjob-gpupdate.*
%_mandir/*/oddjobd-gpupdate.*
%config(noreplace) %_sysconfdir/dbus-*/system.d/oddjob-gpupdate.conf
%config(noreplace) %_sysconfdir/oddjobd.conf.d/oddjobd-gpupdate.conf

%changelog

