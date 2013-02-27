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
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires:	cmake
Url:            ${RPM_URL}


%define prefix /opt/${RPMNAME}-%{version}
%define rpmprefix $RPM_BUILD_ROOT%{prefix}
%define srcdirname %{name}-%{version}-Source

%description
${RPMNAME} : ${RPM_DESCRIPTION}

 ")

# if needed deal with FSAL modules
if(USE_FSAL_CEPH)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"%description ceph
This is FSAL_VFS package. This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport CEPH
")
endif(USE_FSAL_CEPH)

if(USE_FSAL_VFS)
FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"%description vfs
This is FSAL_VFS package. This package contains a FSAL shared object to 
be used with NFS-Ganesha to suppport VFS based filesystems
")
endif(USE_FSAL_VFS)

FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%prep
%setup -q -n %{srcdirname}

%build
cd ..
rm -rf build_tree
mkdir build_tree
cd build_tree
cmake -DCMAKE_INSTALL_PREFIX=%{rpmprefix} ../%{srcdirname}
make
  
%install 
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

" )
endif(USE_FSAL_CEPH)

if(USE_FSAL_VFS)
        FILE(APPEND ${RPM_ROOTDIR}/SPECS/${RPMNAME}.spec  
"
%files vfs
%defattr(-,root,root,-)

" )
endif(USE_FSAL_VFS)

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
	COMMAND ${RPMTools_RPMBUILD_EXECUTABLE} -bs --define=\"_topdir ${RPM_ROOTDIR}\" --buildroot=${RPM_ROOTDIR}/tmp ${RPM_ROOTDIR}/SPECS/${SPECFILE_NAME} 
	)
      
      ADD_CUSTOM_TARGET(${RPMNAME}_rpm
	COMMAND cpack -G TGZ --config CPackSourceConfig.cmake
	COMMAND ${CMAKE_COMMAND} -E copy ${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz ${RPM_ROOTDIR}/SOURCES    
	COMMAND ${RPMTools_RPMBUILD_EXECUTABLE} -bb --define=\"_topdir ${RPM_ROOTDIR}\" --buildroot=${RPM_ROOTDIR}/tmp ${RPM_ROOTDIR}/SPECS/${SPECFILE_NAME} 
	)  
    ENDMACRO(RPMTools_ADD_RPM_TARGETS)

  ELSE (RPMTools_RPMBUILD_FOUND)
    SET(RPMTools FALSE)
  ENDIF (RPMTools_RPMBUILD_FOUND)  
  
  
