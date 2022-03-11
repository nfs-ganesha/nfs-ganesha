# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
# NFS Ganesha  Cmake

# Current version as of Fedora 16.  Not tested with earlier.

cmake_minimum_required(VERSION 2.6.3)

message( STATUS "cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" )

if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  if(COMMAND cmake_policy)
      cmake_policy(SET CMP0017 NEW)
  endif(COMMAND cmake_policy)
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )

# Set GANESHA_TOP_CMAKE_DIR to this CMakeLists.txt path
set(GANESHA_TOP_CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${GANESHA_TOP_CMAKE_DIR}/cmake/modules/")

project(nfs-ganesha C CXX)
# Project versioning
set(GANESHA_MAJOR_VERSION 4)
# Minor version is blank for development.  On release, it becomes ".0".  On a
# stable maintenance branch, this becomes ".N" where N is monotonically
# increasing starting at 1. Remember to include the "." !!
set(GANESHA_MINOR_VERSION .0)

IF(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
set(GANESHA_BUILD_RELEASE 1)
ELSE()
set(GANESHA_BUILD_RELEASE 0)
ENDIF(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")

# needs to come after project()
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR CMAKE_INSTALL_PREFIX STREQUAL "/usr")
	SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix for common files" FORCE)
	message(STATUS "override default CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
	SET(SYSCONFDIR "/etc" CACHE PATH "Install prefix for common files")
	SET(SYSSTATEDIR "/var" CACHE PATH "Install prefix for common files")
ELSE()
	message(STATUS "was set CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
	SET(SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc" CACHE PATH "Install prefix for common files")
	SET(SYSSTATEDIR "${CMAKE_INSTALL_PREFIX}/var" CACHE PATH "Install prefix for common files")
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR CMAKE_INSTALL_PREFIX STREQUAL "/usr")

SET(RUNTIMEDIR "${SYSSTATEDIR}/run/ganesha" CACHE PATH "Runtime directory (for things like pid file, not touched by install)")

# Extra version is for naming development/RC.  It is blank in stable branches
# so it can be available to end-users to name local variants/versions
# If used, it is always of the form "-whateveryouwant"
set(GANESHA_EXTRA_VERSION .3)

set(GANESHA_VERSION ${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}${GANESHA_EXTRA_VERSION})
set(GANESHA_BASE_VERSION ${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION})

set(VERSION_COMMENT
  "GANESHA file server is 64 bits compliant and supports NFS v3,4.0,4.1 (pNFS) and 9P"
)

# find out which platform we are building on
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(LINUX ON)
  set(UNIX ON)
  # Now detects the Linux's distro
  set(DISTRO "UNKNOWN")
  set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/libexec")

  EXECUTE_PROCESS(
      COMMAND awk -F=  "/^NAME=/ { print $2 }" /etc/os-release
      OUTPUT_VARIABLE SYS_RELEASE
      ERROR_QUIET
   )
  EXECUTE_PROCESS(
      COMMAND awk -F=  "/^ID_LIKE=/ { print $2 }" /etc/os-release
      OUTPUT_VARIABLE ID_LIKE
   )
# Red Hat Enterprise Linux versions before 7.0 will be detected as UNKNOWN

  if( ${SYS_RELEASE} MATCHES "Red Hat" )
     message( STATUS "Detected a Linux Red Hat machine" )
     set(DISTRO "RED_HAT")
  elseif( ${SYS_RELEASE} MATCHES "Fedora" )
     message( STATUS "Detected a Linux Fedora machine" )
     set(DISTRO "FEDORA")
  elseif( ${SYS_RELEASE} MATCHES "SLES" )
     message( STATUS "Detected a Linux SLES machine" )
     set(DISTRO "SLES")
     set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/lib")
  elseif( ${SYS_RELEASE} MATCHES "openSUSE Leap" )
     message( STATUS "Detected a Linux openSUSE Leap machine" )
     set(DISTRO "SLES")
     set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/lib")
  elseif( ${SYS_RELEASE} MATCHES "openSUSE Tumbleweed" )
     message( STATUS "Detected a Linux openSUSE Tumbleweed machine" )
     set(DISTRO "SLES")
     set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/lib")
  elseif( (${SYS_RELEASE} MATCHES "Debian GNU/Linux") OR (${SYS_RELEASE} MATCHES "Ubuntu") )
     message( STATUS "Detected a Linux Debian base machine" )
     set(DISTRO "DEBIAN")
     set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/lib")
  else( ${SYS_RELEASE} MATCHES "Red Hat" )
     if( ${ID_LIKE} MATCHES "suse" )
         message( STATUS "Detected a SUSE Build server machine" )
         set(DISTRO "SLES")
         set(LIBEXECDIR "/usr/lib")
     else( ${ID_LIKE} MATCHES "suse" )
         message( STATUS "Detected an UNKNOWN Linux machine" )
         set(DISTRO "UNKNOWN")
     endif( ${ID_LIKE} MATCHES "suse" )
  endif( ${SYS_RELEASE} MATCHES "Red Hat" )
endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(BSDBASED ON)
  set(FREEBSD ON)
  set(UNIX ON)
# On FreeBSD libc doesn't directly provide libexecinfo, so we have to find it
  set(USE_EXECINFO ON)
  set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/libexec")
endif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(BSDBASED ON)
  set(DARWIN ON)
  set(UNIX ON)
  set(LIBEXECDIR "/${CMAKE_INSTALL_PREFIX}/libexec")
endif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(WINDOWS ON)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

if(DARWIN)
  # Apple's /usr/bin/ld doesn't use the same flags for specifying treatment of
  # undefined symbols, and its defaults are opposite of GNU's and LLVM's ld.
  set(LDFLAG_ALLOW_UNDEF "-Wl,-undefined,suppress")
  set(LDFLAG_DISALLOW_UNDEF "-Wl,-undefined,error")
elseif(FREEBSD OR LINUX)
  # llvm-ld seems to be missing a flag to explicitly allow undefined, but this
  # is the default anyway for both llvm-ld and GNU ld.
  set(LDFLAG_ALLOW_UNDEF "")
  set(LDFLAG_DISALLOW_UNDEF "-Wl,--no-undefined")
endif(DARWIN)

# Identify the host we are building on
EXECUTE_PROCESS( COMMAND hostname
  OUTPUT_VARIABLE BUILD_HOST_NAME
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )

find_package(Toolchain REQUIRED)
find_package(Sanitizers)

# Add maintainer mode for (mainly) strict builds
include(${GANESHA_TOP_CMAKE_DIR}/cmake/maintainer_mode.cmake)

# For libraries that provide pkg-config files
include(FindPkgConfig)

# For our option system
include(${GANESHA_TOP_CMAKE_DIR}/cmake/goption.cmake)

# If we are in a git tree, then this CMakeLists.txt is in "src/" and go .git is in "src/.."
IF( EXISTS ${GANESHA_TOP_CMAKE_DIR}/../.git/HEAD  )
  message( STATUS "Compilation from within a git repository. Using git rev-parse HEAD")
  EXECUTE_PROCESS( COMMAND git rev-parse HEAD
		   WORKING_DIRECTORY ${GANESHA_TOP_CMAKE_DIR}
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
                   OUTPUT_VARIABLE _GIT_HEAD_COMMIT)
  EXECUTE_PROCESS( COMMAND git describe --long
                   WORKING_DIRECTORY  ${GANESHA_TOP_CMAKE_DIR}
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
                   OUTPUT_VARIABLE _GIT_DESCRIBE)

ELSE( EXISTS ${GANESHA_TOP_CMAKE_DIR}/../.git/HEAD  )
  message( STATUS "Outside a git repository, use saved data" )
  EXEC_PROGRAM(${GANESHA_TOP_CMAKE_DIR}/cmake/githead_from_path.sh ARGS ${GANESHA_TOP_CMAKE_DIR}
                OUTPUT_VARIABLE  _GIT_HEAD_COMMIT)

  EXEC_PROGRAM(${GANESHA_TOP_CMAKE_DIR}/cmake/gitdesc_from_path.sh ARGS ${GANESHA_TOP_CMAKE_DIR}
                OUTPUT_VARIABLE  _GIT_DESCRIBE)

ENDIF( EXISTS ${GANESHA_TOP_CMAKE_DIR}/../.git/HEAD  )

STRING(SUBSTRING ${_GIT_HEAD_COMMIT} 0  7 _GIT_HEAD_COMMIT_ABBREV )

if (BSDBASED)
#default gcc doesn't like using -Wuninitialized without -O on FreeBSD
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -ggdb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-optimize-sibling-calls")
    find_library(LIBDL c)  # libc suffices on FreeBSD and Darwin
endif(BSDBASED)

if (FREEBSD)
    set(PLATFORM "FREEBSD")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-dynamic")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/freebsd")
endif(FREEBSD)

if (DARWIN)
    set(PLATFORM "DARWIN")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-export_dynamic")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/darwin")
endif(DARWIN)

if (LINUX)
    set(PLATFORM "LINUX")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/linux")
    find_library(LIBDL dl)  # module loader
endif(LINUX)

if (MSVC)
   add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

# Library path name
get_property(USE_LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
if (USE_LIB64)
	set(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib64 CACHE PATH
		"Specify name of libdir inside install path")
else (USE_LIB64)
	set(LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib CACHE PATH
		"Specify name of libdir inside install path")
endif (USE_LIB64)

IF(FSAL_DESTINATION)
    set( FSAL_DESTINATION ${FSAL_DESTINATION} )
ELSE()
    set( FSAL_DESTINATION "${LIB_INSTALL_DIR}/ganesha")
ENDIF()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
  # uname -p is broken on this system.  Try uname -m
  EXECUTE_PROCESS( COMMAND uname -m
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
		   OUTPUT_VARIABLE ARCH)
else (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")
  set(ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif (CMAKE_SYSTEM_PROCESSOR MATCHES "unknown")

if (ARCH MATCHES "x86_64")
  # Nothing special to do for x86_64
elseif (ARCH MATCHES "i386")
  # Nothing special to do for i386
elseif (ARCH MATCHES "mips")
  set(SYSTEM_LIBRARIES "-latomic" ${SYSTEM_LIBRARIES})
else()
  message(WARNING "Unhandled architecture ${ARCH}")
endif ()

# FSAL selection
# FSALs which are enabled by default but could be disabled
# during the build
goption(USE_FSAL_PROXY_V4 "build PROXY_V4 FSAL shared library" ON)
goption(USE_FSAL_PROXY_V3 "build PROXY_V3 FSAL shared library" ON)
goption(USE_FSAL_VFS "build VFS FSAL shared library" ON)
goption(USE_FSAL_LUSTRE "build LUSTRE FSAL shared library" ON)
goption(USE_FSAL_LIZARDFS "build LIZARDFS FSAL shared library" ON)
goption(USE_FSAL_KVSFS "build KVSFS FSAL shared library" ON)
goption(USE_FSAL_CEPH "build CEPH FSAL shared library" ON)
goption(USE_FSAL_GPFS "build GPFS FSAL" ON)
goption(USE_FSAL_XFS "build XFS support in VFS FSAL" ON)
goption(USE_FSAL_GLUSTER "build GLUSTER FSAL shared library" ON)
goption(USE_FSAL_NULL "build NULL FSAL shared library" ON)
goption(USE_FSAL_RGW "build RGW FSAL shared library" ON)
goption(USE_FSAL_MEM "build Memory FSAL shared library" ON)

# Internal capability required by some FSALs
option(GSH_CAN_HOST_LOCAL_FS "Ganesha supports hosting local filesystems" ON)

# nTIRPC
option(USE_SYSTEM_NTIRPC "Use the system nTIRPC, rather than the submodule" OFF)
option (USE_GSS "enable RPCSEC_GSS support" ON)
option(TIRPC_EPOLL "platform supports EPOLL or emulation" ON)

# Build configure options
goption(USE_DBUS "enable DBUS protocol support" ON)

# Various DBUS enabled features
option(USE_CB_SIMULATOR "enable callback simulator thread" OFF)

goption(USE_NFSIDMAP "Use of libnfsidmap for name resolution" ON)
option(ENABLE_ERROR_INJECTION "enable error injection" OFF)
option(ENABLE_VFS_DEBUG_ACL "Enable debug ACL store for VFS" OFF)
option(ENABLE_RFC_ACL "Use all RFC ACL checks" OFF)
option(USE_TOOL_MULTILOCK "build multilock tool" OFF)

# Electric Fence (-lefence) link flag
goption(USE_EFENCE "link with efence memory debug library" OFF)

# These are -D_FOO options, why ???  should be flags??
option(_NO_TCP_REGISTER "disable registration of tcp services on portmapper" OFF)
option(RPCBIND "enable registration with rpcbind" ON)
option(DEBUG_SAL "enable debugging of SAL by keeping list of all locks, stateids, and state owners" OFF)
option(_VALGRIND_MEMCHECK "Initialize buffers passed to GPFS ioctl that valgrind doesn't understand" OFF)
option(ENABLE_LOCKTRACE "Enable lock trace" OFF)
goption(PROXYV4_HANDLE_MAPPING "enable NFSv3 handle mapping for PROXY_V4 FSAL" OFF)
option(DEBUG_MDCACHE "Add various asserts to mdcache" OFF)

# Debug symbols (-g) build flag
option(DEBUG_SYMS "include debug symbols to binaries (-g option)" OFF)

# Add coverage information to build tree
option(COVERAGE "add flag to generate coverage data at runtime" OFF)

option(ENFORCE_GCC "enforce gcc as a the C compiler used for the project" OFF)

# Define CPACK component (to deal with sub packages)
set(CPACK_COMPONENTS_ALL daemon fsal headers tools)
set(CPACK_COMPONENT_DAEMON_DISPLAY_NAME "NFS-Ganesha daemon")
if (USE_SYSTEM_NTIRPC)
  # Don't include libntirpc in the tarball
  set(CPACK_SOURCE_IGNORE_FILES "libntirpc")
else(USE_SYSTEM_NTIRPC)
  # Don't include libntirpc's spec file; this can confuse rpmbuild
  set(CPACK_SOURCE_IGNORE_FILES "libntirpc.spec$")
endif(USE_SYSTEM_NTIRPC)

# Include custom config and cpack module
include(${GANESHA_TOP_CMAKE_DIR}/cmake/cpack_config.cmake)
include(CPack)

# MSPAC support -lwbclient link flag
goption(_MSPAC_SUPPORT "enable mspac Winbind support" ON)

# CUnit
goption(USE_CUNIT "Use Cunit test framework" OFF)

# GTest
goption(USE_GTEST "Use Google Test test framework" OFF)
gopt_test(USE_GTEST)
if(USE_GTEST)
  find_package(LTTng ${USE_GTEST_REQUIRED})
  find_package(Gperftools ${USE_GTEST_REQUIRED})
  find_package(GTest ${USE_GTEST_REQUIRED})
  find_package(Boost 1.34.0 ${USE_GTEST_REQUIRED} COMPONENTS filesystem program_options)
  if((NOT LTTNG_FOUND) OR (NOT GPERFTOOLS_FOUND) OR (NOT GTEST_FOUND))
    message(WARNING "Couldn't find GTest dependencies. Disabling USE_GTEST")
    set(USE_GTEST OFF)
  endif((NOT LTTNG_FOUND) OR (NOT GPERFTOOLS_FOUND) OR (NOT GTEST_FOUND))
endif(USE_GTEST)

# NFS RDMA
option(USE_NFS_RDMA "enable NFS/RDMA support" OFF)

if(USE_NFS_RDMA)
    # NFS/RDMA is unsupported and should not be used currently
    message(STATUS "-------------------------------------------------------------------------")
    message(STATUS "The RDMA effort for Ganesha has been long abandoned, the current code may")
    message(STATUS "or may not compile but is most likely not functional and certainly is not")
    message(STATUS "supported by the active development team. If anyone develops an interest")
    message(STATUS "in actively developing and supporting RDMA, we would welcome you.")
    message(STATUS "-------------------------------------------------------------------------")
    message(FATAL_ERROR "Aborting - NFS/RDMA not currently supported.")
endif(USE_NFS_RDMA)

# Enable 9P Support
option(USE_9P "enable 9P support" ON)
option(USE_9P_RDMA "enable 9P_RDMA support" OFF)

# Enable NFSv3 Support
option(USE_NFS3 "enable NFSv3 support" ON)

if(USE_NFS3)
    # Enable NLM Support
    option(USE_NLM "enable NLM support" ON)
    # Enable NFSACL3 protocol extension support
    option(USE_NFSACL3 "enable NFSACLv3 support" ON)
else(USE_NFS3)
    # Disable NLM Support
    set(USE_NLM OFF)
    # Disable NFSACL3 protocol extension support
    set(USE_NFSACL3 OFF)
endif(USE_NFS3)

# Enable RQUOTA support
option(USE_RQUOTA "enable RQUOTA support" ON)

# AF_VSOCK host support (NFS)
option(USE_VSOCK "enable AF_VSOCK listener" OFF)
if(USE_VSOCK)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRPC_VSOCK")
endif(USE_VSOCK)

# This option will trigger "long distro name" aka name that contains git information
option(DISTNAME_HAS_GIT_DATA "Distribution package's name carries git data" OFF )

# Build and package Python admin scripts for managing via DBus
goption(USE_ADMIN_TOOLS "Package Admin scripts" OFF)

# Build and package Python gui admin scripts for managing via DBus
goption(USE_GUI_ADMIN_TOOLS "Package GUI Admin scripts" ON)

# Enable LTTng tracing
goption(USE_LTTNG "Enable LTTng tracing" OFF)

# Build man page.
goption(USE_MAN_PAGE "Build MAN page" OFF)

# Enable Rados KV store for recovery
goption(USE_RADOS_RECOV "Enable Rados KV Recovery" ON)

# Enable RADOS URL config file sections
goption(RADOS_URLS "Enable config file inclusion from RADOS objects" ON)

# Enable CephFS POSIX ACL
goption(CEPHFS_POSIX_ACL "Enable CephFS POSIX ACL" ON)

# Enable NFSv4 and POSIX acls mapping
option(USE_ACL_MAPPING "Build NFSv4 to POSIX ACL mapping" OFF)

#
# End build options
#

# Choose a shortcut build config

IF(BUILD_CONFIG)
  INCLUDE(
  ${GANESHA_TOP_CMAKE_DIR}/cmake/build_configurations/${BUILD_CONFIG}.cmake)
ENDIF()

IF(DEBUG_SYMS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
ENDIF(DEBUG_SYMS)

IF(COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
ENDIF(COVERAGE)

# Set what's needed is GCC is enforced
IF(ENFORCE_GCC)
    set(CMAKE_COMPILER_IS_GNUCXX TRUE)
    set(CMAKE_C_COMPILER gcc)
ENDIF(ENFORCE_GCC)

include(CheckSymbolExists)
check_symbol_exists(__GLIBC__ features.h HAVE_GLIBC)
if(HAVE_GLIBC)
  add_definitions(-D_GNU_SOURCE=1)
endif(HAVE_GLIBC)

IF(USE_FSAL_GLUSTER)
  IF(GLUSTER_PREFIX)
    set(GLUSTER_PREFIX ${GLUSTER_PREFIX} CACHE PATH "Path to Gluster installation")
    LIST(APPEND CMAKE_PREFIX_PATH "${GLUSTER_PREFIX}")
    LIST(APPEND CMAKE_LIBRARY_PATH "${GLUSTER_PREFIX}/lib")
    LIST(APPEND CMAKE_LIBRARY_PATH "${GLUSTER_PREFIX}/local/lib")
    LIST(APPEND CMAKE_LIBRARY_PATH "${GLUSTER_PREFIX}/local/lib64")
    LIST(APPEND CMAKE_REQUIRED_INCLUDES "${GLUSTER_PREFIX}/include")
  ELSE()
    set(GLUSTER_PREFIX "/usr" CACHE PATH "Path to Gluster installation")
  ENDIF()
ENDIF()

IF(KRB5_PREFIX)
  set(KRB5_PREFIX ${KRB5_PREFIX} CACHE PATH "Path to Krb5 installation")
  LIST(APPEND CMAKE_PREFIX_PATH "${KRB5_PREFIX}")
  LIST(APPEND CMAKE_LIBRARY_PATH "${KRB5_PREFIX}/lib")
ENDIF()

if(SAMBA4_PREFIX)
  set(SAMBA4_PREFIX ${SAMBA4_PREFIX} CACHE PATH "Path to Samba4 installation")
  LIST(APPEND CMAKE_PREFIX_PATH "${SAMBA4_PREFIX}")
  LIST(APPEND CMAKE_LIBRARY_PATH "${SAMBA4_PREFIX}/lib")
endif()

IF(MOOSHIKA_PREFIX)
  set(MOOSHIKA_PREFIX ${MOOSHIKA_PREFIX} CACHE PATH "Path to Mooshika installation")
  set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}:${MOOSHIKA_PREFIX}/lib/pkgconfig")
ENDIF()

if(USE_NFS_RDMA OR USE_9P_RDMA)
  find_package(RDMA REQUIRED)
  include_directories(${RDMA_INCLUDE_DIR})
  set(SYSTEM_LIBRARIES ${SYSTEM_LIBRARIES} ${RDMA_LIBRARY})
endif(USE_NFS_RDMA OR USE_9P_RDMA)

if(USE_CB_SIMULATOR AND NOT USE_DBUS)
  message(WARNING "The callback simulator needs DBUS.  Enabling DBUS")
  set(USE_DBUS ON)
endif(USE_CB_SIMULATOR AND NOT USE_DBUS)

if(USE_9P_RDMA AND NOT USE_9P)
  message(WARNING "The support of 9P/RDMA needs 9P protocol support. Enabling 9P")
  set(USE_9P ON)
endif(USE_9P_RDMA AND NOT USE_9P)

IF(ALLOCATOR)
  set(ALLOCATOR ${ALLOCATOR} CACHE STRING "memory allocator: jemalloc|tcmalloc|libc")
ELSE()
  if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
    set(ALLOCATOR "jemalloc" CACHE STRING
      "specify the memory allocator to use: jemalloc|tcmalloc|libc")
  else("${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
    set(ALLOCATOR "libc" CACHE STRING
      "specify the memory allocator to use: jemalloc|tcmalloc|libc")
  endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
ENDIF()

# Find packages and libs we need for building
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckCSourceCompiles)
include(TestBigEndian)

check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(string.h HAVE_STRING_H)

if(HAVE_STRING_H AND HAVE_STRINGS_H)
  # we have all the libraries and include files to use string.h
  set(HAVE_STRNLEN ON)
endif(HAVE_STRING_H AND HAVE_STRINGS_H)

IF(_VALGRIND_MEMCHECK)
  check_include_files(valgrind/memcheck.h HAVE_MEMCHECK_H)
  if(NOT HAVE_MEMCHECK_H)
	  message(FATAL_ERROR "Cannot find valgrind/memcheck.h, install valgrind-devel package to enable _VALGRIND_MEMCHECK")
  ENDIF(NOT HAVE_MEMCHECK_H)
ENDIF(_VALGRIND_MEMCHECK)

TEST_BIG_ENDIAN(BIGENDIAN)
if(NOT ${BIGENDIAN})
  set(LITTLEEND ON)
endif(NOT ${BIGENDIAN})

if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  find_package(Threads REQUIRED)
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )

if (USE_GSS)
  find_package(Krb5 REQUIRED gssapi)
  check_include_files(gssapi.h HAVE_GSSAPI_H)
  if (NOT HAVE_GSSAPI_H)
    # Debian/Ubuntu 12 magic
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I/usr/include/mit-krb5/")
    check_include_files(gssapi.h HAVE_GSSAPI_H)
  endif(NOT HAVE_GSSAPI_H)

  if(KRB5_FOUND AND HAVE_GSSAPI_H)
    set(HAVE_KRB5 ON)
    set(_HAVE_GSSAPI ON)
  else(KRB5_FOUND AND HAVE_GSSAPI_H)
    if (NOT KRB5_FOUND)
      message(FATAL_ERROR "Cannot find kerberos libraries")
    endif(NOT KRB5_FOUND)
    if (NOT HAVE_GSSAPI_H)
      message(FATAL_ERROR "Cannot find GSS libraries")
    endif (NOT HAVE_GSSAPI_H)
  endif(KRB5_FOUND AND HAVE_GSSAPI_H)
endif(USE_GSS)

gopt_test(USE_ADMIN_TOOLS)
if (USE_ADMIN_TOOLS)
  find_package (Python COMPONENTS Interpreter Development)
  if (NOT Python_Interpreter_FOUND)
    message(WARNING "Cannot find python. Disablin admin tools")
    set(USE_ADMIN_TOOLS OFF)
  endif (NOT Python_Interpreter_FOUND)
endif (USE_ADMIN_TOOLS)

gopt_test(USE_GUI_ADMIN_TOOLS)
if (USE_ADMIN_TOOLS)
  if (USE_GUI_ADMIN_TOOLS)
    set(PYQTX PyQt5)
    find_program(PYUIC NAMES pyuic5 DOC "PyQT UI-compiler executable")
    if (NOT PYUIC)
      set(PYQTX PyQt4)
      find_program(PYUIC NAMES pyuic4 DOC "PyQT UI-compiler executable")
    endif (NOT PYUIC)
    if (NOT PYUIC)
      if (USE_GUI_ADMIN_TOOLS_REQUIRED)
	message(FATAL_ERROR "Cannot find PyQt but GUI admin tools requested on command line")
      else (USE_GUI_ADMIN_TOOLS_REQUIRED)
	message(WARNING "Cannot find PyQt4 or PyQt5.  Disabling GUI admin tools")
	set(USE_GUI_ADMIN_TOOLS OFF)
      endif (USE_GUI_ADMIN_TOOLS_REQUIRED)
    endif (NOT PYUIC)
  endif (USE_GUI_ADMIN_TOOLS)
endif (USE_ADMIN_TOOLS)

gopt_test(USE_MAN_PAGE)
if (USE_MAN_PAGE)
  find_program(SPHINX_BUILD sphinx-build)
  if(NOT SPHINX_BUILD)
    find_program(SPHINX_BUILD sphinx-build-3)
  endif(NOT SPHINX_BUILD)
  if(NOT SPHINX_BUILD)
    if (USE_MAN_PAGE_REQUIRED)
      message(FATAL_ERROR "Can't find sphinx-build but man pages requested on command line")
    else (USE_MAN_PAGE_REQUIRED)
      message(WARNING "Can't find sphinx-build. Disabling man pages")
      set(USE_MAN_PAGE OFF)
    endif (USE_MAN_PAGE_REQUIRED)
  endif(NOT SPHINX_BUILD)
endif(USE_MAN_PAGE)

# Validate fsal dependencies

gopt_test(USE_FSAL_PROXY_V4)
if (USE_FSAL_PROXY_V4)
  # PROXY_V4 has no deps of it's own, but it has a dependent option
  # PROXY_V4 handle mapping needs sqlite3
  gopt_test(PROXYV4_HANDLE_MAPPING)
  if(PROXYV4_HANDLE_MAPPING)
    check_include_files(sqlite3.h HAVE_SQLITE3_H)
    check_library_exists(
      sqlite3
      sqlite3_open
      ""
      HAVE_SQLITE3
      )
    if(NOT HAVE_SQLITE3 OR NOT HAVE_SQLITE3_H)
      if(PROXYV4_HANDLE_MAPPING_REQUIRED)
	message(FATAL_ERROR "Cannot find sqlite3.h or the library but proxyv4 handle mapping requested on command line")
      else(PROXYV4_HANDLE_MAPPING_REQUIRED)
	message(WARNING "Cannot find sqlite3.h or the library. Disabling proxyv4 handle mapping")
	set(PROXYV4_HANDLE_MAPPING OFF)
      endif(PROXYV4_HANDLE_MAPPING_REQUIRED)
    endif(NOT HAVE_SQLITE3 OR NOT HAVE_SQLITE3_H)
  endif(PROXYV4_HANDLE_MAPPING)
endif (USE_FSAL_PROXY_V4)

# VFS has an optional dependency for libbtrfsutil
gopt_test(USE_FSAL_VFS)
if(USE_FSAL_VFS)
  find_library(BTRFSUTIL_LIB btrfsutil)
  if (BTRFSUTIL_LIB)
    message(STATUS "Found BTRFS Util library: ${BTRFSUTIL_LIB}")
    set(USE_BTRFSUTIL ON)
    set(SYSTEM_LIBRARIES ${BTRFSUTIL_LIB} ${SYSTEM_LIBRARIES})
  endif (BTRFSUTIL_LIB)
endif(USE_FSAL_VFS)

gopt_test(USE_FSAL_LUSTRE)
if(USE_FSAL_LUSTRE)
    ########### lustre hsm version test ##########
    # Lustre/HSM feature needs Lustre 2.5.0.
    # As some hsm calls were already landed as empty nutshells in 2.4
    # we rely on this new call of 2.5.0: llapi_hsm_state_get_fd().
    ##############################################
    CHECK_LIBRARY_EXISTS(lustreapi llapi_hsm_state_get_fd "" USE_LLAPI)
    if(NOT USE_LLAPI)
      if(USE_FSAL_LUSTRE_REQUIRED)
	message(FATAL_ERROR "Cannot find lustreapi, but requested on command line")
      else(USE_FSAL_LUSTRE_REQUIRED)
	message(WARNING "Cannot find lustreapi. We will only build a dummy lustre fsal.")
      endif(USE_FSAL_LUSTRE_REQUIRED)
    endif(NOT USE_LLAPI)
endif(USE_FSAL_LUSTRE)

gopt_test(USE_FSAL_LIZARDFS)
if(USE_FSAL_LIZARDFS)
  find_library(LIZARDFS_CLIENT_LIB lizardfs-client)
  if (LIZARDFS_CLIENT_LIB)
    message(STATUS "Found LizardFS client library: ${LIZARDFS_CLIENT_LIB}")
  else(LIZARDFS_CLIENT_LIB)
    message(WARNING "Cannot find LizardFS client lib. Disabling lizardfs fsal")
    set(USE_FSAL_LIZARDFS OFF)
  endif(LIZARDFS_CLIENT_LIB)
endif(USE_FSAL_LIZARDFS)

gopt_test(USE_FSAL_KVSFS)
if(USE_FSAL_KVSFS)
  find_library(KVSNS_LIB kvsns)
  if (KVSNS_LIB)
    message(STATUS "Found KVSNS library: ${KVSNS_LIB}")
  else (KVSNS_LIB)
    message(WARNING "Cannot find libkvsns. Disabling KVSNS fsal")
    set(USE_FSAL_KVSFS OFF)
  endif (KVSNS_LIB)
endif(USE_FSAL_KVSFS)

gopt_test(USE_FSAL_CEPH)
if(USE_FSAL_CEPH)
  message(STATUS ${USE_FSAL_CEPH_REQUIRED})
  find_package(CEPHFS ${USE_FSAL_CEPH_REQUIRED})
  if(NOT CEPHFS_FOUND)
    message(WARNING "Cannot find CEPH runtime.  Disabling CEPH fsal build")
    set(USE_FSAL_CEPH OFF)
  endif(NOT CEPHFS_FOUND)
  gopt_test(CEPHFS_POSIX_ACL)
  if(CEPHFS_POSIX_ACL)
    check_include_files("acl/libacl.h" HAVE_ACL_H)
    if(HAVE_ACL_H)
      set(USE_ACL_MAPPING ON)
      set(LibACL_FIND_REQUIRED TRUE)
    else()
      set(CEPHFS_POSIX_ACL OFF)
      message(STATUS "Could not find libacl, disabling CephFS POSIX ACL")
    endif(HAVE_ACL_H)
  endif(CEPHFS_POSIX_ACL)
endif(USE_FSAL_CEPH)

gopt_test(USE_NFSACL3)
  if(USE_NFSACL3)
    check_include_files("acl/libacl.h" HAVE_ACL_H)
    if(HAVE_ACL_H)
      set(USE_ACL_MAPPING ON)
      set(LibACL_FIND_REQUIRED TRUE)
    else()
      set(USE_NFSACL3 OFF)
      message(STATUS "Could not find libacl, disabling USE_NFSACL3")
    endif(HAVE_ACL_H)
  endif(USE_NFSACL3)

gopt_test(USE_FSAL_GPFS)

gopt_test(USE_FSAL_XFS)
if(USE_FSAL_XFS)
  if(EXISTS /lib/libhandle.so)
    check_library_exists(handle "open_by_handle" "/./lib" HAVE_XFS_LIB)
    if(HAVE_XFS_LIB)
      set(PATH_LIBHANDLE "/lib/libhandle.so" CACHE INTERNAL "debian stretch and ubuntu xenial hack")
    endif(HAVE_XFS_LIB)
  else(EXISTS /lib/libhandle.so)
    check_library_exists(handle "open_by_handle" "" HAVE_XFS_LIB)
  endif(EXISTS /lib/libhandle.so)
  check_include_files("xfs/xfs.h" HAVE_XFS_H)
  if((NOT HAVE_XFS_LIB) OR (NOT HAVE_XFS_H))
    if(USE_FSAL_XFS_REQUIRED)
      message(FATAL_ERROR "Cannot find XFS runtime, but requested on command line.")
    else(USE_FSAL_XFS_REQUIRED)
      message(WARNING "Cannot find XFS runtime. Disabling XFS build")
      set(USE_FSAL_XFS OFF)
    endif(USE_FSAL_XFS_REQUIRED)
  endif((NOT HAVE_XFS_LIB) OR (NOT HAVE_XFS_H))
endif(USE_FSAL_XFS)

# VFS, GPFS, LUSTRE, and XFS require GSH_CAN_HOST_LOCAL_FS
if(NOT GSH_CAN_HOST_LOCAL_FS)
  if((USE_FSAL_VFS AND USE_FSAL_VFS_REQUIRED) OR
     (USE_FSAL_GPFS AND USE_FSAL_GPFS_REQUIRED) OR
     (USE_FSAL_LUSTRE AND USE_FSAL_LUSTRE_REQUIRED) OR
     (USE_FSAL_XFS AND USE_FSAL_XFS_REQUIRED))
    message(FATAL_ERROR "USE_FSAL_VFS, _GPFS, _LUSTRE, or _XFS requested but local FS hosting is not enabled")
  endif((USE_FSAL_VFS AND USE_FSAL_VFS_REQUIRED) OR
        (USE_FSAL_GPFS AND USE_FSAL_GPFS_REQUIRED) OR
        (USE_FSAL_LUSTRE AND USE_FSAL_LUSTRE_REQUIRED) OR
        (USE_FSAL_XFS AND USE_FSAL_XFS_REQUIRED))

  set(USE_FSAL_VFS OFF)
  set(USE_FSAL_GPFS OFF)
  set(USE_FSAL_LUSTRE OFF)
  set(USE_FSAL_XFS OFF)
endif(NOT GSH_CAN_HOST_LOCAL_FS)

gopt_test(USE_FSAL_GLUSTER)
if(USE_FSAL_GLUSTER)
  find_package(PkgConfig)
  IF(GLUSTER_PREFIX)
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}:${GLUSTER_PREFIX}/lib/pkgconfig")
  ENDIF(GLUSTER_PREFIX)
  # pkg_check_modules doesn't fatal error on REQUIRED, so handle it ourselves
  pkg_check_modules(GFAPI glusterfs-api>=7.6.6)
  if(NOT GFAPI_FOUND)
    if(USE_FSAL_GLUSTER_REQUIRED)
      message(FATAL_ERROR "Cannot find GLUSTER GFAPI runtime but requested on command line")
    else(USE_FSAL_GLUSTER_REQUIRED)
      message(WARNING "Cannot find GLUSTER GFAPI runtime. Disabling GLUSTER fsal build")
      set(USE_FSAL_GLUSTER OFF)
    endif(USE_FSAL_GLUSTER_REQUIRED)
  else(NOT GFAPI_FOUND)
    message(STATUS "GFAPI_INCLUDE_DIRS=${GFAPI_INCLUDE_DIRS}")
    message(STATUS "GFAPI_LIBRARY_DIRS=${GFAPI_LIBRARY_DIRS}")
    message(STATUS "GFAPI_LIBDIR=${GFAPI_LIBDIR}")
    include_directories(${GFAPI_INCLUDE_DIRS})
    # missing directory not provided by current version of GlusterFS
    include_directories(${GFAPI_PREFIX}/include)
    link_directories (${GFAPI_LIBRARY_DIRS})
    pkg_check_modules(STAT_FETCH_GFAPI glusterfs-api>=7.6)
    if(STAT_FETCH_GFAPI_FOUND)
        set(USE_GLUSTER_STAT_FETCH_API ON)
    else()
        set(USE_GLUSTER_STAT_FETCH_API OFF)
        message(STATUS "turning off stat api's")
    endif(STAT_FETCH_GFAPI_FOUND)
  endif(NOT GFAPI_FOUND)

  if(USE_FSAL_GLUSTER)
    check_include_files("unistd.h;sys/xattr.h" HAVE_SYS_XATTR_H)
    if(NOT HAVE_SYS_XATTR_H)
      if(USE_FSAL_GLUSTER_REQUIRED)
	message(FATAL_ERROR "Can not find sys/xattr.h, but GLUSTER requested on command line")
      else(USE_FSAL_GLUSTER_REQUIRED)
        message(WARNING "Can not find sys/xattr.h, disabling GLUSTER fsal build")
        set(USE_FSAL_GLUSTER OFF)
      endif(USE_FSAL_GLUSTER_REQUIRED)
    endif(NOT HAVE_SYS_XATTR_H)
    check_include_files("acl/libacl.h" HAVE_ACL_H)
    if(HAVE_ACL_H)
	set(USE_POSIX_ACLS ON)
	set(USE_ACL_MAPPING ON)
	set(LibACL_FIND_REQUIRED TRUE)
    else()
	set(USE_POSIX_ACLS OFF)
	set(USE_FSAL_GLUSTER OFF)
	message(STATUS "Could not find libacl, disabling GLUSTER fsal build")
    endif(HAVE_ACL_H)
    check_library_exists(gfapi glfs_xreaddirplus_r ${GFAPI_LIBDIR} HAVE_XREADDIRPLUS)
    if(HAVE_XREADDIRPLUS)
        set(USE_GLUSTER_XREADDIRPLUS ON)
    else()
        set(USE_GLUSTER_XREADDIRPLUS OFF)
        message(STATUS "Could not find glfs_xreaddirplus, switching to glfs_readdir_r")
    endif(HAVE_XREADDIRPLUS)
    check_library_exists(gfapi glfs_fd_set_lkowner ${GFAPI_LIBDIR} HAVE_LKOWNER)
    if(HAVE_LKOWNER)
	set(USE_LKOWNER ON)
    else()
	set(USE_LKOWNER OFF)
	set(USE_FSAL_GLUSTER OFF)
	message(STATUS "lkowner support is needed to enable GLUSTER build")
    endif(HAVE_LKOWNER)
    check_library_exists(gfapi glfs_upcall_register ${GFAPI_LIBDIR} HAVE_REGISTER_UPCALL)
    if(HAVE_REGISTER_UPCALL)
        set(USE_GLUSTER_UPCALL_REGISTER ON)
    else()
        set(USE_GLUSTER_UPCALL_REGISTER OFF)
        message(STATUS "Could not find glfs_upcall_register, switching to glfs_h_poll_upcall")
    endif(HAVE_REGISTER_UPCALL)
    check_library_exists(gfapi glfs_upcall_lease_get_object ${GFAPI_LIBDIR} HAVE_DELEG)
    if(HAVE_DELEG)
        set(USE_GLUSTER_DELEGATION ON)
    else()
        set(USE_GLUSTER_DELEGATION OFF)
        message(STATUS "Could not find glfs_upcall_lease_get_object, switching off delegations")
    endif(HAVE_DELEG)
  endif(USE_FSAL_GLUSTER)
endif(USE_FSAL_GLUSTER)

gopt_test(USE_FSAL_NULL)
# NULL has no dependencies

gopt_test(USE_FSAL_RGW)
if(USE_FSAL_RGW)
  # require RGW w/API version 1.1.x
  find_package(RGW 1.1.7 ${USE_FSAL_RGW_REQUIRED})
  if(NOT RGW_FOUND)
    message(WARNING "Cannot find supported RGW runtime.  Disabling RGW fsal build")
    set(USE_FSAL_RGW OFF)
  endif(NOT RGW_FOUND)
endif(USE_FSAL_RGW)

gopt_test(USE_FSAL_MEM)
# MEM has no dependencies

# sort out which allocator to use
if(${ALLOCATOR} STREQUAL "jemalloc")
  find_package(JeMalloc)
  if(JEMALLOC_FOUND)
    set(SYSTEM_LIBRARIES ${JEMALLOC_LIBRARIES} ${SYSTEM_LIBRARIES})
  else(JEMALLOC_FOUND)
    message(WARNING "jemalloc not found, falling back to libc")
    set(ALLOCATOR "libc")
  endif(JEMALLOC_FOUND)
elseif(${ALLOCATOR} STREQUAL "tcmalloc")
  find_package(TcMalloc)
  if(TCMALLOC_FOUND)
    set(SYSTEM_LIBRARIES ${TCMALLOC_LIBRARIES} ${SYSTEM_LIBRARIES})
  else(TCMALLOC_FOUND)
    message(WARNING "tcmalloc not found, falling back to libc")
    set(ALLOCATOR "libc")
  endif(TCMALLOC_FOUND)
else()
  if(NOT ${ALLOCATOR} STREQUAL "libc")
    message(SEND_ERROR "${ALLOCATOR} is not a valid option. Valid allocators are: jemalloc|tcmalloc|libc")
  endif()
endif()

# Find optional libraries/packages

if (USE_ACL_MAPPING)
  find_package(LibACL)
  if(LIBACL_FOUND)
    set(SYSTEM_LIBRARIES ${LIBACL_LIBRARY} ${SYSTEM_LIBRARIES})
  else(LIBACL_FOUND)
    set(USE_ACL_MAPPING OFF)
    message(STATUS "Could not find libacl, disabling ACL mapping build")
  endif(LIBACL_FOUND)
endif(USE_ACL_MAPPING)

gopt_test(USE_EFENCE)
if(USE_EFENCE)
  find_library(LIBEFENCE efence)
  if(LIBEFENCE_FOUND)
    set(SYSTEM_LIBRARIES ${LIBEFENCE} ${SYSTEM_LIBRARIES})
  else(LIBEFENCE_FOUND)
    if(USE_EFENCE_REQUIRED)
      message(FATAL_ERROR "Cannot find efence libs but requested on command line")
    else(USE_EFENCE_REQUIRED)
      message(WARNING "Cannot find efence libs. Disabling efence support")
    endif(USE_EFENCE_REQUIRED)
    set(USE_EFENCE OFF)
  endif(LIBEFENCE_FOUND)
endif(USE_EFENCE)

gopt_test(USE_DBUS)
if(USE_DBUS)
  find_package(PkgConfig)
  # pkg_check_modules doesn't fatal error on REQUIRED, so handle it ourselves
  pkg_check_modules(DBUS ${USE_DBUS_REQUIRED} dbus-1)
  if(NOT DBUS_FOUND)
    if(USE_DBUS_REQUIRED)
      message(FATAL_ERROR "Cannot find DBUS libs but requested on command line")
    else(USE_DBUS_REQUIRED)
      message(WARNING "Cannot find DBUS libs. Disabling DBUS support")
      set(USE_DBUS OFF)
    endif(USE_DBUS_REQUIRED)
  else(NOT DBUS_FOUND)
    set(SYSTEM_LIBRARIES ${DBUS_LIBRARIES} ${SYSTEM_LIBRARIES})
    LIST(APPEND CMAKE_LIBRARY_PATH ${DBUS_LIBRARY_DIRS})
    link_directories (${DBUS_LIBRARY_DIRS})
  endif(NOT DBUS_FOUND)
endif(USE_DBUS)

gopt_test(USE_NFSIDMAP)
if(USE_NFSIDMAP)
  find_package(NfsIdmap ${USE_NFSIDMAP_REQUIRED})
  if(NFSIDMAP_FOUND)
    set(SYSTEM_LIBRARIES ${NFSIDMAP_LIBRARY} ${SYSTEM_LIBRARIES})
  else(NFSIDMAP_FOUND)
    message(WARNING "libnfsidmap not found, disabling USE_NFSIDMAP")
    set(USE_NFSIDMAP OFF)
  endif(NFSIDMAP_FOUND)
endif(USE_NFSIDMAP)

if(USE_EXECINFO)
  find_package(ExecInfo REQUIRED)
  set(SYSTEM_LIBRARIES ${EXECINFO_LIBRARY} ${SYSTEM_LIBRARIES})
endif(USE_EXECINFO)

gopt_test(USE_CUNIT)
if(USE_CUNIT)
  find_package(CUnit ${USE_CUNIT_REQUIRED})
  if (CUNIT_FOUND)
    set(SYSTEM_LIBRARIES ${CUNIT_LIBRARIES} ${SYSTEM_LIBRARIES})
  else (CUNIT_FOUND)
    message(WARNING "CUnit not found.  Disabling USE_CUNIT")
    set(USE_CUNIT OFF)
  endif (CUNIT_FOUND)
endif(USE_CUNIT)

gopt_test(_MSPAC_SUPPORT)
if(_MSPAC_SUPPORT)
  find_package(WBclient ${_MSPAC_SUPPORT_REQUIRED})
  if(WBCLIENT_FOUND)
    set(SYSTEM_LIBRARIES ${WBCLIENT_LIBRARIES} ${SYSTEM_LIBRARIES})
  else(WBCLIENT_FOUND)
    message(WARNING "Samba 4 wbclient not found.  Disabling MSPAC_SUPPORT")
    set(_MSPAC_SUPPORT OFF)
  endif(WBCLIENT_FOUND)
endif(_MSPAC_SUPPORT)

gopt_test(USE_LTTNG)
if(USE_LTTNG)
  # Set LTTNG_PATH_HINT on the command line
  # if your LTTng is not in a standard place
  find_package(LTTng ${USE_LTTNG_REQUIRED})
  if(LTTNG_FOUND)
    include_directories(${LTTNG_INCLUDE_DIR})
  else(LTTNG_FOUND)
    message(WARNING "LTTng libraries not found. Disabling USE_LTTNG")
    set(USE_LTTNG OFF)
  endif(LTTNG_FOUND)
endif(USE_LTTNG)

gopt_test(USE_RADOS_RECOV)
if(USE_RADOS_RECOV)
  find_package(RADOS ${USE_RADOS_RECOV_REQUIRED})
  if(NOT RADOS_FOUND)
    message(WARNING "Rados libraries not found. Disabling USE_RADOS_RECOV")
    set(USE_RADOS_RECOV OFF)
  endif(NOT RADOS_FOUND)
endif(USE_RADOS_RECOV)

gopt_test(RADOS_URLS)
if(RADOS_URLS)
  find_package(RADOS ${RADOS_URLS_REQUIRED})
  if(NOT RADOS_FOUND)
    message(WARNING "Rados libraries not found. Disabling RADOS_URLS")
    set(RADOS_URLS OFF)
  endif(NOT RADOS_FOUND)
endif(RADOS_URLS)

# Cmake 2.6 has issue in managing BISON and FLEX
if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8" )
   message( status "CMake 2.6 detected, using portability hooks" )
   set(CMAKE_CURRENT_LIST_DIR /usr/share/cmake/Modules )
   set(CMAKE_MODULE_PATH ${GANESHA_TOP_CMAKE_DIR}/cmake/portability_cmake_2.8  /usr/share/cmake/Modules ${CMAKE_MODULE_PATH})
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8" )

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}/include"
  "${OS_INCLUDE_DIR}"
)

if (HAVE_KRB5)
  include_directories(
    "${KRB5_INCLUDE_DIRS}"
    )
endif (HAVE_KRB5)

# check for d_off support, FreeBSD doesn't support it before 1200500
if(LINUX)
  add_definitions(-DHAS_DOFF)
elseif(FREEBSD)
  EXECUTE_PROCESS(
    COMMAND uname -K
    OUTPUT_VARIABLE FREEBSD_VERSION
    ERROR_QUIET
  )
  if (FREEBSD_VERSION GREATER 1200500)
    add_definitions(-DHAS_DOFF)
  endif()
elseif(DARWIN)
  # d_off unavailable on macOS
else() # d_off is on by default for other cases
  add_definitions(-DHAS_DOFF)
endif()

# Fixup loose bits of autotools legacy
set(_USE_9P ${USE_9P})
set(_USE_9P_RDMA ${USE_9P_RDMA})
set(_USE_NFS3 ${USE_NFS3})
set(_USE_NLM ${USE_NLM})
set(_USE_RQUOTA ${USE_RQUOTA})
set(_USE_CB_SIMULATOR ${USE_CB_SIMULATOR})

########### add a "make dist" and a "make rpm"  ###############

set( PKG_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}.tar.gz")
add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

# Find misc system libs

goption(USE_CAPS "Enable capability management" ON)
gopt_test(USE_CAPS)
if(USE_CAPS)
  find_package(Caps ${USE_CAPS_REQUIRED})
  if (CAPS_FOUND)
    set(SYSTEM_LIBRARIES ${SYSTEM_LIBRARIES} ${CAPS_LIBRARIES})
  else (CAPS_FOUND)
    message(WARNING "Capability libraries not found. Disabling USE_CAPS")
    set(USE_CAPS OFF)
  endif (CAPS_FOUND)
endif(USE_CAPS)

# Check if we have libblkid and libuuid, will just be reported under one
# flag USE_BLKID

check_include_files("blkid/blkid.h" HAVE_LIBBLKID_H)
find_library(LIBBLKID blkid)  # Management of Capablilities
check_library_exists(
	blkid
	blkid_devno_to_devname
	""
	HAVE_LIBBLKID
	)

check_include_files("uuid/uuid.h" HAVE_LIBUUID_H)
find_library(LIBUUID uuid)  # Management of Capablilities
check_library_exists(
	uuid
	uuid_parse
	""
	HAVE_LIBUUID
	)

if(HAVE_LIBBLKID AND HAVE_LIBUUID AND HAVE_LIBBLKID_H AND HAVE_LIBUUID_H)
  # we have all the libraries and include files to use libblkid and libuuid
  set(SYSTEM_LIBRARIES ${SYSTEM_LIBRARIES} ${LIBBLKID} ${LIBUUID})
  set(USE_BLKID ON)
else(HAVE_LIBBLKID AND HAVE_LIBUUID AND HAVE_LIBBLKID_H AND HAVE_LIBUUID_H)
  # we are missing something and can't use libblkid and libuuid
  set(USE_BLKID OFF)
  if(NOT HAVE_LIBBLKID)
    message(STATUS "Could not find blkid library, disabling USE_BLKID")
  elseif(NOT HAVE_LIBUUID)
    message(STATUS "Could not find uuid library, disabling USE_BLKID")
  elseif(NOT HAVE_LIBBLKID_H)
    message(STATUS "Could not find blkid header files, disabling USE_BLKID")
  else(NOT HAVE_LIBBLKID)
    message(STATUS "Could not find uuid header files, disabling USE_BLKID")
  endif(NOT HAVE_LIBBLKID)
endif(HAVE_LIBBLKID AND HAVE_LIBUUID AND HAVE_LIBBLKID_H AND HAVE_LIBUUID_H)

# check is daemon exists
# I use check_library_exists there to be portab;e
check_library_exists(
	c
	daemon
	""
	HAVE_DAEMON
	)

# Needs to be before ntirpc
add_custom_target( rpm DEPENDS dist)
add_custom_command(TARGET rpm
                  COMMAND sh -c "rpmbuild -ta ${PKG_NAME}"
		  VERBATIM
		  DEPENDS dist)

set(RPMDEST "--define '_srcrpmdir ${CMAKE_CURRENT_BINARY_DIR}'")
add_custom_target( srpm DEPENDS dist)
add_custom_command(TARGET srpm
                  COMMAND sh -c "rpmbuild ${RPMDEST} -ts ${PKG_NAME}"
		  VERBATIM
		  DEPENDS dist)

# Roll up required libraries

if(USE_9P_RDMA)
  find_package(PkgConfig)
  pkg_check_modules(MOOSHIKA REQUIRED libmooshika>=0.6)

  include_directories(${MOOSHIKA_INCLUDE_DIRS})
  link_directories (${MOOSHIKA_LIBRARY_DIRS})
endif(USE_9P_RDMA)

set(NTIRPC_MIN_VERSION 4.0)
if (USE_SYSTEM_NTIRPC)
  find_package(NTIRPC ${NTIRPC_MIN_VERSION} REQUIRED)
  if (USE_LTTNG)
    set(NTIRPC_LIBRARY ${NTIRPC_LIBRARY} ${NTIRPC_LTTNG})
    set(USE_LTTNG_NTIRPC ON)
  endif (USE_LTTNG)
else (USE_SYSTEM_NTIRPC)
  # Set options for submodule
  set(USE_RPC_RDMA ${USE_NFS_RDMA} CACHE BOOL "Use RDMA")
  set(TIRPC_EPOLL ${TIRPC_EPOLL} CACHE BOOL "Use EPOLL")
  set(USE_GSS ${USE_GSS} CACHE BOOL "Use GSS")
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
	  "${GANESHA_TOP_CMAKE_DIR}/libntirpc/cmake/modules/")
  add_subdirectory(libntirpc)
  set(NTIRPC_LIBRARY ntirpc)
  if (USE_LTTNG)
    set(NTIRPC_LIBRARY ${NTIRPC_LIBRARY} ntirpc_lttng)
  endif (USE_LTTNG)
  set(NTIRPC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/libntirpc/ntirpc/")
  message(STATUS "Using ntirpc submodule")
endif (USE_SYSTEM_NTIRPC)
message(${NTIRPC_INCLUDE_DIR})
include_directories(${NTIRPC_INCLUDE_DIR})

find_library(LIBURCU_LIB urcu-bp)
if (NOT LIBURCU_LIB)
  message(FATAL_ERROR "userspace-rcu library not found!")
endif(NOT LIBURCU_LIB)

find_path(LIBURCU_INC urcu-bp.h)
if (NOT LIBURCU_INC)
  message(FATAL_ERROR "userspace-rcu includes not found!")
endif(NOT LIBURCU_INC)
check_symbol_exists(urcu_ref_get_unless_zero urcu/ref.h HAVE_URCU_REF_GET_UNLESS_ZERO)

# All the plumbing in the basement
set(SYSTEM_LIBRARIES
  ${NTIRPC_LIBRARY}
  ${SYSTEM_LIBRARIES}
  ${LIBDL}
  ${KRB5_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBURCU_LIB}
)

include_directories(${LIBURCU_INC})

# Config file; make sure it doesn't clobber an existing one
include(${GANESHA_TOP_CMAKE_DIR}/cmake/modules/InstallPackageConfigFile.cmake)
InstallPackageConfigFile(${GANESHA_TOP_CMAKE_DIR}/config_samples/ganesha.conf.example ${SYSCONFDIR}/ganesha ganesha.conf)

add_subdirectory(log)
add_subdirectory(config_parsing)
add_subdirectory(cidr)
add_subdirectory(test)
add_subdirectory(avl)
add_subdirectory(hashtable)
add_subdirectory(SAL)
add_subdirectory(RPCAL)
add_subdirectory(Protocols)
add_subdirectory(support)
add_subdirectory(os)

add_subdirectory(FSAL)
add_subdirectory(idmapper)

add_subdirectory(MainNFSD)
add_subdirectory(tools)

if(USE_GTEST)
  add_subdirectory(gtest)
endif(USE_GTEST)

if(USE_DBUS)
  add_subdirectory(dbus)
endif(USE_DBUS)

if(USE_LTTNG)
  add_subdirectory(tracing)
endif(USE_LTTNG)

add_subdirectory(scripts)
add_subdirectory(doc)
# display configuration vars

message(STATUS)
message(STATUS "-------------------------------------------------------")
message(STATUS "PLATFORM = ${PLATFORM}")
message(STATUS "ARCH = ${ARCH}")
message(STATUS "VERSION = ${GANESHA_VERSION}")
message(STATUS "BUILD HOST = ${BUILD_HOST_NAME}")
message(STATUS "-------------------------------------------------------")
message(STATUS "USE_FSAL_PROXY_V4 = ${USE_FSAL_PROXY_V4}")
message(STATUS "USE_FSAL_PROXY_V3 = ${USE_FSAL_PROXY_V3}")
message(STATUS "USE_FSAL_VFS = ${USE_FSAL_VFS}")
message(STATUS "USE_FSAL_LUSTRE = ${USE_FSAL_LUSTRE}")
message(STATUS "USE_FSAL_LIZARDFS = ${USE_FSAL_LIZARDFS}")
message(STATUS "USE_FSAL_KVSFS = ${USE_FSAL_KVSFS}")
message(STATUS "USE_FSAL_CEPH = ${USE_FSAL_CEPH}")
message(STATUS "USE_FSAL_CEPH_MKNOD = ${USE_FSAL_CEPH_MKNOD}")
message(STATUS "USE_FSAL_CEPH_SETLK = ${USE_FSAL_CEPH_SETLK}")
message(STATUS "USE_FSAL_CEPH_LL_LOOKUP_ROOT = ${USE_FSAL_CEPH_LL_LOOKUP_ROOT}")
message(STATUS "USE_FSAL_CEPH_STATX = ${USE_FSAL_CEPH_STATX}")
message(STATUS "USE_FSAL_CEPH_LL_DELEGATION = ${USE_FSAL_CEPH_LL_DELEGATION}")
message(STATUS "USE_FSAL_CEPH_LL_SYNC_INODE = ${USE_FSAL_CEPH_LL_SYNC_INODE}")
message(STATUS "USE_FSAL_CEPH_ABORT_CONN = ${USE_FSAL_CEPH_ABORT_CONN}")
message(STATUS "USE_FSAL_CEPH_RECLAIM_RESET = ${USE_FSAL_CEPH_RECLAIM_RESET}")
message(STATUS "USE_FSAL_CEPH_GET_FS_CID = ${USE_FSAL_CEPH_GET_FS_CID}")
message(STATUS "USE_FSAL_RGW = ${USE_FSAL_RGW}")
message(STATUS "USE_FSAL_XFS = ${USE_FSAL_XFS}")
message(STATUS "USE_FSAL_GPFS = ${USE_FSAL_GPFS}")
message(STATUS "USE_FSAL_GLUSTER = ${USE_FSAL_GLUSTER}")
message(STATUS "USE_FSAL_NULL = ${USE_FSAL_NULL}")
message(STATUS "USE_FSAL_MEM = ${USE_FSAL_MEM}")
message(STATUS "GSH_CAN_HOST_LOCAL_FS = ${GSH_CAN_HOST_LOCAL_FS}")
message(STATUS "USE_SYSTEM_NTIRPC = ${USE_SYSTEM_NTIRPC}")
message(STATUS "USE_DBUS = ${USE_DBUS}")
message(STATUS "USE_CB_SIMULATOR = ${USE_CB_SIMULATOR}")
message(STATUS "USE_BTRFSUTIL = ${USE_BTRFSUTIL}")
message(STATUS "USE_NFSIDMAP = ${USE_NFSIDMAP}")
message(STATUS "ENABLE_ERROR_INJECTION = ${ENABLE_ERROR_INJECTION}")
message(STATUS "ENABLE_VFS_DEBUG_ACL = ${ENABLE_VFS_DEBUG_ACL}")
message(STATUS "ENABLE_RFC_ACL = ${ENABLE_RFC_ACL}")
message(STATUS "CEPHFS_POSIX_ACL = ${CEPHFS_POSIX_ACL}")
message(STATUS "USE_CAPS = ${USE_CAPS}")
message(STATUS "USE_BLKID = ${USE_BLKID}")
message(STATUS "DISTNAME_HAS_GIT_DATA = ${DISTNAME_HAS_GIT_DATA}" )
message(STATUS "_MSPAC_SUPPORT = ${_MSPAC_SUPPORT}")
message(STATUS "USE_EFENCE = ${USE_EFENCE}")
message(STATUS "_NO_TCP_REGISTER = ${_NO_TCP_REGISTER}")
message(STATUS "RPCBIND = ${RPCBIND}")
message(STATUS "DEBUG_SAL = ${DEBUG_SAL}")
message(STATUS "_VALGRIND_MEMCHECK = ${_VALGRIND_MEMCHECK}")
message(STATUS "ENABLE_LOCKTRACE = ${ENABLE_LOCKTRACE}")
message(STATUS "PROXYV4_HANDLE_MAPPING = ${PROXYV4_HANDLE_MAPPING}")
message(STATUS "DEBUG_MDCACHE = ${DEBUG_MDCACHE}")
message(STATUS "DEBUG_SYMS = ${DEBUG_SYMS}")
message(STATUS "COVERAGE = ${COVERAGE}")
message(STATUS "ENFORCE_GCC = ${ENFORCE_GCC}")
message(STATUS "USE_GTEST = ${USE_GTEST}")
message(STATUS "USE_GSS = ${USE_GSS}")
message(STATUS "TIRPC_EPOLL = ${TIRPC_EPOLL}")
message(STATUS "USE_9P = ${USE_9P}")
message(STATUS "_USE_9P = ${_USE_9P}")
message(STATUS "_USE_9P_RDMA = ${_USE_9P_RDMA}")
message(STATUS "USE_NFS_RDMA = ${USE_NFS_RDMA}")
message(STATUS "USE_NFS3 = ${USE_NFS3}")
message(STATUS "USE_NLM = ${USE_NLM}")
message(STATUS "USE_NFSACL3 = ${USE_NFSACL3}")
message(STATUS "USE_ACL_MAPPING = ${USE_ACL_MAPPING}")
message(STATUS "KRB5_PREFIX = ${KRB5_PREFIX}")
message(STATUS "CEPH_PREFIX = ${CEPH_PREFIX}")
message(STATUS "RGW_PREFIX = ${RGW_PREFIX}")
message(STATUS "GLUSTER_PREFIX = ${GLUSTER_PREFIX}")
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
message(STATUS "_GIT_HEAD_COMMIT = ${_GIT_HEAD_COMMIT}")
message(STATUS "_GIT_HEAD_COMMIT_ABBREV = ${_GIT_HEAD_COMMIT_ABBREV}")
message(STATUS "_GIT_DESCRIBE = ${_GIT_DESCRIBE}")
message(STATUS "ALLOCATOR = ${ALLOCATOR}")
message(STATUS "GOLD_LINKER = ${GOLD_LINKER}")
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
message(STATUS "FSAL_DESTINATION = ${FSAL_DESTINATION}")
message(STATUS "USE_ADMIN_TOOLS = ${USE_ADMIN_TOOLS}")
message(STATUS "USE_GUI_ADMIN_TOOLS = ${USE_GUI_ADMIN_TOOLS}")
message(STATUS "MODULES_PATH = ${MODULES_PATH}")
message(STATUS "USE_LTTNG = ${USE_LTTNG}")
message(STATUS "USE_VSOCK = ${USE_VSOCK}")
message(STATUS "USE_TOOL_MULTILOCK = ${USE_TOOL_MULTILOCK}")
message(STATUS "USE_MAN_PAGE = ${USE_MAN_PAGE}")
message(STATUS "USE_RADOS_RECOV = ${USE_RADOS_RECOV}")
message(STATUS "RADOS_URLS = ${RADOS_URLS}")
message(STATUS "USE_CUNIT = ${USE_CUNIT}")
message(STATUS "SANITIZE_ADDRESS = ${SANITIZE_ADDRESS}")

set(USE_CB_SIMULATOR ${USE_CB_SIMULATOR}
  CACHE BOOL
   "enable callback simulator thread"
   FORCE)

set(DEBUG_SAL ${DEBUG_SAL}
  CACHE BOOL
   "enable debug SAL"
   FORCE)

set(_VALGRIND_MEMCHECK ${_VALGRIND_MEMCHECK}
  CACHE BOOL
   "Initialize buffers passed to GPFS ioctl"
   FORCE)

set(ENABLE_ERROR_INJECTION ${ENABLE_ERROR_INJECTION}
  CACHE BOOL
   "enable error injection"
   FORCE)

set(ENABLE_VFS_DEBUG_ACL ${ENABLE_VFS_DEBUG_ACL}
  CACHE BOOL
   "Enable debug ACL store for VFS"
   FORCE)

set(ENABLE_RFC_ACL ${ENABLE_RFC_ACL}
  CACHE BOOL
   "Enable debug ACL store for VFS"
   FORCE)

set( DISTNAME_HAS_GIT_DATA ${DISTNAME_HAS_GIT_DATA}
  CACHE BOOL
  "Distribution package's name carries git data"
  FORCE)

set(USE_9P ${USE_9P}
  CACHE BOOL
   "enable 9P support"
   FORCE)

set(_USE_9P ${_USE_9P}
  CACHE BOOL
   "enable 9P support in config"
   FORCE)

set(_USE_9P_RDMA ${_USE_9P_RDMA}
  CACHE BOOL
   "enable 9P_RDMA support"
   FORCE)

set(USE_NFS3 ${USE_NFS3}
  CACHE BOOL
   "enable NFSv3 support"
   FORCE)

set(USE_NLM ${USE_NLM}
  CACHE BOOL
   "enable NLM support"
   FORCE)

set(USE_NFS_RDMA ${USE_NFS_RDMA}
  CACHE BOOL
  "enable nfs RDMA"
  FORCE)

set(_USE_NFS_RDMA ${USE_NFS_RDMA}
  CACHE BOOL
  "enable nfs RDMA in config"
  FORCE)

set(USE_ACL_MAPPING ${USE_ACL_MAPPING}
  CACHE BOOL
  "Build NFSv4 to POSIX ACL mapping"
  FORCE)


# Now create a useable config.h
configure_file(
  "${PROJECT_SOURCE_DIR}/include/config-h.in.cmake"
  "${PROJECT_BINARY_DIR}/include/config.h"
)

# Tweak the "%bcond_<with|without> in the specfile for every
# optional feature. Take care on the logic of this syntax
# %bcond_with means you add a "--with" option, default is "without this feature"
# %bcond_without adds a"--without" so the feature is enabled by default
# This has to be coherent with chosen FSALs

if(USE_FSAL_XFS)
    set(BCOND_XFS "%bcond_without")
else(USE_FSAL_XFS)
    set(BCOND_XFS "%bcond_with")
endif(USE_FSAL_XFS)

if(USE_FSAL_LUSTRE AND USE_LLAPI)
    set(BCOND_LUSTRE "%bcond_without")
else(USE_FSAL_LUSTRE AND USE_LLAPI)
    set(BCOND_LUSTRE "%bcond_with")
endif(USE_FSAL_LUSTRE AND USE_LLAPI)

if(USE_FSAL_GPFS)
    set(BCOND_GPFS "%bcond_without")
else(USE_FSAL_GPFS)
    set(BCOND_GPFS "%bcond_with")
endif(USE_FSAL_GPFS)

if(USE_FSAL_LIZARDFS)
    set(BCOND_LIZARDFS "%bcond_without")
else(USE_FSAL_LIZARDFS)
    set(BCOND_LIZARDFS "%bcond_with")
endif(USE_FSAL_LIZARDFS)

if(USE_FSAL_KVSFS)
    set(BCOND_KVSFS "%bcond_without")
else(USE_FSAL_KVSFS)
    set(BCOND_KVSFS "%bcond_with")
endif(USE_FSAL_KVSFS)

if(USE_FSAL_CEPH)
    set(BCOND_CEPH "%bcond_without")
else(USE_FSAL_CEPH)
    set(BCOND_CEPH "%bcond_with")
endif(USE_FSAL_CEPH)

if(USE_FSAL_RGW)
    set(BCOND_RGW "%bcond_without")
else(USE_FSAL_RGW)
    set(BCOND_RGW "%bcond_with")
endif(USE_FSAL_RGW)

if(USE_FSAL_GLUSTER)
    set(BCOND_GLUSTER "%bcond_without")
else(USE_FSAL_GLUSTER)
    set(BCOND_GLUSTER "%bcond_with")
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_NULL)
    set(BCOND_NULLFS "%bcond_without")
else(USE_FSAL_NULL)
    set(BCOND_NULLFS "%bcond_with")
endif(USE_FSAL_NULL)

if(USE_FSAL_MEM)
    set(BCOND_MEM "%bcond_without")
else(USE_FSAL_MEM)
    set(BCOND_MEM "%bcond_with")
endif(USE_FSAL_MEM)

if(USE_9P)
    set(BCOND_9P "%bcond_without")
    if(USE_9P_RDMA)
        set(BCOND_RDMA "%bcond_without")
    else(USE_9P_RDMA)
        set(BCOND_RDMA "%bcond_with")
    endif(USE_9P_RDMA)
else(USE_9P)
    set(BCOND_9P "%bcond_with")
    set(BCOND_RDMA "%bcond_with")
endif(USE_9P)

if(USE_LTTNG)
    set(BCOND_LTTNG "%bcond_without")
else(USE_LTTNG)
    set(BCOND_LTTNG "%bcond_with")
endif(USE_LTTNG)

if(${ALLOCATOR} STREQUAL "jemalloc")
    set(BCOND_JEMALLOC "%bcond_without")
else(${ALLOCATOR} STREQUAL "jemalloc")
    set(BCOND_JEMALLOC "%bcond_with")
endif(${ALLOCATOR} STREQUAL "jemalloc")

if(USE_ADMIN_TOOLS)
    set(BCOND_UTILS "%bcond_without")
else(USE_ADMIN_TOOLS)
    set(BCOND_UTILS "%bcond_with")
endif(USE_ADMIN_TOOLS)

if(USE_GUI_ADMIN_TOOLS)
    set(BCOND_GUI_UTILS "%bcond_without")
else(USE_GUI_ADMIN_TOOLS)
    set(BCOND_GUI_UTILS "%bcond_with")
endif(USE_GUI_ADMIN_TOOLS)

if (USE_SYSTEM_NTIRPC)
  set(BCOND_NTIRPC "%bcond_without")
else(USE_SYSTEM_NTIRPC)
  set(BCOND_NTIRPC "%bcond_with")
endif(USE_SYSTEM_NTIRPC)

if (USE_MAN_PAGE)
  set(BCOND_MAN_PAGE "%bcond_without")
else(USE_MAN_PAGE)
  set(BCOND_MAN_PAGE "%bcond_with")
endif(USE_MAN_PAGE)

if(USE_RADOS_RECOV)
    set(BCOND_RADOS_RECOV "%bcond_without")
else(USE_RADOS_RECOV)
    set(BCOND_RADOS_RECOV "%bcond_with")
endif(USE_RADOS_RECOV)

if(RADOS_URLS)
    set(BCOND_RADOS_URLS "%bcond_without")
else(RADOS_URLS)
    set(BCOND_RADOS_URLS "%bcond_with")
endif(RADOS_URLS)

if(RPCBIND)
    set(BCOND_RPCBIND "%bcond_without")
else(RPCBIND)
    set(BCOND_RPCBIND "%bcond_with")
endif(RPCBIND)

if(_MSPAC_SUPPORT)
    set(BCOND_MSPAC_SUPPORT "%bcond_without")
else(_MSPAC_SUPPORT)
    set(BCOND_MSPAC_SUPPORT "%bcond_with")
endif(_MSPAC_SUPPORT)

if(SANITIZE_ADDRESS)
    set(BCOND_SANITIZE_ADDRESS "%bcond_without")
else(SANITIZE_ADDRESS)
    set(BCOND_SANITIZE_ADDRESS "%bcond_with")
endif(SANITIZE_ADDRESS)

# Now create a useable specfile
configure_file(
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec-in.cmake"
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec"
)

configure_file(
  "${PROJECT_SOURCE_DIR}/scripts/systemd/nfs-ganesha-config.service-in.cmake"
  "${PROJECT_SOURCE_DIR}/scripts/systemd/nfs-ganesha-config.service"
)

# Make a docker image
set(DOCKER_TMP_INSTALL_PATH "${PROJECT_BINARY_DIR}/docker/root")
find_package(LSB)
if(LSB_RELEASE_EXECUTABLE)
  string(TOLOWER ${LSB_RELEASE_ID_SHORT} DOCKER_DISTRO)
  set(DOCKER_DISTRO_VERSION "${LSB_RELEASE_RELEASE_SHORT}")

  configure_file(
    "${PROJECT_SOURCE_DIR}/scripts/docker/entrypoint.sh-in.cmake"
    "${PROJECT_BINARY_DIR}/docker/entrypoint.sh"
    @ONLY
    )
  configure_file(
    "${PROJECT_SOURCE_DIR}/scripts/docker/Dockerfile-in.cmake"
    "${PROJECT_BINARY_DIR}/docker/Dockerfile"
    @ONLY
    )
  add_custom_target(docker
    COMMAND sh -c "make DESTDIR=${PROJECT_BINARY_DIR}/docker/root install"
    COMMAND sh -c "docker build -t ganesha/dev ${PROJECT_BINARY_DIR}/docker"
    VERBATIM )
  add_dependencies(docker ganesha.nfsd )
endif(LSB_RELEASE_EXECUTABLE)


if(COVERAGE)
	find_program(LCOV_EXEC lcov)
	find_program(GENHTML_EXEC genhtml)
	if(LCOV_EXEC AND GENHTML_EXEC)
		add_custom_target(lcov)
		add_custom_command(TARGET lcov
			COMMAND ${LCOV_EXEC} --capture --directory .
			--output-file coverage.info
			COMMAND ${GENHTML_EXEC} coverage.info
			--output-directory ./coverage_html/
			VERBATIM
			WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
			)
	endif(LCOV_EXEC AND GENHTML_EXEC)
endif(COVERAGE)

########### next target ###############

add_executable(ganesha.nfsd ./MainNFSD/nfs_main.c)
add_sanitizers(ganesha.nfsd)

target_link_libraries(ganesha.nfsd
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)
set_target_properties(ganesha.nfsd PROPERTIES COMPILE_FLAGS "-fPIC")
install(TARGETS ganesha.nfsd COMPONENT daemon DESTINATION bin)

########### next target ###############

if (USE_RADOS_RECOV)
  add_executable(ganesha-rados-grace
    ./tools/ganesha-rados-grace.c ./support/rados_grace.c)
  add_sanitizers(ganesha-rados-grace)
  include_directories(${RADOS_INCLUDE_DIR})
  target_link_libraries(ganesha-rados-grace
    ganesha_nfsd
    ${RADOS_LIBRARIES}
    ${LDFLAG_DISALLOW_UNDEF}
  )
  install(TARGETS ganesha-rados-grace COMPONENT tools DESTINATION bin)
endif(USE_RADOS_RECOV)

########### next target ###############

if(LINUX)

  add_executable(sm_notify.ganesha ./Protocols/NLM/sm_notify.c)
  add_sanitizers(sm_notify.ganesha)

  target_link_libraries(sm_notify.ganesha
    ganesha_nfsd
    ${LIBTIRPC_LIBRARIES}
    ${SYSTEM_LIBRARIES}
    ${LTTNG_LIBRARIES}
    ${LDFLAG_DISALLOW_UNDEF}
  )

  if( USE_ADMIN_TOOLS )
    install(TARGETS sm_notify.ganesha COMPONENT daemon DESTINATION bin)
  endif( USE_ADMIN_TOOLS )
endif(LINUX)

########### next target ###############

add_executable(verif_syntax EXCLUDE_FROM_ALL ./config_parsing/verif_syntax.c)
add_sanitizers(verif_syntax)

target_link_libraries(verif_syntax
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

########### next target ###############

add_executable(test_parsing EXCLUDE_FROM_ALL ./config_parsing/test_parse.c)
add_sanitizers(test_parsing)

target_link_libraries(test_parsing
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

########### add a "make doc" target to call Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${GANESHA_TOP_CMAKE_DIR}/Doxyfile.in
		 ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif(DOXYGEN_FOUND)
