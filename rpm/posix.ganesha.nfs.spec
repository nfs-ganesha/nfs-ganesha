Summary: The GANESHA NFS daemon for POSIX interfaces 
Name: posix.ganesha.nfsd
Version: 0.99.1
Release: 3 
Group: Application/Devel
Packager: SHERPA development team
License: CeCILL 
Source: %{name}-%{version}.tar
BuildRoot: /tmp/%{name}-buildroot
Prefix: %{_prefix}

%define _topdir . 

%description
This package contains the GANESHA NFS daemon for POSIX and the ganeshell test suite


%install
[ $RPM_BUILD_ROOT != "/" ] && rm -fr $RPM_BUILD_ROOT
mkdir -p -m 755 $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../bin/`cea_os -M`/posix.ganesha.nfsd $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../bin/`cea_os -M`/posix.ganeshell $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../share/scripts/ganestat.pl $RPM_BUILD_ROOT/%{_prefix}/bin

%files
%{_bindir}/*

%clean 
[ $RPM_BUILD_ROOT != "/" ] && rm -fr $RPM_BUILD_ROOT

%post 

%postun


%changelog


