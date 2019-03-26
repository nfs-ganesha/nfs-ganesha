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
BuildRequires: distribution-release
%if ( ! 0%{?is_opensuse} )
BuildRequires: sles-release >= 12
Requires: sles-release >= 12
%else
BuildRequires: openSUSE-release
Requires: openSUSE-release
%endif

%global with_systemd 1
%global with_nfsidmap 1
%endif

# Conditionally enable some FSALs, disable others.
#
# 1. rpmbuild accepts these options (gpfs as example):
#    --with gpfs
#    --without gpfs

%define on_off_switch() %%{?with_%1:ON}%%{!?with_%1:OFF}

# A few explanation about % bcond_with and % bcond_without
# /!\ be careful: this syntax can be quite messy
# % bcond_with means you add a "--with" option, default = without this feature
# % bcond_without adds a"--without" so the feature is enabled by default

@BCOND_NULLFS@ nullfs
%global use_fsal_null %{on_off_switch nullfs}

@BCOND_MEM@ mem
%global use_fsal_mem %{on_off_switch mem}

@BCOND_GPFS@ gpfs
%global use_fsal_gpfs %{on_off_switch gpfs}

@BCOND_XFS@ xfs
%global use_fsal_xfs %{on_off_switch xfs}

@BCOND_LUSTRE@ lustre
%global use_fsal_lustre %{on_off_switch lustre}

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

@BCOND_9P@ 9P
%global use_9P %{on_off_switch 9P}

@BCOND_JEMALLOC@ jemalloc

@BCOND_LTTNG@ lttng
%global use_lttng %{on_off_switch lttng}

@BCOND_UTILS@ utils
%global use_utils %{on_off_switch utils}

@BCOND_GUI_UTILS@ gui_utils
%global use_gui_utils %{on_off_switch gui_utils}

@BCOND_NTIRPC@ system_ntirpc
%global use_system_ntirpc %{on_off_switch system_ntirpc}

@BCOND_MAN_PAGE@ man_page
%global use_man_page %{on_off_switch man_page}

@BCOND_RADOS_RECOV@ rados_recov
%global use_rados_recov %{on_off_switch rados_recov}

@BCOND_RADOS_URLS@ rados_urls
%global use_rados_urls %{on_off_switch rados_urls}

@BCOND_RPCBIND@ rpcbind
%global use_rpcbind %{on_off_switch rpcbind}

@BCOND_MSPAC_SUPPORT@ mspac_support
%global use_mspac_support %{on_off_switch mspac_support}

@BCOND_SANITIZE_ADDRESS@ sanitize_address
%global use_sanitize_address %{on_off_switch sanitize_address}

%global dev_version %{lua: s = string.gsub('@GANESHA_EXTRA_VERSION@', '^%-', ''); s2 = string.gsub(s, '%-', '.'); print((s2 ~= nil and s2 ~= '') and s2 or "0.1") }

%define sourcename @CPACK_SOURCE_PACKAGE_FILE_NAME@

Name:		nfs-ganesha
Version:	@GANESHA_BASE_VERSION@
Release:	%{dev_version}%{?dist}
Summary:	NFS-Ganesha is a NFS Server running in user space
Group:		Applications/System
License:	LGPLv3+
Url:		https://github.com/nfs-ganesha/nfs-ganesha/wiki

Source:		%{sourcename}.tar.gz

BuildRequires:	cmake
BuildRequires:	bison flex
BuildRequires:	flex
BuildRequires:	pkgconfig
BuildRequires:	userspace-rcu-devel
BuildRequires:	krb5-devel
%if ( 0%{?suse_version} >= 1330 )
BuildRequires:  libnsl-devel
%else
%if ( 0%{?fedora} >= 28 || 0%{?rhel} >= 8 )
BuildRequires:  libnsl2-devel
%endif
%endif
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
%if %{with mspac_support}
BuildRequires:	libwbclient-devel
%endif
BuildRequires:	gcc-c++
%if %{with system_ntirpc}
BuildRequires: libntirpc-devel >= @NTIRPC_MIN_VERSION@
%else
Requires: libntirpc = @NTIRPC_VERSION_EMBED@
%endif
%if %{with sanitize_address}
BuildRequires:	libasan
%endif
Requires:	nfs-utils

%if ( 0%{?with_rpcbind} )
%if ( 0%{?fedora} ) || ( 0%{?rhel} && 0%{?rhel} >= 6 ) || ( 0%{?suse_version} )
Requires:	rpcbind
%else
Requires:	portmap
%endif
%endif

%if %{with_nfsidmap}
%if ( 0%{?suse_version} )
BuildRequires:	nfsidmap-devel
%else
BuildRequires:	libnfsidmap-devel
%endif
%else
BuildRequires: nfs-utils-lib-devel
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
%if %{with man_page}
%if ( 0%{?fedora} >= 28 || 0%{?rhel} >= 8 )
BuildRequires: python3-sphinx
%else
BuildRequires: python-sphinx
%endif
%endif
Requires(post): psmisc
Requires(pre): shadow-utils

# Use CMake variables

%description
nfs-ganesha : NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as
 shared objects to support different file systems and name-spaces.

%if %{with 9P}
%package mount-9P
Summary: a 9p mount helper
Group: Applications/System

%description mount-9P
This package contains the mount.9P script that clients can use
to simplify mounting to NFS-GANESHA. This is a 9p mount helper.
%endif

%package vfs
Summary: The NFS-GANESHA VFS FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha = %{version}-%{release}

%description vfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support VFS based filesystems

%package proxy
Summary: The NFS-GANESHA PROXY FSAL
Group: Applications/System
BuildRequires: libattr-devel
Requires: nfs-ganesha = %{version}-%{release}

%description proxy
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PROXY based filesystems

%if %{with utils}
%package utils
Summary: The NFS-GANESHA util scripts
Group: Applications/System
%if ( 0%{?suse_version} )
Requires:	dbus-1-python, python-gobject2, python-pyparsing
%else
Requires:	dbus-python, pygobject2, pyparsing
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
%if ( 0%{?suse_version} )
BuildRequires:  python-devel
Requires: nfs-ganesha = %{version}-%{release}, python
%else
BuildRequires:  python2-devel
Requires: nfs-ganesha = %{version}-%{release}, python2
%endif

%description utils
This package contains utility scripts for managing the NFS-GANESHA server
%endif

%if %{with lttng}
%package lttng
Summary: The NFS-GANESHA library for use with LTTng
Group: Applications/System
BuildRequires: lttng-ust-devel >= 2.3
BuildRequires: lttng-tools-devel >= 2.3
Requires: nfs-ganesha = %{version}-%{release}, lttng-tools >= 2.3,  lttng-ust >= 2.3

%description lttng
This package contains the libganesha_trace.so library. When preloaded
to the ganesha.nfsd server, it makes it possible to trace using LTTng.
%endif

%if %{with rados_recov}
%package rados-grace
Summary: The NFS-GANESHA command for managing the RADOS grace database
Group: Applications/System
BuildRequires: librados-devel >= 0.61
Requires: nfs-ganesha = %{version}-%{release}

%description rados-grace
This package contains the ganesha-rados-grace tool for interacting with the
database used by the rados_cluster recovery backend.
%endif

# Option packages start here. use "rpmbuild --with gpfs" (or equivalent)
# for activating this part of the spec file

# NULL
%if %{with nullfs}
%package nullfs
Summary: The NFS-GANESHA NULLFS Stackable FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description nullfs
This package contains a Stackable FSAL shared object to
be used with NFS-Ganesha. This is mostly a template for future (more sophisticated) stackable FSALs
%endif

# MEM
%if %{with mem}
%package mem
Summary: The NFS-GANESHA Memory backed testing FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description mem
This package contains a FSAL shared object to be used with NFS-Ganesha.  This
is used for speed and latency testing.
%endif

# GPFS
%if %{with gpfs}
%package gpfs
Summary: The NFS-GANESHA GPFS FSAL
Group: Applications/System
Requires: nfs-ganesha = %{version}-%{release}

%description gpfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support GPFS backend
%endif

# CEPH
%if %{with ceph}
%package ceph
Summary: The NFS-GANESHA CephFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libcephfs-devel >= 10.2.0

%description ceph
This package contains a FSAL shared object to
be used with NFS-Ganesha to support CephFS
%endif

# RGW
%if %{with rgw}
%package rgw
Summary: The NFS-GANESHA Ceph RGW FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	librgw-devel >= 10.2.0

%description rgw
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Ceph RGW
%endif

# XFS
%if %{with xfs}
%package xfs
Summary: The NFS-GANESHA XFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:	libattr-devel xfsprogs-devel

%description xfs
This package contains a shared object to be used with FSAL_VFS
to support XFS correctly
%endif

#LUSTRE
%if %{with lustre}
%package lustre
Summary: The NFS-GANESHA LUSTRE FSAL
Group: Applications/System
BuildRequires: libattr-devel
BuildRequires: lustre-client
Requires: nfs-ganesha = %{version}-%{release}
Requires: lustre-client

%description lustre
This package contains a FSAL shared object to
be used with NFS-Ganesha to support LUSTRE based filesystems
%endif

# PANFS
%if %{with panfs}
%package panfs
Summary: The NFS-GANESHA PANFS FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}

%description panfs
This package contains a FSAL shared object to
be used with NFS-Ganesha to support PANFS
%endif

# GLUSTER
%if %{with gluster}
%package gluster
Summary: The NFS-GANESHA GLUSTER FSAL
Group: Applications/System
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires:        glusterfs-api-devel >= 3.8
BuildRequires:        libattr-devel, libacl-devel

%description gluster
This package contains a FSAL shared object to
be used with NFS-Ganesha to support Gluster
%endif

# SELINUX
%if ( 0%{?fedora} >= 30 || 0%{?rhel} >= 8 )
%package selinux
Summary: The NFS-GANESHA SELINUX targeted policy
Group: Applications/System
BuildArch:	noarch
Requires:	nfs-ganesha = %{version}-%{release}
BuildRequires: selinux-policy-devel
%{?selinux_requires}

%description selinux
This package contains an selinux policy for running ganesha.nfsd

%post selinux
%selinux_modules_install %{_datadir}/selinux/packages/ganesha.pp.bz2

%pre selinux
%selinux_relabel_pre

%postun selinux
if [ $1 -eq 0 ]; then
    %selinux_modules_uninstall ganesha
fi

%posttrans
%selinux_relabel_post
%endif

# NTIRPC (if built-in)
%if ! %{with system_ntirpc}
%package -n libntirpc
Summary:	New Transport Independent RPC Library
Group:		System Environment/Libraries
License:	BSD
Version:	@NTIRPC_VERSION_EMBED@
Url:		https://github.com/nfs-ganesha/ntirpc

# libtirpc has /etc/netconfig, most machines probably have it anyway
# for NFS client
Requires:	libtirpc

%description -n libntirpc
This package contains a new implementation of the original libtirpc,
transport-independent RPC (TI-RPC) library for NFS-Ganesha. It has
the following features not found in libtirpc:
 1. Bi-directional operation
 2. Full-duplex operation on the TCP (vc) transport
 3. Thread-safe operating modes
 3.1 new locking primitives and lock callouts (interface change)
 3.2 stateless send/recv on the TCP transport (interface change)
 4. Flexible server integration support
 5. Event channels (remove static arrays of xprt handles, new EPOLL/KEVENT
    integration)

%package -n libntirpc-devel
Summary:	Development headers for libntirpc
Requires:	libntirpc = @NTIRPC_VERSION_EMBED@
Group:		System Environment/Libraries
License:	BSD
Version:	@NTIRPC_VERSION_EMBED@
Url:		https://github.com/nfs-ganesha/ntirpc

%description -n libntirpc-devel
Development headers and auxiliary files for developing with %{name}.
%endif

%prep
%setup -q -n %{sourcename}

%build
cmake .	-DCMAKE_BUILD_TYPE=Debug			\
	-DBUILD_CONFIG=rpmbuild				\
	-DUSE_FSAL_NULL=%{use_fsal_null}		\
	-DUSE_FSAL_MEM=%{use_fsal_mem}			\
	-DUSE_FSAL_XFS=%{use_fsal_xfs}			\
	-DUSE_FSAL_LUSTRE=%{use_fsal_lustre}			\
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
	-DUSE_RADOS_RECOV=%{use_rados_recov}		\
	-DRADOS_URLS=%{use_rados_urls}			\
	-DUSE_FSAL_VFS=ON				\
	-DUSE_FSAL_PROXY=ON				\
	-DUSE_DBUS=ON					\
	-DUSE_9P=%{use_9P}				\
	-DDISTNAME_HAS_GIT_DATA=OFF			\
	-DUSE_MAN_PAGE=%{use_man_page}                  \
	-DRPCBIND=%{use_rpcbind}			\
	-D_MSPAC_SUPPORT=%{use_mspac_support}		\
	-DSANITIZE_ADDRESS=%{use_sanitize_address}	\
%if %{with jemalloc}
	-DALLOCATOR=jemalloc
%endif

make %{?_smp_mflags} || make %{?_smp_mflags} || make

%if ( 0%{?fedora} >= 30 || 0%{?rhel} >= 8 )
make -C selinux -f /usr/share/selinux/devel/Makefile ganesha.pp
pushd selinux && bzip2 -9 ganesha.pp && popd
%endif

%install
mkdir -p %{buildroot}%{_sysconfdir}/ganesha/
mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
mkdir -p %{buildroot}%{_sysconfdir}/sysconfig
mkdir -p %{buildroot}%{_sysconfdir}/logrotate.d
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sbindir}
mkdir -p %{buildroot}%{_libdir}/ganesha
mkdir -p %{buildroot}%{_localstatedir}/run/ganesha
mkdir -p %{buildroot}%{_localstatedir}/log/ganesha
mkdir -p %{buildroot}%{_libexecdir}/ganesha
install -m 644 config_samples/logrotate_ganesha	%{buildroot}%{_sysconfdir}/logrotate.d/ganesha
install -m 644 scripts/ganeshactl/org.ganesha.nfsd.conf	%{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 755 scripts/nfs-ganesha-config.sh %{buildroot}%{_libexecdir}/ganesha
%if %{with 9P}
install -m 755 tools/mount.9P	%{buildroot}%{_sbindir}/mount.9P
%endif

install -m 644 config_samples/vfs.conf %{buildroot}%{_sysconfdir}/ganesha

%if %{with_systemd}
mkdir -p %{buildroot}%{_unitdir}

install -m 644 scripts/systemd/nfs-ganesha.service.el7	%{buildroot}%{_unitdir}/nfs-ganesha.service
install -m 644 scripts/systemd/nfs-ganesha-lock.service.el7	%{buildroot}%{_unitdir}/nfs-ganesha-lock.service
install -m 644 scripts/systemd/nfs-ganesha-config.service %{buildroot}%{_unitdir}/nfs-ganesha-config.service
install -m 644 scripts/systemd/sysconfig/nfs-ganesha	%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%if 0%{?_tmpfilesdir:1}
mkdir -p %{buildroot}%{_tmpfilesdir}
install -m 644 scripts/systemd/tmpfiles.d/ganesha.conf	%{buildroot}%{_tmpfilesdir}
%endif
%else
mkdir -p %{buildroot}%{_sysconfdir}/init.d
install -m 755 scripts/init.d/nfs-ganesha.el6		%{buildroot}%{_sysconfdir}/init.d/nfs-ganesha
install -m 644 scripts/init.d/sysconfig/ganesha		%{buildroot}%{_sysconfdir}/sysconfig/ganesha
%endif

%if %{with pt}
install -m 644 config_samples/pt.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with lustre}
install -m 644 config_samples/lustre.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with xfs}
install -m 644 config_samples/xfs.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with ceph}
install -m 644 config_samples/ceph.conf %{buildroot}%{_sysconfdir}/ganesha
%endif

%if %{with rgw}
install -m 644 config_samples/rgw.conf %{buildroot}%{_sysconfdir}/ganesha
install -m 644 config_samples/rgw_bucket.conf %{buildroot}%{_sysconfdir}/ganesha
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

%if ( 0%{?fedora} >= 30 || 0%{?rhel} >= 8 )
install -d %{buildroot}%{_selinux_store_path}/packages
install -d -p %{buildroot}%{_selinux_store_path}/devel/include/contrib
install -p -m 644 selinux/ganesha.if %{buildroot}%{_selinux_store_path}/devel/include/contrib
install -m 0644 selinux/ganesha.pp.bz2 %{buildroot}%{_selinux_store_path}/packages
%endif

%post
%if ( 0%{?suse_version} )
%service_add_post nfs-ganesha.service nfs-ganesha-lock.service nfs-ganesha-config.service
%else
%if ( 0%{?fedora} || ( 0%{?rhel} && 0%{?rhel} > 6 ) )
semanage fcontext -a -t ganesha_var_log_t %{_localstatedir}/log/ganesha > /dev/null 2>&1 || :
semanage fcontext -a -t ganesha_var_log_t %{_localstatedir}/log/ganesha/ganesha.log > /dev/null 2>&1 || :
%if %{with gluster}
semanage fcontext -a -t ganesha_var_log_t %{_localstatedir}/log/ganesha/ganesha-gfapi.log > /dev/null 2>&1 || :
%endif
restorecon %{_localstatedir}/log/ganesha
%endif
%if %{with_systemd}
%systemd_post nfs-ganesha.service
%systemd_post nfs-ganesha-lock.service
%systemd_post nfs-ganesha-config.service
%endif
%endif
killall -SIGHUP dbus-daemon >/dev/null 2>&1 || :

%pre
getent group ganesha > /dev/null || groupadd -r ganesha
getent passwd ganesha > /dev/null || useradd -r -g ganesha -d /var/run/ganesha -s /sbin/nologin -c "NFS-Ganesha Daemon" ganesha
exit 0

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
%debug_package
%else
%if %{with_systemd}
%systemd_postun_with_restart nfs-ganesha-lock.service
%endif
%endif

%files
%{_bindir}/ganesha.nfsd
%{_libdir}/libganesha_nfsd.so*
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
%dir %attr(0775,ganesha,root) %{_localstatedir}/log/ganesha

%if %{with_systemd}
%{_unitdir}/nfs-ganesha.service
%{_unitdir}/nfs-ganesha-lock.service
%{_unitdir}/nfs-ganesha-config.service
%if 0%{?_tmpfilesdir:1}
%{_tmpfilesdir}/ganesha.conf
%endif
%else
%{_sysconfdir}/init.d/nfs-ganesha
%endif

%if %{with man_page}
%{_mandir}/*/ganesha-config.8.gz
%{_mandir}/*/ganesha-core-config.8.gz
%{_mandir}/*/ganesha-export-config.8.gz
%{_mandir}/*/ganesha-cache-config.8.gz
%{_mandir}/*/ganesha-log-config.8.gz
%endif


%if %{with rados_recov}
%files rados-grace
%{_bindir}/ganesha-rados-grace
%if %{with man_page}
%{_mandir}/*/ganesha-rados-grace.8.gz
%{_mandir}/*/ganesha-rados-cluster-design.8.gz
%endif
%endif


%if %{with 9P}
%files mount-9P
%{_sbindir}/mount.9P
%if %{with man_page}
%{_mandir}/*/ganesha-9p-config.8.gz
%endif
%endif

%files vfs
%{_libdir}/ganesha/libfsalvfs*
%config(noreplace) %{_sysconfdir}/ganesha/vfs.conf
%if %{with man_page}
%{_mandir}/*/ganesha-vfs-config.8.gz
%endif

%files proxy
%{_libdir}/ganesha/libfsalproxy*
%if %{with man_page}
%{_mandir}/*/ganesha-proxy-config.8.gz
%endif

# Optional packages
%if %{with lustre}
%files lustre
%{_libdir}/ganesha/libfsallustre*
%config(noreplace) %{_sysconfdir}/ganesha/lustre.conf
%if %{with man_page}
%{_mandir}/*/ganesha-lustre-config.8.gz
%endif
%endif

%if %{with nullfs}
%files nullfs
%{_libdir}/ganesha/libfsalnull*
%endif

%if %{with mem}
%files mem
%{_libdir}/ganesha/libfsalmem*
%endif

%if %{with gpfs}
%files gpfs
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
%if %{with man_page}
%{_mandir}/*/ganesha-gpfs-config.8.gz
%endif
%endif

%if %{with xfs}
%files xfs
%{_libdir}/ganesha/libfsalxfs*
%config(noreplace) %{_sysconfdir}/ganesha/xfs.conf
%if %{with man_page}
%{_mandir}/*/ganesha-xfs-config.8.gz
%endif
%endif

%if %{with ceph}
%files ceph
%{_libdir}/ganesha/libfsalceph*
%config(noreplace) %{_sysconfdir}/ganesha/ceph.conf
%if %{with man_page}
%{_mandir}/*/ganesha-ceph-config.8.gz
%endif
%endif

%if %{with rgw}
%files rgw
%{_libdir}/ganesha/libfsalrgw*
%config(noreplace) %{_sysconfdir}/ganesha/rgw.conf
%config(noreplace) %{_sysconfdir}/ganesha/rgw_bucket.conf
%if %{with man_page}
%{_mandir}/*/ganesha-rgw-config.8.gz
%endif
%endif

%if %{with gluster}
%files gluster
%config(noreplace) %{_sysconfdir}/logrotate.d/ganesha-gfapi
%{_libdir}/ganesha/libfsalgluster*
%if %{with man_page}
%{_mandir}/*/ganesha-gluster-config.8.gz
%endif
%endif

%if ( 0%{?fedora} >= 30 || 0%{?rhel} >= 8 )
%files selinux
%attr(0644,root,root) %{_selinux_store_path}/packages/ganesha.pp.bz2
%attr(0644,root,root) %{_selinux_store_path}/devel/include/contrib/ganesha.if
%endif

%if ! %{with system_ntirpc}
%files -n libntirpc
%{_libdir}/libntirpc.so.@NTIRPC_VERSION_EMBED@
%{_libdir}/libntirpc.so.@NTIRPC_ABI_EMBED@
%{_libdir}/libntirpc.so
%{!?_licensedir:%global license %%doc}
%license libntirpc/COPYING
%doc libntirpc/NEWS libntirpc/README
%files -n libntirpc-devel
%{_libdir}/pkgconfig/libntirpc.pc
%dir %{_includedir}/ntirpc
%{_includedir}/ntirpc/*
%endif

%if %{with panfs}
%files panfs
%{_libdir}/ganesha/libfsalpanfs*
%endif

%if %{with pt}
%files pt
%{_libdir}/ganesha/libfsalpt*
%config(noreplace) %{_sysconfdir}/ganesha/pt.conf
%endif

%if %{with lttng}
%files lttng
%{_libdir}/ganesha/libganesha_trace*
%endif

%if %{with utils}
%files utils
%if ( 0%{?suse_version} )
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
%if %{with 9P}
%{_bindir}/client_stats_9pOps
%{_bindir}/export_stats_9pOps
%endif
%endif
%{_bindir}/fake_recall
%{_bindir}/get_clientids
%{_bindir}/grace_period
%{_bindir}/ganesha_stats
%{_bindir}/sm_notify.ganesha
%{_bindir}/ganesha_mgr
%{_bindir}/ganesha_conf
%{_mandir}/*/ganesha_conf.8.gz
%endif

%changelog

