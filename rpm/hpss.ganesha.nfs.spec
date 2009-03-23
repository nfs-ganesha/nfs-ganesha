Summary: The GANESHA NFS daemon for HPSS 
Name: hpss.ganesha.nfsd
Version: 0.99.4
Release: 1 
Group: Application/Devel
Packager: SHERPA development team
License: CeCILL 
Source: %{name}-%{version}.tar
BuildRoot: /tmp/%{name}-buildroot
Prefix: %{_prefix}

%define _topdir . 

%description
This package contains the GANESHA NFS daemon for HPSS and the ganeshell test suite


%install
[ $RPM_BUILD_ROOT != "/" ] && rm -fr $RPM_BUILD_ROOT
mkdir -p -m 755 $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../bin/`cea_os -M`/hpss.ganesha.nfsd $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../bin/`cea_os -M`/hpss.ganeshell $RPM_BUILD_ROOT/%{_prefix}/bin
install -m 755 ../../share/scripts/ganestat.pl $RPM_BUILD_ROOT/%{_prefix}/bin

%files
%{_bindir}/*

%clean 
[ $RPM_BUILD_ROOT != "/" ] && rm -fr $RPM_BUILD_ROOT

%post 

%postun


%changelog


