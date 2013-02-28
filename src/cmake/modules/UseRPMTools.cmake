#
# - Find tools needed for building RPM Packages
#   on Linux systems and defines macro that helps to
#   build source or binary RPM, the MACRO assumes
#   CMake 2.4.x which includes CPack support.
#   CPack is used to build tar.gz source tarball
#   which may be used by a custom user-made spec file.
#
# - Define RPMTools_ADD_RPM_TARGETS which defines
#   two (top-level) CUSTOM targets for building
#   source and binary RPMs
#
# Those CMake macros are provided by the TSP Developer Team
# https://savannah.nongnu.org/projects/tsp
#

# Look for RPM builder executable
FIND_PROGRAM(RPMTools_RPMBUILD_EXECUTABLE 
    NAMES rpmbuild
    PATHS "/usr/bin;/usr/lib/rpm"
    PATH_SUFFIXES bin
    DOC "The RPM builder tool")
  
IF (RPMTools_RPMBUILD_EXECUTABLE)
    MESSAGE(STATUS "Looking for RPMTools... - found rpmuild is ${RPMTools_RPMBUILD_EXECUTABLE}")
    SET(RPMTools_RPMBUILD_FOUND "YES")
    GET_FILENAME_COMPONENT(RPMTools_BINARY_DIRS ${RPMTools_RPMBUILD_EXECUTABLE} PATH)
ELSE (RPMTools_RPMBUILD_EXECUTABLE) 
    SET(RPMTools_RPMBUILD_FOUND "NO")
    MESSAGE(STATUS "Looking for RPMTools... - rpmbuild NOT FOUND")
ENDIF (RPMTools_RPMBUILD_EXECUTABLE) 
  
# Detect if CPack was included or not
IF (NOT DEFINED "CPACK_PACKAGE_NAME") 
    MESSAGE(FATAL_ERROR "CPack was not included, you should include CPack before Using RPMTools")
ENDIF (NOT DEFINED "CPACK_PACKAGE_NAME")
  
IF (RPMTools_RPMBUILD_FOUND)
    SET(RPMTools_FOUND TRUE)    
    #
    # - first arg  (ARGV0) is RPM name
    #
    MACRO(RPMTools_ADD_RPM_TARGETS RPMNAME)

      #
      # If no spec file is provided create a minimal one
      #
      IF ("${ARGV1}" STREQUAL "")
	SET(SPECFILE_PATH "${CMAKE_BINARY_DIR}/${RPMNAME}.spec")
      ELSE ("${ARGV1}" STREQUAL "")
	SET(SPECFILE_PATH "${ARGV1}")
      ENDIF("${ARGV1}" STREQUAL "")
      
      # Prepare RPM build tree
      SET(RPM_ROOTDIR ${CMAKE_BINARY_DIR}/RPM)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR})
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/tmp)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/BUILD)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/RPMS)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/SOURCES)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/SPECS)
      FILE(MAKE_DIRECTORY ${RPM_ROOTDIR}/SRPMS)

      # Read RPM changelog and store the result into a variable
      EXEC_PROGRAM(cat ARGS ${RPM_CHANGELOG_FILE} OUTPUT_VARIABLE RPM_CHANGELOG_FILE_CONTENT)

      SET(SPECFILE_PATH "${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec")
      SET(SPECFILE_NAME "${RPMNAME}.spec")
      FILE(WRITE ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec
	  "# -*- rpm-spec -*-
Summary:        ${RPM_SUMMARY}
Name:           ${RPMNAME}
Version:        ${PACKAGE_VERSION}
Release:        ${RPM_RELEASE}
License:        ${RPM_PACKAGE_LICENSE}
Group:          ${RPM_PACKAGE_GROUP}
Source:         ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz
BuildRequires:	cmake
Url:            ${RPM_URL}
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)


%define srcdirname %{name}-%{version}-Source

%description
${RPMNAME} : ${RPM_DESCRIPTION}

 ")

# if needed deal with FSAL modules
if(USE_FSAL_CEPH)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package ceph
Summary: The NFS-GANESHA's CEPH FSAL
Group: Applications/System

%description ceph
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport CEPH
")
endif(USE_FSAL_CEPH)

if(USE_FSAL_LUSTRE)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package lustre
Summary: The NFS-GANESHA's LUSTRE FSAL
Group: Applications/System
BuildRequires: libattr-devel lustre-client

%description lustre
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport LUSTRE
")
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_POSIX)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package posix
Summary: The NFS-GANESHA's LUSTRE FSAL
Group: Applications/System
BuildRequires: libattr-devel

%description posix
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport POSIX
")
endif(USE_FSAL_POSIX)

if(USE_FSAL_SHOOK)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package shook
Summary: The NFS-GANESHA's LUSTRE/SHOOK FSAL
Group: Applications/System
BuildRequires: libattr-devel lustre-client shook-devel

%description shook
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport LUSTRE
")
endif(USE_FSAL_SHOOK)

if(USE_FSAL_VFS)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package vfs
Summary: The NFS-GANESHA's VFS FSAL
Group: Applications/System
BuildRequires: libattr-devel


%description vfs
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport VFS based filesystems
")
endif(USE_FSAL_VFS)

if(USE_FSAL_PROXY)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package proxy
Summary: The NFS-GANESHA's VFS FSAL
Group: Applications/System
BuildRequires: libattr-devel


%description proxy
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport PROXY based filesystems
")
endif(USE_FSAL_PROXY)

if(USE_FSAL_HPSS)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package hpss
Summary: The NFS-GANESHA's HPSS FSAL
Group: Applications/System

%description hpss
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport HPSS 
")
endif(USE_FSAL_ZFS)

if(USE_FSAL_ZFS)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  "
%package zfs
Summary: The NFS-GANESHA's ZFS FSAL
Group: Applications/System
BuildRequires: libzfswrap-devel

%description zfs
This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport ZFS 
")
endif(USE_FSAL_ZFS)

FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%prep
%setup -q -n %{srcdirname}

%build
cd ..
rm -rf build_tree
mkdir build_tree
cd build_tree
cmake -DCMAKE_INSTALL_PREFIX=$RPM_BUILD_ROOT/usr -DCMAKE_BUILD_TYPE=Debug -DBUILD_CONFIG=rpmbuild -DUSE_FSAL_GPFS=OFF -DUSE_FSAL_XFS=OFF ../%{srcdirname}
make
  
%install 
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/ganesha/
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/logrotate.d
mkdir -p $RPM_BUILD_ROOT%{_bindir}
mkdir -p $RPM_BUILD_ROOT%{_libdir}

cd ../build_tree
make install

%clean
rm -rf %{srcdirname}
rm -rf build_tree

%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_sysconfdir}/*
"
)
# if needed deal with FSALs
if(USE_FSAL_CEPH)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files ceph
%defattr(-,root,root,-)
%{_libdir}/libfsalceph*

" )
endif(USE_FSAL_CEPH)

if(USE_FSAL_LUSTRE)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files lustre
%defattr(-,root,root,-)
%{_libdir}/libfsallustre*

" )
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_POSIX)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files posix
%defattr(-,root,root,-)
%{_prefix}/lib/libfsalposix*

" )
endif(USE_FSAL_POSIX)

if(USE_FSAL_SHOOK)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files shook
%defattr(-,root,root,-)
%{_libdir}/libfsalshook*

" )
endif(USE_FSAL_SHOOK)


if(USE_FSAL_VFS)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files vfs
%defattr(-,root,root,-)
%{_prefix}/lib/libfsalvfs*

" )
endif(USE_FSAL_VFS)

if(USE_FSAL_HPSS)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files hpss
%defattr(-,root,root,-)
%{_prefix}/lib/libfsalhpss*

" )
endif(USE_FSAL_HPSS)

if(USE_FSAL_PROXY)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files proxy
%defattr(-,root,root,-)
%{_prefix}/lib/libfsalproxy*

" )
endif(USE_FSAL_PROXY)

if(USE_FSAL_ZFS)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files zfs
%defattr(-,root,root,-)
%{_prefix}/lib/libfsalzfs*

" )
endif(USE_FSAL_ZFS)

# Append changelog
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%changelog
"
)

FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec ${RPM_CHANGELOG_FILE_CONTENT} )
      ADD_CUSTOM_TARGET(${RPMNAME}_srpm
	COMMAND cpack -G TGZ --config CPackSourceConfig.cmake
	COMMAND ${CMAKE_COMMAND} -E copy ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz ${RPM_ROOTDIR}/SOURCES    
	COMMAND ${RPMTools_RPMBUILD_EXECUTABLE} --verbose -bs --define=\"_topdir ${RPM_ROOTDIR}\" ${RPM_ROOTDIR}/SPECS/${SPECFILE_NAME} 
	)
      
      ADD_CUSTOM_TARGET(${RPMNAME}_rpm
	COMMAND cpack -G TGZ --config CPackSourceConfig.cmake
	COMMAND ${CMAKE_COMMAND} -E copy ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz ${RPM_ROOTDIR}/SOURCES    
	COMMAND ${RPMTools_RPMBUILD_EXECUTABLE} --verbose -bb --define=\"_topdir ${RPM_ROOTDIR}\" ${RPM_ROOTDIR}/SPECS/${SPECFILE_NAME} 
	)  
    ENDMACRO(RPMTools_ADD_RPM_TARGETS)

  ELSE (RPMTools_RPMBUILD_FOUND)
    SET(RPMTools FALSE)
  ENDIF (RPMTools_RPMBUILD_FOUND)  
  
  
