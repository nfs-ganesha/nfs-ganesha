%define __arch_install_post   /usr/lib/rpm/check-rpaths   /usr/lib/rpm/check-buildroot

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 7
%global with_nfsidmap 1
%else
%global with_nfsidmap 0
%endif

%if ( 0%{?fedora} >= 18 || 0%{?rhel} >= 7 ) && ! %{!?bl6:0}
%global with_systemd 1
%else
%global with_systemd 0
%endif

# Conditionally enable some FSALs, disable others.
#
# 1. rpmbuild accepts these options (gpfs as example):
#    --with gpfs
#    --without gpfs
# 2. CMake enables FSALs by detecting available support on the build system.
#
# Build the FSAL when "--with <fsal>" or CMake detected support.
# Override CMake's decision by passing "--without <fsal>" to rpmbuild.

%if 0%{?_with_gpfs:1} || (0%{!?_without_gpfs:1} && "@USE_FSAL_GPFS@" == "ON")
%global with_fsal_gpfs 1
%else
%global with_fsal_gpfs 0
%endif

%if 0%{?_with_zfs:1} || (0%{!?_without_zfs:1} && "@USE_FSAL_ZFS@" == "ON")
%global with_fsal_zfs 1
%else
%global with_fsal_zfs 0
%endif

%if 0%{?_with_xfs:1} || (0%{!?_without_xfs:1} && "@USE_FSAL_XFS@" == "ON")
%global with_fsal_xfs 1
%else
%global with_fsal_xfs 0
%endif

%if 0%{?_with_ceph:1} || (0%{!?_without_ceph:1} && "@USE_FSAL_CEPH@" == "ON")
%global with_fsal_ceph 1
%else
%global with_fsal_ceph 0
%endif

%if 0%{?_with_lustre:1} || (0%{!?_without_lustre:1} && "@USE_FSAL_LUSTRE@" == "ON")
%global with_fsal_lustre 1
%else
%global with_fsal_lustre 0
%endif

%if 0%{?_with_shook:1} || (0%{!?_without_shook:1} && "@USE_FSAL_SHOOK@" == "ON")
%global with_fsal_shook 1
%else
%global with_fsal_shook 0
%endif

%if 0%{?_with_gluster:1} || (0%{!?_without_gluster:1} && "@USE_FSAL_GLUSTER@" == "ON")
%global with_fsal_gluster 1
%else
%global with_fsal_gluster 0
%endif

%if 0%{?_with_hpss:1} || (0%{!?_without_hpss:1} && "@USE_FSAL_HPSS@" == "ON")
%global with_fsal_hpss 1
%else
%global with_fsal_hpss 0
%endif

%if 0%{?_with_panfs:1} || (0%{!?_without_pan:1} && "@USE_FSAL_PANFS@" == "ON")
%global with_fsal_panfs 1
%else
%global with_fsal_panfs 0
%endif

%if 0%{?_with_pt:1} || (0%{!?_without_pt:1} && "@USE_FSAL_PT@" == "ON")
%global with_fsal_pt 1
%else
%global with_fsal_pt 0
%endif

%if 0%{?_with_rdma:1} || (0%{!?_without_rdma:1} && "@USE_9P_RDMA@" == "ON")
%global with_rdma 1
%else
%global with_rdma 0
%endif

%if %{?_with_jemalloc:1}%{!?_with_jemalloc:0}
%global with_jemalloc 1
%else
%global with_jemalloc 0
%endif

%if %{?_with_lustre_up:1}%{!?_with_lustre_up:0}
%global with_lustre_up 1
%else
%global with_lustre_up 0
%endif

%if %{?_with_lttng:1}%{!?_with_lttng:0}
%global with_lttng 1
%else
%global with_lttng 0
%endif

%if 0%{?_with_utils:1} || (0%{!?_without_utils:1} && "@USE_ADMIN_TOOLS@" == "ON")
%global with_utils 1
%else
%global with_utils 0
%endif

%global dev_version %{lua: extraver = string.gsub('@GANESHA_EXTRA_VERSION@', '%-', '_'); print(extraver) }

%define sourcename @CPACK_SOURCE_PACKAGE_FILE_NAME@

Name:		nfs-ganesha
Version:	@GANESHA_BASE_VERSION@%{dev_version}
Release:	1%{?dist}
Summary:	NFS-Ganesha is a NFS Server running in user space
Group:		Applications/System
License:	LGPLv3+
Url:               https://github.com/nfs-ganesha/nfs-ganesha/wiki

Source:		%{sourcename}.tar.gz

BuildRequires:	cmake
BuildRequires:	bison flex
BuildRequires:	flex
BuildRequires:	pkgconfig
BuildRequires:	krb5-devel
BuildRequires:	dbus-devel
BuildRequires:	libcap-devel
BuildRequires:	libblkid-devel
BuildRequires:	libuuid-devel
Requires:	dbus
%if %{with_nfsidmap}
BuildRequires:	libnfsidmap-devel
%else
BuildRequires:	nfs-utils-lib-devel
%endif
%if %{with_rdma}
BuildRequires:	libmooshika-devel >= 0.6-0
%endif
%if %{with_jemalloc}
BuildRequires:	jemalloc-devel
%endif
%if %{with_lustre_up}
BuildRequires: lcap-devel >= 0.1-0
%endif
%if %{with_systemd}
BuildRequires: systemd
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
%else
BuildRequires:	initscripts
%endif

# Use CMake variables

%description
nfs-ganesha : NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as
 shared objects to support different file systems and name-spaces.

%package mount-9P
Summary: a 9p mount helper
Group: Applications/System

%description mount-9P
This package contains the mount.9P script that clients can use
to simplify mounting to NFS-GANESHA. This is a 9p mount helper.

%package vfs
Summary: The NFS-GANESHA's VFS FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha

%description vfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support VFS based filesystems

%package nullfs
Summary: The NFS-GANESHA's NULLFS Stackable FSAL
Group: Applications/System

%description nullfs
This package contains a Stackble FSAL shared object to
be used with NFS-Ganesha. This is mostly a template for future (more sophisticated) stackable FSALs

%package proxy
Summary: The NFS-GANESHA's PROXY FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha

%description proxy
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PROXY based filesystems

%if %{with_utils}
%package utils
Summary: The NFS-GANESHA's util scripts
Group: Applications/System
BuildRequires:	PyQt4-devel
Requires:	PyQt4
Requires: nfs-ganesha python

%description utils
This package contains utility scripts for managing the NFS-GANESHA server
%endif

%if %{with_lttng}
%package lttng
Summary: The NFS-GANESHA's library for use with LTTng
Group: Applications/System
BuildRequires: lttng-ust-devel >= 2.3
Requires: nfs-ganesha, lttng-tools >= 2.3,  lttng-ust >= 2.3

%description lttng
This package contains the libganesha_trace.so library. When preloaded
to the ganesha.nfsd server, it makes it possible to trace using LTTng.
%endif

# Option packages start here. use "rpmbuild --with lustre" (or equivalent)
# for activating this part of the spec file

# GPFS
%if %{with_fsal_gpfs}
%package gpfs
Summary: The NFS-GANESHA's GPFS FSAL
Group: Applications/System

%description gpfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support GPFS backend
%endif

# ZFS
%if %{with_fsal_zfs}
%package zfs
Summary: The NFS-GANESHA's ZFS FSAL
Group: Applications/System
Requires:	nfs-ganesha
BuildRequires:	libzfswrap-devel

%description zfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support ZFS
%endif

# CEPH
%if %{with_fsal_ceph}
%package ceph
Summary: The NFS-GANESHA's CEPH FSAL
Group: Applications/System
Requires:	ceph >= 0.78
BuildRequires:	ceph-devel >= 0.78

%description ceph
This package contains a FSAL shared object to
be used with NFS-Ganesha to support CEPH
%endif

# LUSTRE
%if %{with_fsal_lustre}
%package lustre
Summary: The NFS-GANESHA's LUSTRE FSAL
Group: Applications/System
Requires:	lustre nfs-ganesha
BuildRequires:	libattr-devel lustre

%description lustre
This package contains a FSAL shared object to
be used with NFS-Ganesha to support LUSTRE
%endif

# SHOOK
%if %{with_fsal_shook}
%package shook
Summary: The NFS-GANESHA's LUSTRE/SHOOK FSAL
Group: Applications/System
Requires:	lustre shook-client nfs-ganesha
BuildRequires:	libattr-devel lustre shook-devel

%description shook
This package contains a FSAL shared object to
be used with NFS-Ganesha to support LUSTRE via SHOOK
%endif

# XFS
%if %{with_fsal_xfs}
%package xfs
Summary: The NFS-GANESHA's XFS FSAL
Group: Applications/System
Requires:	nfs-ganesha
BuildRequires:	libattr-devel xfsprogs-devel

%description xfs
This package contains a shared object to be used with FSAL_VFS
to support XFS correctly
%endif

# HPSS
%if %{with_fsal_hpss}
%package hpss
Summary: The NFS-GANESHA's HPSS FSAL
Group: Applications/System
Requires:	nfs-ganesha
#BuildRequires:	hpssfs

%description hpss
This package contains a FSAL shared object to
be used with NFS-Ganesha to support HPSS
%endif

# PANFS
%if %{with_fsal_panfs}
%package panfs
Summary: The NFS-GANESHA's PANFS FSAL
Group: Applications/System
Requires:	nfs-ganesha

%description panfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PANFS
%endif

# PT
%if %{with_fsal_pt}
%package pt
Summary: The NFS-GANESHA's PT FSAL
Group: Applications/System
Requires:	nfs-ganesha

%description pt
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PT
%endif

# GLUSTER
%if %{with_fsal_gluster}
%package gluster
Summary: The NFS-GANESHA's GLUSTER FSAL
Group: Applications/System
Requires:	nfs-ganesha
BuildRequires:	glusterfs-api-devel >= 3.5.1
BuildRequires:  libattr-devel

%description gluster
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Gluster
%endif

%prep
%setup -q -n %{sourcename}

%build
cmake .	-DCMAKE_BUILD_TYPE=Debug			\
	-DBUILD_CONFIG=rpmbuild				\
%if %{with_fsal_zfs}
	-DUSE_FSAL_ZFS=ON				\
%else
	-DUSE_FSAL_ZFS=OFF				\
%endif
%if %{with_fsal_xfs}
	-DUSE_FSAL_XFS=ON				\
%else
	-DUSE_FSAL_XFS=OFF				\
%endif
%if %{with_fsal_ceph}
	-DUSE_FSAL_CEPH=ON				\
%else
	-DUSE_FSAL_CEPH=OFF				\
%endif
%if %{with_fsal_lustre}
	-DUSE_FSAL_LUSTRE=ON				\
%else
	-DUSE_FSAL_LUSTRE=OFF				\
%endif
%if %{with_fsal_shook}
	-DUSE_FSAL_SHOOK=ON				\
%else
	-DUSE_FSAL_SHOOK=OFF				\
%endif
%if %{with_fsal_gpfs}
	-DUSE_FSAL_GPFS=ON				\
%else
	-DUSE_FSAL_GPFS=OFF				\
%endif
%if %{with_fsal_hpss}
	-DUSE_FSAL_HPSS=ON				\
%else
	-DUSE_FSAL_HPSS=OFF				\
%endif
%if %{with_fsal_panfs}
	-DUSE_FSAL_PANFS=ON				\
%else
	-DUSE_FSAL_PANFS=OFF				\
%endif
%if %{with_fsal_pt}
	-DUSE_FSAL_PT=ON				\
%else
	-DUSE_FSAL_PT=OFF				\
%endif
%if %{with_fsal_gluster}
	-DUSE_FSAL_GLUSTER=ON				\
%else
	-DUSE_FSAL_GLUSTER=OFF				\
%endif
%if %{with_rdma}
	-DUSE_9P_RDMA=ON				\
%endif
%if %{with_jemalloc}
	-DALLOCATOR=jemalloc				\
%endif
%if %{with_lustre_up}
	-DUSE_FSAL_LUSTRE_UP=ON				\
%endif
%if %{with_lttng}
	-DUSE_LTTNG=ON				\
%endif
%if %{with_utils}
        -DUSE_ADMIN_TOOLS=ON                            \
%endif
	-DUSE_FSAL_VFS=ON				\
	-DUSE_FSAL_PROXY=ON				\
	-DUSE_DBUS=ON					\
	-DUSE_9P=ON					\
	-DDISTNAME_HAS_GIT_DATA=OFF

make %{?_smp_mflags} || make %{?_smp_mflags} || make

%install
mkdir -p %{buildroot}%{_sysconfdir}/ganesha/
mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
mkdir -p %{buildroot}%{_sysconfdir}/sysconfig
mkdir -p %{buildroot}%{_sysconfdir}/logrotate.d
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sbindir}
mkdir -p %{buildroot}%{_libdir}/ganesha
mkdir -p %{buildroot}%{_localstatedir}/run/ganesha
install -m 644 config_samples/logrotate_ganesha         %{buildroot}%{_sysconfdir}/logrotate.d/ganesha
install -m 644 scripts/ganeshactl/org.ganesha.nfsd.conf	%{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 755 tools/mount.9P				%{buildroot}%{_sbindir}/mount.9P

install -m 644 config_samples/vfs.conf             %{buildroot}%{_sysconfdir}/ganesha

%if %{with_systemd}
mkdir -p %{buildroot}%{_unitdir}
install -m 644 scripts/systemd/nfs-ganesha.service	%{buildroot}%{_unitdir}/nfs-ganesha.service
install -m 644 scripts/systemd/nfs-ganesha-lock.service	%{buildroot}%{_unitdir}/nfs-ganesha-lock.service
install -m 755 scripts/systemd/sysconfig/nfs-ganesha	%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%else
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 ganesha.init				%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha
install -m 755 ganesha.sysconfig			%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%endif

%if %{with_utils} && 0%{?rhel} && 0%{?rhel} <= 6
%{!?__python2: %global __python2 /usr/bin/python2}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}
%{!?python2_sitearch: %global python2_sitearch %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib(1))")}
%endif

%if %{with_fsal_pt}
install -m 755 ganesha.pt.init                            %{buildroot}%{_sysconfdir}/init.d/nfs-ganesha-pt
install -m 644 config_samples/pt.conf                     %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_fsal_xfs}
install -m 755 config_samples/xfs.conf			%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_fsal_zfs}
install -m 755 config_samples/zfs.conf			%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_fsal_ceph}
install -m 755 config_samples/ceph.conf			%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_fsal_lustre}
install -m 755 config_samples/lustre.conf		%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_fsal_gpfs}
install -m 755 config_samples/gpfs.conf			%{buildroot}%{_sysconfdir}/ganesha
install -m 755 config_samples/gpfs.ganesha.nfsd.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 755 config_samples/gpfs.ganesha.main.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 755 config_samples/gpfs.ganesha.log.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 755 config_samples/gpfs.ganesha.exports.conf	%{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with_utils}
pushd .
cd scripts/ganeshactl/
python setup.py --quiet install --root=%{buildroot}
popd
install -m 755 Protocols/NLM/sm_notify.ganesha		%{buildroot}%{_bindir}/sm_notify.ganesha
%endif

make DESTDIR=%{buildroot} install

%post
%if %{with_systemd}
%systemd_post nfs-ganesha.service
%systemd_post nfs-ganesha-lock.service
%endif

%preun
%if %{with_systemd}
%systemd_preun nfs-ganesha-lock.service
%endif

%postun
%if %{with_systemd}
%systemd_postun_with_restart nfs-ganesha-lock.service
%endif

%files
%defattr(-,root,root,-)
%{_bindir}/ganesha.nfsd
%{_bindir}/libntirpc.a
%config %{_sysconfdir}/dbus-1/system.d/org.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/sysconfig/ganesha
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha
%dir %{_sysconfdir}/ganesha/
%config(noreplace) %{_sysconfdir}/ganesha/ganesha.conf
%dir %{_defaultdocdir}/ganesha/
%{_defaultdocdir}/ganesha/*
%dir %{_localstatedir}/run/ganesha

%if %{with_systemd}
%{_unitdir}/nfs-ganesha.service
%{_unitdir}/nfs-ganesha-lock.service
%else
%{_sysconfdir}/init.d/nfs-ganesha
%endif

%files mount-9P
%defattr(-,root,root,-)
%{_sbindir}/mount.9P


%files vfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalvfs*
%config(noreplace) %{_sysconfdir}/ganesha/vfs.conf


%files nullfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalnull*


%files proxy
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalproxy*

# Optionnal packages
%if %{with_fsal_gpfs}
%files gpfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalgpfs*
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.main.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.log.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.exports.conf
%endif

%if %{with_fsal_zfs}
%files zfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalzfs*
%config(noreplace) %{_sysconfdir}/ganesha/zfs.conf
%endif

%if %{with_fsal_xfs}
%files xfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalxfs*
%config(noreplace) %{_sysconfdir}/ganesha/xfs.conf
%endif

%if %{with_fsal_ceph}
%files ceph
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalceph*
%config(noreplace) %{_sysconfdir}/ganesha/ceph.conf
%endif

%if %{with_fsal_lustre}
%files lustre
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/ganesha/lustre.conf
%{_libdir}/ganesha/libfsallustre*
%endif

%if %{with_fsal_shook}
%files shook
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalshook*
%endif

%if %{with_fsal_gluster}
%files gluster
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalgluster*
%endif

%if %{with_fsal_hpss}
%files hpss
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalhpss*
%endif

%if %{with_fsal_panfs}
%files panfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpanfs*
%endif

%if %{with_fsal_pt}
%files pt
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpt*
%config(noreplace) %{_sysconfdir}/init.d/nfs-ganesha-pt
%config(noreplace) %{_sysconfdir}/ganesha/pt.conf
%endif

%if %{with_lttng}
%files lttng
%defattr(-,root,root,-)
%{_libdir}/ganesha/libganesha_trace*
%endif

%if %{with_utils}
%files utils
%defattr(-,root,root,-)
%{python2_sitelib}/Ganesha/*
%{python2_sitelib}/ganeshactl-*-info
/usr/bin/ganesha-admin
/usr/bin/manage_clients
/usr/bin/manage_exports
/usr/bin/manage_logger
/usr/bin/ganeshactl
/usr/bin/fake_recall
/usr/bin/get_clientids
/usr/bin/grace_period
/usr/bin/purge_gids
/usr/bin/ganesha_stats
/usr/bin/sm_notify.ganesha
/usr/bin/ganesha_mgr
%endif


%changelog
* Mon Jan 5 2015 Niels de Vos <ndevos@redhat.com>
- Add a panfs subpackage.

* Mon Dec 15 2014 Niels de Vos <ndevos@redhat.com>
- Enable building against jemalloc.

* Thu Nov 20 2014 Niels de Vos <ndevos@redhat.com>
- Include the systemd unit in RHEL7.
- Include the nfs-ganesha-lock systemd unit.

* Fri Jun 27 2014  Philippe DENIEL <philippe.deniel@cea.fr> 2.1
- Exports are now dynamic.  They can be added or removed via DBus commands.
- The Pseudo filesystem has been re-written as a FSAL
- The configuration file processing has been rewritten to improve error checking and logging.
- GIDs can now be managed to use external authentication sources. Altgroups with AUTH_SYS can be larger than 16.
- RPM packaging has been restructured and updated.  The DBus tools are now packaged.

* Thu Nov 21 2013  Philippe DENIEL <philippe.deniel@cea.fr> 2.O
- FSALs (filesystem backends) are now loadable shared objects.
- The server can support multiple backends at runtime.
- NFSv4.1 pNFS is supported.
- DBus is now the administration tool.
- All the significant bugfixes from the 1.5.x branch have been backported
- The server passes all of the cthonv4 and pynfs 4.0 tests.
-  All of the significant (non-delegation) pynfs 4.1 tests also pass.
- NFSv2 support has been deprecated.
- NFSv3 still supports the older version of the MNT protocol for compatibility
- The build process has been converted to Cmake
- The codebase has been reformatted to conform to Linux kernel coding style.

