%define __arch_install_post /usr/lib/rpm/check-rpaths /usr/lib/rpm/check-buildroot

%if 0%{?fedora} >= 15 || 0%{?rhel} >= 7
%global with_nfsidmap 1
%else
%global with_nfsidmap 0
%endif

%if ( 0%{?fedora} >= 18 || 0%{?rhel} >= 7 )
%global with_systemd 1
%else
%global with_systemd 0
%endif

%if ( 0%{?suse_version} )
BuildRequires: sles-release >= 12
Requires: sles-release >= 12

%global with_systemd 1
%global with_nfsidmap 1
%endif

# Conditionally enable some FSALs, disable others.
#
# 1. rpmbuild accepts these options (gpfs as example):
#    --with gpfs
#    --without gpfs

%define on_off_switch() %%{?with_%1:ON}%%{!?with_%1:OFF}

# A few explanation about %bcond_with and %bcond_without
# /!\ be careful: this syntax can be quite messy
# %bcond_with means you add a "--with" option, default = without this feature
# %bcond_without adds a"--without" so the feature is enabled by default

@BCOND_NULLFS@ nullfs
%global use_fsal_null %{on_off_switch nullfs}

@BCOND_GPFS@ gpfs
%global use_fsal_gpfs %{on_off_switch gpfs}

@BCOND_ZFS@ zfs
%global use_fsal_zfs %{on_off_switch zfs}

@BCOND_XFS@ xfs
%global use_fsal_xfs %{on_off_switch xfs}

@BCOND_CEPH@ ceph
%global use_fsal_ceph %{on_off_switch ceph}

@BCOND_RGW@ rgw
%global use_fsal_rgw %{on_off_switch rgw}

@BCOND_GLUSTER@ gluster
%global use_fsal_gluster %{on_off_switch gluster}

@BCOND_PANFS@ panfs
%global use_fsal_panfs %{on_off_switch panfs}

@BCOND_RDMA@ rdma
%global use_rdma %{on_off_switch rdma}

@BCOND_JEMALLOC@ jemalloc

@BCOND_LTTNG@ lttng
%global use_lttng %{on_off_switch lttng}

@BCOND_UTILS@ utils
%global use_utils %{on_off_switch utils}

@BCOND_GUI_UTILS@ gui_utils
%global use_gui_utils %{on_off_switch gui_utils}

@BCOND_NTIRPC@ system_ntirpc
%global use_system_ntirpc %{on_off_switch system_ntirpc}

%global dev_version %{lua: extraver = string.gsub('@GANESHA_EXTRA_VERSION@', '%-', '.'); print(extraver) }

%define sourcename @CPACK_SOURCE_PACKAGE_FILE_NAME@

Name:		nfs-ganesha
Version:	@GANESHA_BASE_VERSION@
Release:	0%{dev_version}%{?dist}
Summary:	NFS-Ganesha is a NFS Server running in user space
Group:		Applications/System
License:	LGPLv3+
Url:		https://github.com/nfs-ganesha/nfs-ganesha/wiki

Source:		%{sourcename}.tar.gz

BuildRequires:	cmake
BuildRequires:	bison flex
BuildRequires:	flex
BuildRequires:	pkgconfig
BuildRequires:	krb5-devel
%if ( 0%{?suse_version} )
BuildRequires:	dbus-1-devel
Requires:	dbus-1
%else
BuildRequires:	dbus-devel
Requires:	dbus
%endif

%if ( 0%{?suse_version} )
BuildRequires:	systemd-rpm-macros
%endif

BuildRequires:	libcap-devel
BuildRequires:	libblkid-devel
BuildRequires:	libuuid-devel
%if %{with system_ntirpc}
BuildRequires: libntirpc-devel >= @NTIRPC_VERSION@
%endif
Requires:	nfs-utils
%if ( 0%{?fedora} ) || ( 0%{?rhel} && 0%{?rhel} >= 6 ) || ( 0%{?suse_version} )
Requires:	rpcbind
%else
Requires:	portmap
%endif
%if %{with_nfsidmap}
%if ( 0%{?suse_version} )
BuildRequires:	nfsidmap-devel
%else
BuildRequires:	libnfsidmap-devel
%endif
%else
BuildRequires:	nfs-utils-lib-devel
%endif
%if %{with rdma}
BuildRequires:	libmooshika-devel >= 0.6-0
%endif
%if %{with jemalloc}
BuildRequires:	jemalloc-devel
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
Requires: nfs-ganesha = %{version}-%{release}

%description vfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support VFS based filesystems

%package proxy
Summary: The NFS-GANESHA's PROXY FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha = %{version}-%{release}

%description proxy
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PROXY based filesystems

%if %{with utils}
%package utils
Summary: The NFS-GANESHA's util scripts
Group: Applications/System
%if ( 0%{?suse_version} )
Requires:	dbus-1-python, python-gobject2
%else
Requires:	dbus-python, pygobject2
%endif
%if %{with gui_utils}
%if ( 0%{?suse_version} )
BuildRequires:	python-qt4-devel
Requires:	python-qt4
%else
BuildRequires:	PyQt4-devel
Requires:	PyQt4
%endif
%endif
BuildRequires:  python-devel
Requires: nfs-ganesha = %{version}-%{release}, python

%description utils
This package contains utility scripts for managing the NFS-GANESHA server
%endif

%if %{with lttng}
%package lttng
Summary: The NFS-GANESHA's library for use with LTTng
Group: Applications/System
BuildRequires: lttng-ust-devel >= 2.3
Requires: nfs-ganesha = %{version}-%{release}, lttng-tools >= 2.3,  lttng-ust >= 2.3

%description lttng
This package contains the libganesha_trace.so library. When preloaded
to the ganesha.nfsd server, it makes it possible to trace using LTTng.
%endif

# Option packages start here. use "rpmbuild --with gpfs" (or equivalent)
# for activating this part of the spec file

# NULL
%if %{with nullfs}
%package nullfs
Summary: The NFS-GANESHA's NULLFS Stackable FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description nullfs
This package contains a Stackable FSAL shared object to
be used with NFS-Ganesha. This is mostly a template for future (more sophisticated) stackable FSALs
%endif

# GPFS
%if %{with gpfs}
%package gpfs
Summary: The NFS-GANESHA's GPFS FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description gpfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support GPFS backend
%endif

# ZFS
%if %{with zfs}
%package zfs
Summary: The NFS-GANESHA's ZFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libzfswrap-devel

%description zfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support ZFS
%endif

# CEPH
%if %{with ceph}
%package ceph
Summary: The NFS-GANESHA's CephFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libcephfs1-devel >= 10.2.0

%description ceph
This package contains a FSAL shared object to
be used with NFS-Ganesha to support CephFS
%endif

# RGW
%if %{with rgw}
%package rgw
Summary: The NFS-GANESHA's Ceph RGW FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	librgw2-devel >= 10.2.0

%description rgw
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Ceph RGW
%endif

# XFS
%if %{with xfs}
%package xfs
Summary: The NFS-GANESHA's XFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libattr-devel xfsprogs-devel

%description xfs
This package contains a shared object to be used with FSAL_VFS
to support XFS correctly
%endif

# PANFS
%if %{with panfs}
%package panfs
Summary: The NFS-GANESHA's PANFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}

%description panfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PANFS
%endif

# GLUSTER
%if %{with gluster}
%package gluster
Summary: The NFS-GANESHA's GLUSTER FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:        glusterfs-api-devel >= 3.8
BuildRequires:        libattr-devel, libacl-devel

%description gluster
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Gluster
%endif

%prep
%setup -q -n %{sourcename}

%build
cmake .	-DCMAKE_BUILD_TYPE=Debug			\
	-DBUILD_CONFIG=rpmbuild				\
	-DUSE_FSAL_NULL=%{use_fsal_null}		\
	-DUSE_FSAL_ZFS=%{use_fsal_zfs}			\
	-DUSE_FSAL_XFS=%{use_fsal_xfs}			\
	-DUSE_FSAL_CEPH=%{use_fsal_ceph}		\
	-DUSE_FSAL_RGW=%{use_fsal_rgw}			\
	-DUSE_FSAL_GPFS=%{use_fsal_gpfs}		\
	-DUSE_FSAL_PANFS=%{use_fsal_panfs}		\
	-DUSE_FSAL_GLUSTER=%{use_fsal_gluster}		\
	-DUSE_SYSTEM_NTIRPC=%{use_system_ntirpc}	\
	-DUSE_9P_RDMA=%{use_rdma}			\
	-DUSE_LTTNG=%{use_lttng}			\
	-DUSE_ADMIN_TOOLS=%{use_utils}			\
	-DUSE_GUI_ADMIN_TOOLS=%{use_gui_utils}		\
	-DUSE_FSAL_VFS=ON				\
	-DUSE_FSAL_PROXY=ON				\
	-DUSE_DBUS=ON					\
	-DUSE_9P=ON					\
	-DDISTNAME_HAS_GIT_DATA=OFF			\
%if %{with jemalloc}
	-DALLOCATOR=jemalloc
%endif

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
mkdir -p %{buildroot}%{_libexecdir}/ganesha
install -m 644 config_samples/logrotate_ganesha	%{buildroot}%{_sysconfdir}/logrotate.d/ganesha
install -m 644 scripts/ganeshactl/org.ganesha.nfsd.conf	%{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 755 scripts/nfs-ganesha-config.sh %{buildroot}%{_libexecdir}/ganesha
install -m 755 tools/mount.9P	%{buildroot}%{_sbindir}/mount.9P

install -m 644 config_samples/vfs.conf %{buildroot}%{_sysconfdir}/ganesha

%if %{with_systemd}
mkdir -p %{buildroot}%{_unitdir}

install -m 644 scripts/systemd/nfs-ganesha.service	%{buildroot}%{_unitdir}/nfs-ganesha.service
install -m 644 scripts/systemd/nfs-ganesha-lock.service	%{buildroot}%{_unitdir}/nfs-ganesha-lock.service
install -m 644 scripts/systemd/nfs-ganesha-config.service %{buildroot}%{_unitdir}/nfs-ganesha-config.service
install -m 644 scripts/systemd/sysconfig/nfs-ganesha	%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%else
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 scripts/init.d/nfs-ganesha.el6		%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha
install -m 644 scripts/init.d/sysconfig/ganesha		%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%endif

%if %{with pt}
install -m 644 config_samples/pt.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with xfs}
install -m 644 config_samples/xfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with zfs}
install -m 644 config_samples/zfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with ceph}
install -m 644 config_samples/ceph.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with gluster}
install -m 644 config_samples/logrotate_fsal_gluster %{buildroot}%{_sysconfdir}/logrotate.d/ganesha-gfapi
%endif

%if %{with gpfs}
install -m 755 scripts/gpfs-epoch %{buildroot}%{_libexecdir}/ganesha
install -m 644 config_samples/gpfs.conf	%{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.nfsd.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.main.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.log.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/gpfs.ganesha.exports.conf	%{buildroot}%{_sysconfdir}/ganesha
%if ! %{with_systemd}
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 scripts/init.d/nfs-ganesha.gpfs		%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha-gpfs
%endif
%endif

make DESTDIR=%{buildroot} install

%post
%if ( 0%{?suse_version} )
%service_add_post nfs-ganesha.service nfs-ganesha-lock.service nfs-ganesha-config.service
%else
%if %{with_systemd}
%systemd_post nfs-ganesha.service
%systemd_post nfs-ganesha-lock.service
%systemd_post nfs-ganesha-config.service
%endif
%endif
killall -SIGHUP dbus-daemon 2>&1 > /dev/null

%preun
%if ( 0%{?suse_version} )
%service_del_preun nfs-ganesha-lock.service
%else
%if %{with_systemd}
%systemd_preun nfs-ganesha-lock.service
%endif
%endif

%postun
%if ( 0%{?suse_version} )
%service_del_postun nfs-ganesha-lock.service
%else
%if %{with_systemd}
%systemd_postun_with_restart nfs-ganesha-lock.service
%endif
%endif

%files
%defattr(-,root,root,-)
%{_bindir}/ganesha.nfsd
%if ! %{with system_ntirpc}
%{_libdir}/libntirpc.so.@NTIRPC_VERSION@
%{_libdir}/libntirpc.so.1.4
%{_libdir}/libntirpc.so
%{_libdir}/pkgconfig/libntirpc.pc
%{_includedir}/ntirpc/
%endif
%config %{_sysconfdir}/dbus-1/system.d/org.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/sysconfig/ganesha
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha
%dir %{_sysconfdir}/ganesha/
%config(noreplace) %{_sysconfdir}/ganesha/ganesha.conf
%dir %{_defaultdocdir}/ganesha/
%{_defaultdocdir}/ganesha/*
%dir %{_localstatedir}/run/ganesha
%dir %{_libexecdir}/ganesha
%{_libexecdir}/ganesha/nfs-ganesha-config.sh

%if %{with_systemd}
%{_unitdir}/nfs-ganesha.service
%{_unitdir}/nfs-ganesha-lock.service
%{_unitdir}/nfs-ganesha-config.service
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


%files proxy
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalproxy*

# Optional packages
%if %{with nullfs}
%files nullfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalnull*
%endif

%if %{with gpfs}
%files gpfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalgpfs*
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.nfsd.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.main.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.log.conf
%config(noreplace) %{_sysconfdir}/ganesha/gpfs.ganesha.exports.conf
%{_libexecdir}/ganesha/gpfs-epoch
%if ! %{with_systemd}
%{_sysconfdir}/init.d/nfs-ganesha-gpfs
%endif
%endif

%if %{with zfs}
%files zfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalzfs*
%config(noreplace) %{_sysconfdir}/ganesha/zfs.conf
%endif

%if %{with xfs}
%files xfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalxfs*
%config(noreplace) %{_sysconfdir}/ganesha/xfs.conf
%endif

%if %{with ceph}
%files ceph
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalceph*
%config(noreplace) %{_sysconfdir}/ganesha/ceph.conf
%endif

%if %{with rgw}
%files rgw
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalrgw*
%config(noreplace) %{_sysconfdir}/ganesha/rgw.conf
%endif

%if %{with gluster}
%files gluster
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha-gfapi
%{_libdir}/ganesha/libfsalgluster*
%endif

%if %{with panfs}
%files panfs
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpanfs*
%endif

%if %{with pt}
%files pt
%defattr(-,root,root,-)
%{_libdir}/ganesha/libfsalpt*
%config(noreplace) %{_sysconfdir}/ganesha/pt.conf
%endif

%if %{with lttng}
%files lttng
%defattr(-,root,root,-)
%{_libdir}/ganesha/libganesha_trace*
%endif

%if %{with utils}
%files utils
%defattr(-,root,root,-)
%if ( 0%{?suse_version} || 0%{?fedora} )
%{python_sitelib}/Ganesha/*
%{python_sitelib}/ganeshactl-*-info
%else
%{python2_sitelib}/Ganesha/*
%{python2_sitelib}/ganeshactl-*-info
%endif
%if %{with gui_utils}
%{_bindir}/ganesha-admin
%{_bindir}/manage_clients
%{_bindir}/manage_exports
%{_bindir}/manage_logger
%{_bindir}/ganeshactl
%{_bindir}/client_stats_9pOps
%{_bindir}/export_stats_9pOps
%endif
%{_bindir}/fake_recall
%{_bindir}/get_clientids
%{_bindir}/grace_period
%{_bindir}/purge_gids
%{_bindir}/ganesha_stats
%{_bindir}/sm_notify.ganesha
%{_bindir}/ganesha_mgr
%endif

%changelog
* Tue Apr 21 2015  Philippe DENIEL <philippe.deniel@cea.fr> 2.2
- Ganesha supports granting delegations
- There have been numerous config changes
- Ganesha now includes systemd scripts
- Improved packaging for RPM and Debian
- Major stability improvements
- non-QT based python tools
- Support for Ganesha to be a pNFS DS only, no MDS
- SECINFO in preferred order
- LTTng support
- NFS v4.2 support
- Major improvements in 9p support
- Code cleanup (checkpatch and Coverity)
- ntirpc improvements
- FSAL_GLUSTER updated with pNFS and ACL support and more

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

