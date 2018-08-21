# NFS Ganesha  Cmake

# Current version as of Fedora 16.  Not tested with earlier.

cmake_minimum_required(VERSION 2.6.3)

message( STATUS "cmake version ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" )

if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  if(COMMAND cmake_policy)
      cmake_policy(SET CMP0017 NEW)
  endif(COMMAND cmake_policy)
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

project(nfs-ganesha C CXX)
# Project versioning
set(GANESHA_MAJOR_VERSION 2)
set(GANESHA_MINOR_VERSION 6)

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
# Patch level is always ".0" for mainline (master).  It is blank for development.
# When starting a stable maintenance branch, this becomes ".N"
# where N is monotonically increasing starting at 1. Remember to include the "." !!
set(GANESHA_PATCH_LEVEL .3)

# Extra version is for naming development/RC.  It is blank in master/stable branches
# so it can be available to end-users to name local variants/versions
# If used, it is always of the form "-whateveryouwant"
set(GANESHA_EXTRA_VERSION )

set(GANESHA_VERSION ${GANESHA_MAJOR_VERSION}.${GANESHA_MINOR_VERSION}${GANESHA_PATCH_LEVEL}${GANESHA_EXTRA_VERSION})
set(GANESHA_BASE_VERSION ${GANESHA_MAJOR_VERSION}.${GANESHA_MINOR_VERSION}${GANESHA_PATCH_LEVEL})

set(VERSION_COMMENT
  "GANESHA file server is 64 bits compliant and supports NFS v3,4.0,4.1 (pNFS) and 9P"
)

# find out which platform we are building on
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(LINUX ON)
  set(UNIX ON)
  # Now detects the Linux's distro
  set(DISTRO "UNKNOWN")
  set(LIBEXECDIR "/usr/libexec")

  EXECUTE_PROCESS(
      COMMAND awk -F=  "/^NAME=/ { print $2 }" /etc/os-release
      OUTPUT_VARIABLE SYS_RELEASE
      ERROR_QUIET
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
     set(LIBEXECDIR "/usr/lib")
  elseif( ${SYS_RELEASE} MATCHES "openSUSE Leap" )
     message( STATUS "Detected a Linux openSUSE Leap machine" )
     set(DISTRO "SLES")
     set(LIBEXECDIR "/usr/lib")
  elseif( ${SYS_RELEASE} MATCHES "openSUSE Tumbleweed" )
     message( STATUS "Detected a Linux openSUSE Tumbleweed machine" )
     set(DISTRO "SLES")
     set(LIBEXECDIR "/usr/lib")
  elseif( (${SYS_RELEASE} MATCHES "Debian GNU/Linux") OR (${SYS_RELEASE} MATCHES "Ubuntu") )
     message( STATUS "Detected a Linux Debian base machine" )
     set(DISTRO "DEBIAN")
     set(LIBEXECDIR "/usr/lib")
  else( ${SYS_RELEASE} MATCHES "Red Hat" )
     message( STATUS "Detected an UNKNOWN Linux machine" )
     set(DISTRO "UNKNOWN")
  endif( ${SYS_RELEASE} MATCHES "Red Hat" )
endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(FREEBSD ON)
  set(UNIX ON)
# On FreeBSD libc doesn't directly provide libexecinfo, so we have to find it
  set(USE_EXECINFO ON)
endif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(WINDOWS ON)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

# Identify the host we are building on
EXECUTE_PROCESS( COMMAND hostname
  OUTPUT_VARIABLE BUILD_HOST_NAME
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )

find_package(Toolchain REQUIRED)
find_package(Sanitizers)

# Add maintainer mode for (mainly) strict builds
include(${CMAKE_SOURCE_DIR}/cmake/maintainer_mode.cmake)

# For libraries that provide pkg-config files
include(FindPkgConfig)

# If we are in a git tree, then this CMakeLists.txt is in "src/" and go .git is in "src/.."
IF( EXISTS ${CMAKE_SOURCE_DIR}/../.git/HEAD  )
  message( STATUS "Compilation from within a git repository. Using git rev-parse HEAD")
  EXECUTE_PROCESS( COMMAND git rev-parse HEAD
		   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
                   OUTPUT_VARIABLE _GIT_HEAD_COMMIT)
  EXECUTE_PROCESS( COMMAND git describe --long
                   WORKING_DIRECTORY  ${CMAKE_SOURCE_DIR}
		   OUTPUT_STRIP_TRAILING_WHITESPACE
		   ERROR_QUIET
                   OUTPUT_VARIABLE _GIT_DESCRIBE)

ELSE( EXISTS ${CMAKE_SOURCE_DIR}/../.git/HEAD  )
  message( STATUS "Outside a git repository, use saved data" )
  EXEC_PROGRAM(${CMAKE_SOURCE_DIR}/cmake/githead_from_path.sh ARGS ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE  _GIT_HEAD_COMMIT)

  EXEC_PROGRAM(${CMAKE_SOURCE_DIR}/cmake/gitdesc_from_path.sh ARGS ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE  _GIT_DESCRIBE)

ENDIF( EXISTS ${CMAKE_SOURCE_DIR}/../.git/HEAD  )

STRING(SUBSTRING ${_GIT_HEAD_COMMIT} 0  7 _GIT_HEAD_COMMIT_ABBREV )

if (FREEBSD)
#default gcc doesn't like using -Wuninitialized without -O on FreeBSD
    set(PLATFORM "FREEBSD")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -ggdb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-optimize-sibling-calls")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-dynamic")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/freebsd")
    find_library(LIBDL c)  # libc suffices on freebsd
endif(FREEBSD)

if (LINUX)
    set(PLATFORM "LINUX")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64")
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
option(USE_FSAL_PROXY "build PROXY FSAL shared library" ON)
option(USE_FSAL_VFS "build VFS FSAL shared library" ON)
option(USE_FSAL_CEPH "build CEPH FSAL shared library" ON)
option(USE_FSAL_GPFS "build GPFS FSAL" ON)
option(USE_FSAL_XFS "build XFS support in VFS FSAL" ON)
option(USE_FSAL_PANFS "build PanFS support in VFS FSAL" OFF)
option(USE_FSAL_GLUSTER "build GLUSTER FSAL shared library" ON)
option(USE_FSAL_NULL "build NULL FSAL shared library" ON)
option(USE_FSAL_RGW "build RGW FSAL shared library" OFF)
option(USE_FSAL_MEM "build Memory FSAL shared library" ON)
option(USE_TOOL_MULTILOCK "build multilock tool" OFF)

# nTIRPC
option(USE_SYSTEM_NTIRPC "Use the system nTIRPC, rather than the submodule" OFF)
option (USE_GSS "enable RPCSEC_GSS support" ON)
option(TIRPC_EPOLL "platform supports EPOLL or emulation" ON)

# Build configure options
option(USE_DBUS "enable DBUS protocol support" OFF)

# Various DBUS enabled features
option(USE_CB_SIMULATOR "enable callback simulator thread" OFF)

option(USE_NFSIDMAP "Use of libnfsidmap for name resolution" ON)
option(ENABLE_ERROR_INJECTION "enable error injection" OFF)
option(ENABLE_VFS_DEBUG_ACL "Enable debug ACL store for VFS" OFF)
option(ENABLE_RFC_ACL "Use all RFC ACL checks" OFF)

# Electric Fence (-lefence) link flag
option(USE_EFENCE "link with efence memory debug library" OFF)

# These are -D_FOO options, why ???  should be flags??
option(_NO_TCP_REGISTER "disable registration of tcp services on portmapper" OFF)
option(RPCBIND "enable registration with rpcbind" ON)
option(_NO_XATTRD "disable ghost xattr directory and files support" ON)
option(DEBUG_SAL "enable debugging of SAL by keeping list of all locks, stateids, and state owners" OFF)
option(_VALGRIND_MEMCHECK "Initialize buffers passed to GPFS ioctl that valgrind doesn't understand" OFF)
option(ENABLE_LOCKTRACE "Enable lock trace" OFF)
option(PROXY_HANDLE_MAPPING "enable NFSv3 handle mapping for PROXY FSAL" OFF)
option(DEBUG_MDCACHE "Add various asserts to mdcache" OFF)

# Debug symbols (-g) build flag
option(DEBUG_SYMS "include debug symbols to binaries (-g option)" OFF)

# Add coverage information to build tree
option(COVERAGE "add flag to generate coverage data at runtime" OFF)

# Add coverage information to build tree
option(ENFORCE_GCC "enforce gcc as a the C compiler used for the project" OFF)

# enable code profiling [-g -pg]
option(PROFILING "turn on code profiling (-g and -pg)" OFF)

# Define CPACK component (to deal with sub packages)
set(CPACK_COMPONENTS_ALL daemon fsal headers )
set(CPACK_COMPONENT_DAEMON_DISPLAY_NAME "NFS-Ganesha daemon")
if (USE_SYSTEM_NTIRPC)
  # Don't include libntirpc in the tarball
  set(CPACK_SOURCE_IGNORE_FILES "libntirpc")
else(USE_SYSTEM_NTIRPC)
  # Don't include libntirpc's spec file; this can confuse rpmbuild
  set(CPACK_SOURCE_IGNORE_FILES "libntirpc.spec$")
endif(USE_SYSTEM_NTIRPC)

# Include custom config and cpack module
include(${CMAKE_SOURCE_DIR}/cmake/cpack_config.cmake)
include(CPack)

# MSPAC support -lwbclient link flag
if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  option(_MSPAC_SUPPORT "enable mspac Winbind support" ON)
else( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  option(_MSPAC_SUPPORT "enable mspac Winbind support" OFF)
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )

# CUnit
option(USE_CUNIT "Use Cunit test framework" OFF)

# Blkin (Zipkin) Tracing
option(USE_BLKIN "Use Blkin/Zipkin trace framework" OFF)
option(BLKIN_PREFIX "Blkin installation prefix" "/opt/blkin")
if(USE_BLKIN)
  find_package(LTTng)
  if(LTTNG_FOUND)
  else(LTTNG_FOUND)
    message(WARNING "LTTng libraries not found. Disabling USE_BLKIN")
    set(USE_BLKIN OFF)
  endif(LTTNG_FOUND)
  if (NOT BLKIN_PREFIX)
    set(BLKIN_PREFIX "/opt/blkin")
  endif(NOT BLKIN_PREFIX)
  set(BLKIN_PREFIX ${BLKIN_PREFIX} CACHE PATH "Blkin path")
  find_library(BLKIN NAMES blkin PATHS "${BLKIN_PREFIX}/lib" REQUIRED)
  find_library(BLKIN_INIT NAMES blkin_init PATHS "${BLKIN_REFIX}/lib" REQUIRED)
  find_library(LTTNG NAMES lttng-ust REQUIRED)
  # build flags
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_BLKIN -I${BLKIN_PREFIX}/include")
  set(SYSTEM_LIBRARIES ${BLKIN} ${SYSTEM_LIBRARIES})
  set(SYSTEM_LIBRARIES ${LTTNG}  ${SYSTEM_LIBRARIES})
endif(USE_BLKIN)

# GTest
option(USE_GTEST "Use Google Test test framework" OFF)
option(GTEST_PREFIX "Google Test installation prefix"
  "/opt/gmock/gtest")
if(USE_GTEST)
  find_package(LTTng)
  find_package(Gperftools)
  if(LTTNG_CTL_FOUND AND GPERFTOOLS_FOUND)
    if (NOT GTEST_PREFIX)
      set(GTEST_PREFIX "/opt/gmock/gtest")
    endif(NOT GTEST_PREFIX)
    set(GTEST_PREFIX ${GTEST_PREFIX} CACHE PATH "Google Test path")
    find_library(GTEST NAMES gtest PATHS "${GTEST_PREFIX}")
    find_library(GTEST_MAIN NAMES gtest_main PATHS "${GTEST_PREFIX}")
  else(LTTNG_CTL_FOUND AND GPERFTOOLS_FOUND)
    message(WARNING "Couldn't find eithe LTTng ctl libraries or gperftools. Disabling USE_GTEST")
    set(USE_GTEST OFF)
  endif(LTTNG_CTL_FOUND AND GPERFTOOLS_FOUND)
endif(USE_GTEST)

# NFS RDMA
option(USE_NFS_RDMA "enable NFS/RDMA support" OFF)

# Enable 9P Support
option(USE_9P "enable 9P support" ON)
option(USE_9P_RDMA "enable 9P_RDMA support" OFF)

# Enable NFSv3 Support
option(USE_NFS3 "enable NFSv3 support" ON)

# Enable NLM Support
option(USE_NLM "enable NLM support" ON)

# AF_VSOCK host support (NFS)
option(USE_VSOCK "enable AF_VSOCK listener" OFF)
if(USE_VSOCK)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRPC_VSOCK")
endif(USE_VSOCK)

# This option will stop cmake compilation if a requested FSAL could not be built
option(STRICT_PACKAGE "Enable strict packaging behavior" OFF )

# This option will trigger "long distro name" aka name that contains git information
option(DISTNAME_HAS_GIT_DATA "Distribution package's name carries git data" OFF )

# Build and package Python admin scripts for managing via DBus
option(USE_ADMIN_TOOLS "Package Admin scripts" OFF)

# Build and package Python gui admin scripts for managing via DBus
option(USE_GUI_ADMIN_TOOLS "Package GUI Admin scripts" ON)

# Enable GCC Thread-sanitizer
option(USE_TSAN "Enable GCC Thread-Sanitizer" OFF)

# Enable LTTng tracing
option(USE_LTTNG "Enable LTTng tracing" OFF)

# Build man page.
option(USE_MAN_PAGE "Build MAN page" OFF) 

# Enable Rados KV store for recovery
option(USE_RADOS_RECOV "Enable Rados KV Recovery" ON)

# Enable RADOS URL config file sections
option(RADOS_URLS "Enable config file inclusion from RADOS objects" OFF)

#
# End build options
#

# Choose a shortcut build config

IF(BUILD_CONFIG)
  INCLUDE(
  ${CMAKE_SOURCE_DIR}/cmake/build_configurations/${BUILD_CONFIG}.cmake)
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

check_include_files(stdbool.h HAVE_STDBOOL_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(string.h HAVE_STRING_H)

if(HAVE_STRING_H AND HAVE_STRINGS_H)
  # we have all the libraries and include files to use string.h
  set(HAVE_STRNLEN ON)
endif(HAVE_STRING_H AND HAVE_STRINGS_H)

# PROXY handle mapping needs sqlite3
IF(PROXY_HANDLE_MAPPING)
  check_include_files(sqlite3.h HAVE_SQLITE3_H)
  check_library_exists(
    sqlite3
    sqlite3_open
    ""
    HAVE_SQLITE3
    )
  if(NOT HAVE_SQLITE3 OR NOT HAVE_SQLITE3_H)
    message(WARNING "Cannot find sqlite3.h or the library. Disabling proxy handle mapping")
    set(PROXY_HANDLE_MAPPING OFF)
  endif(NOT HAVE_SQLITE3 OR NOT HAVE_SQLITE3_H)
ENDIF(PROXY_HANDLE_MAPPING)

IF(_VALGRIND_MEMCHECK)
  check_include_files(valgrind/memcheck.h HAVE_MEMCHECK_H)
  if(NOT HAVE_MEMCHECK_H)
	  message(FATAL_ERROR "Cannot find valgrind/memcheck.h, install valgrind-devel package to enable _VALGRIND_MEMCHECK")
  ENDIF(NOT HAVE_MEMCHECK_H)
ENDIF(_VALGRIND_MEMCHECK)

# X_ATTRD requires the kernel to have xattrs...DBUS_STATS
if(NOT _NO_XATTRD)
  check_include_files("unistd.h;sys/xattr.h" HAVE_XATTR_H)
  if(NOT HAVE_XATTR_H)
    message(WARNING "Cannot find xattr.h. Disabling XATTRD support: ${HAVE_XATTR_H}")
    set(_NO_XATTRD ON)
  endif(NOT HAVE_XATTR_H)
endif(NOT _NO_XATTRD)

TEST_BIG_ENDIAN(BIGENDIAN)
if(${BIGENDIAN})
  set(BIGEND ON)
else()
  set(LITTLEEND ON)
endif(${BIGENDIAN})

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
    set(KRB5_VERSION 194)  # hand code until we do krb5-config --version magic
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

if (USE_ADMIN_TOOLS)
  find_package(PythonInterp)
  if (NOT PYTHONINTERP_FOUND)
    message(FATAL_ERROR "Cannot find python for enabling admin tools")
  endif (NOT PYTHONINTERP_FOUND)
  if (USE_GUI_ADMIN_TOOLS)
    find_program(PYUIC NAMES pyuic4 DOC "PyQT UI-compiler executable")
    if (NOT PYUIC)
      message(STATUS "Cannot find PyQt4.  Disabling GUI admin tools")
      set(USE_GUI_ADMIN_TOOLS OFF)
    endif (NOT PYUIC)
  endif (USE_GUI_ADMIN_TOOLS)
endif (USE_ADMIN_TOOLS)

if (USE_MAN_PAGE)
  find_program(SPHINX_BUILD sphinx-build)
  if(NOT SPHINX_BUILD)
    message(FATAL_ERROR "Can't find sphinx-build.")
  endif(NOT SPHINX_BUILD)
endif(USE_MAN_PAGE)

# Validate fsal dependencies

if(USE_FSAL_GLUSTER)
  find_package(PkgConfig)
  IF(GLUSTER_PREFIX)
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}:${GLUSTER_PREFIX}/lib/pkgconfig")
  ENDIF(GLUSTER_PREFIX)
  pkg_check_modules(GFAPI glusterfs-api>=7.3.10.7)
  if(NOT GFAPI_FOUND)
    if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT PACKAGE: Cannot find GLUSTER GFAPI runtime. Disabling GLUSTER fsal build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find GLUSTER GFAPI runtime. Disabling GLUSTER fsal build")
      set(USE_FSAL_GLUSTER OFF)
    endif(STRICT_PACKAGE)
  else(NOT GFAPI_FOUND)
    message(STATUS "GFAPI_INCLUDE_DIRS=${GFAPI_INCLUDE_DIRS}")
    message(STATUS "GFAPI_LIBRARY_DIRS=${GFAPI_LIBRARY_DIRS}")
    message(STATUS "GFAPI_LIBDIR=${GFAPI_LIBDIR}")
    include_directories(${GFAPI_INCLUDE_DIRS})
    # missing directory not provided by current version of GlusterFS
    include_directories(${GFAPI_PREFIX}/include)
    link_directories (${GFAPI_LIBRARY_DIRS})
  endif(NOT GFAPI_FOUND)

  if(USE_FSAL_GLUSTER)
    check_include_files("unistd.h;sys/xattr.h" HAVE_XATTR_H)
    if(NOT HAVE_XATTR_H)
      if(STRICT_PACKAGE)
        message(FATAL_ERROR "STRICT PACKAGE: Can not find sys/xattr.h, disabling GLUSTER fsal build")
      else(STRICT_PACKAGE)
        message(WARNING "Can not find sys/xattr.h, disabling GLUSTER fsal build")
        set(USE_FSAL_GLUSTER OFF)
      endif(STRICT_PACKAGE)
    endif(NOT HAVE_XATTR_H)
    check_include_files("acl/libacl.h" HAVE_ACL_H)
    if(HAVE_ACL_H)
	set(USE_POSIX_ACLS ON)
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
  endif(USE_FSAL_GLUSTER)
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_CEPH)
  find_package(CephFS)
  if(NOT CEPHFS_FOUND)
    if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE : Cannot find CEPH runtime.  Disabling CEPH fsal build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find CEPH runtime.  Disabling CEPH fsal build")
      set(USE_FSAL_CEPH OFF)
    endif(STRICT_PACKAGE)
  endif(NOT CEPHFS_FOUND)
endif(USE_FSAL_CEPH)

if(USE_FSAL_RGW)
  # require RGW w/API version 1.1.x
  find_package(RGW 1.1.6)
  if(NOT RGW_FOUND)
    if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE : Cannot find RGW runtime.  Disabling RGW fsal build")
    else(STRICT_PACKAGE)
      message(WARNING
	"Cannot find supported RGW runtime.  Disabling RGW fsal build")
      set(USE_FSAL_RGW OFF)
    endif(STRICT_PACKAGE)
  endif(NOT RGW_FOUND)
endif(USE_FSAL_RGW)

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
   if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE: Cannot find XFS runtime. Disabling XFS build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find XFS runtime. Disabling XFS build")
      set(USE_FSAL_XFS OFF)
    endif(STRICT_PACKAGE)
  endif((NOT HAVE_XFS_LIB) OR (NOT HAVE_XFS_H))
endif(USE_FSAL_XFS)

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
if(USE_EFENCE)
  find_package(efence REQUIRED)
  set(SYSTEM_LIBRARIES ${efence_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_EFENCE)

if(USE_DBUS)
  find_package(PkgConfig)
  pkg_check_modules(DBUS REQUIRED dbus-1)
  set(SYSTEM_LIBRARIES ${DBUS_LIBRARIES} ${SYSTEM_LIBRARIES})
  LIST(APPEND CMAKE_LIBRARY_PATH ${DBUS_LIBRARY_DIRS})
  link_directories (${DBUS_LIBRARY_DIRS})
endif(USE_DBUS)

if(USE_NFSIDMAP)
  find_package(NfsIdmap)
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

if(USE_CUNIT)
  find_package(cunit REQUIRED)
  set(SYSTEM_LIBRARIES ${cunit_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_CUNIT)

if(_MSPAC_SUPPORT)
  find_package(WBclient REQUIRED)
  if(WBCLIENT_FOUND AND WBCLIENT4_H)
    set(SYSTEM_LIBRARIES ${WBCLIENT_LIBRARIES} ${SYSTEM_LIBRARIES})
  else(WBCLIENT_FOUND AND WBCLIENT4_H)
    message(WARNING "Samba 4 wbclient not found.  Disabling MSPAC_SUPPORT")
    set(_MSPAC_SUPPORT OFF)
  endif(WBCLIENT_FOUND AND WBCLIENT4_H)
endif(_MSPAC_SUPPORT)

if(USE_LTTNG)
  # Set LTTNG_PATH_HINT on the command line
  # if your LTTng is not in a standard place
  find_package(LTTng)
  if(LTTNG_FOUND)
    include_directories(${LTTNG_INCLUDE_DIR})
  else(LTTNG_FOUND)
    message(WARNING "LTTng libraries not found. Disabling USE_LTTNG")
    set(USE_LTTNG OFF)
  endif(LTTNG_FOUND)
endif(USE_LTTNG)

if(USE_RADOS_RECOV OR RADOS_URLS)
  find_package(RADOS)
  if(NOT RADOS_FOUND)
    if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE : Rados libraries not found.")
    else(STRICT_PACKAGE)
      message(WARNING "Rados libraries not found.")
      set(USE_RADOS_RECOV OFF)
      set(RADOS_URLS OFF)
    endif(STRICT_PACKAGE)
  endif(NOT RADOS_FOUND)
endif(USE_RADOS_RECOV OR RADOS_URLS)

# Cmake 2.6 has issue in managing BISON and FLEX
if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8" )
   message( status "CMake 2.6 detected, using portability hooks" )
   set(CMAKE_CURRENT_LIST_DIR /usr/share/cmake/Modules )
   set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/portability_cmake_2.8  /usr/share/cmake/Modules ${CMAKE_MODULE_PATH})
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

# Fixup loose bits of autotools legacy
set(_USE_9P ${USE_9P})
set(_USE_9P_RDMA ${USE_9P_RDMA})
set(_USE_NFS3 ${USE_NFS3})
set(_USE_NLM ${USE_NLM})

if(USE_CB_SIMULATOR)
  set(_USE_CB_SIMULATOR ON)
endif(USE_CB_SIMULATOR)

########### add a "make dist" and a "make rpm"  ###############

set( PKG_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}.tar.gz")
add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

# Find misc system libs
find_library(LIBRT rt)  # extended Pthreads functions

# We need to have libcap installed
find_library(LIBCAP cap)  # Management of Capablilities
check_library_exists(
	cap
	cap_set_proc
	""
	HAVE_LIBCAP
	)

if(HAVE_LIBCAP)
  set(SYSTEM_LIBRARIES ${SYSTEM_LIBRARIES} ${LIBCAP})
  set(USE_CAPS ON)
else(HAVE_LIBCAP)
  set(USE_CAPS OFF)
  message(STATUS "Could not find capabilities library, disabling USE_CAPS")
endif(HAVE_LIBCAP)

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

# Roll up required libraries

#Protocols we support
set(PROTOCOLS
  nfsproto
  rquota
)

if(USE_NLM)
  set(PROTOCOLS ${PROTOCOLS} nlm)
endif(USE_NLM)

if(USE_9P)
  set(PROTOCOLS ${PROTOCOLS} 9p)
endif(USE_9P)

set(PROTOCOLS
  ${PROTOCOLS}
  nfs_mnt_xdr
  )

# Core subsystems
set(GANESHA_CORE
  sal
  idmap
  avltree
  hashtable
  rpcal
  support
  nfs4callbacks
  cidr
  string_utils
  hash
  log
  uid2grp
  netgroup_cache
  fsalpseudo
  fsalmdcache
  )

if(USE_CACHE_INODE)
  set(GANESHA_CORE ${GANESHA_CORE} cache_inode)
endif(USE_CACHE_INODE)

if(USE_DBUS)
  set(GANESHA_CORE ${GANESHA_CORE} gshdbus)
endif(USE_DBUS)

if(USE_9P_RDMA)
  find_package(PkgConfig)
  IF(MOOSHIKA_PREFIX)
    set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH}:${MOOSHIKA_PREFIX}/lib/pkgconfig")
  ENDIF()
  pkg_check_modules(MOOSHIKA REQUIRED libmooshika>=0.6)
  # Remove rdma and ibverbs libraries that have already been found by FindRDMA
  #
  # TODO: this temporary workaround will no longer be required
  # with future mooshika versions (>1.0)
  # Original code is: set(GANESHA_CORE ${GANESHA_CORE} ${MOOSHIKA_LIBRARIES})
  #
  set(MOOSHIKA_LIBS_EXCEPT_RDMA ${MOOSHIKA_LIBRARIES})
  list(REMOVE_ITEM MOOSHIKA_LIBS_EXCEPT_RDMA rdmacm ibverbs)
  set(GANESHA_CORE ${GANESHA_CORE} ${MOOSHIKA_LIBS_EXCEPT_RDMA})

  include_directories(${MOOSHIKA_INCLUDE_DIRS})
  link_directories (${MOOSHIKA_LIBRARY_DIRS})
endif(USE_9P_RDMA)

set(NTIRPC_MIN_VERSION 1.6.1)
if (USE_SYSTEM_NTIRPC)
  find_package(NTIRPC ${NTIRPC_MIN_VERSION} REQUIRED)
else (USE_SYSTEM_NTIRPC)
  # Set options for submodule
  set(USE_RPC_RDMA ${USE_NFS_RDMA} CACHE BOOL "Use RDMA")
  set(TIRPC_EPOLL ${TIRPC_EPOLL} CACHE BOOL "Use EPOLL")
  set(USE_GSS ${USE_GSS} CACHE BOOL "Use GSS")
  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
	  "${CMAKE_SOURCE_DIR}/libntirpc/cmake/modules/")
  add_subdirectory(libntirpc)
  set(NTIRPC_LIBRARY ntirpc)
  set(NTIRPC_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/libntirpc/ntirpc/")
  message(STATUS "Using ntirpc submodule")
endif (USE_SYSTEM_NTIRPC)
message(${NTIRPC_INCLUDE_DIR})
include_directories(${NTIRPC_INCLUDE_DIR})

# All the plumbing in the basement
set(SYSTEM_LIBRARIES
  ${SYSTEM_LIBRARIES}
  gos
  ${LIBDL}
  ${KRB5_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBRT}
  ${NTIRPC_LIBRARY}
)

# Config file; make sure it doesn't clobber an existing one
include(${CMAKE_SOURCE_DIR}/cmake/modules/InstallPackageConfigFile.cmake)
InstallPackageConfigFile(${CMAKE_SOURCE_DIR}/config_samples/ganesha.conf.example ${SYSCONFDIR}/ganesha ganesha.conf)
# Sample config files
if( ${DISTRO} MATCHES "SLES" )
  install(DIRECTORY config_samples DESTINATION share/doc/packages/ganesha)
else( ${DISTRO} MATCHES "SLES" )
  install(DIRECTORY config_samples DESTINATION share/doc/ganesha)
endif( ${DISTRO} MATCHES "SLES" )
# pre-create PREFIX/var/run/ganesha
install(DIRECTORY DESTINATION ${SYSSTATEDIR}/run/ganesha)

add_subdirectory(log)
add_subdirectory(config_parsing)
add_subdirectory(cidr)
add_subdirectory(test)
add_subdirectory(avl)
add_subdirectory(hashtable)
if(USE_CACHE_INODE)
  add_subdirectory(cache_inode)
endif(USE_CACHE_INODE)
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
message(STATUS "USE_FSAL_PROXY = ${USE_FSAL_PROXY}")
message(STATUS "USE_FSAL_VFS = ${USE_FSAL_VFS}")
message(STATUS "USE_FSAL_CEPH = ${USE_FSAL_CEPH}")
message(STATUS "USE_FSAL_CEPH_MKNOD = ${USE_FSAL_CEPH_MKNOD}")
message(STATUS "USE_FSAL_CEPH_SETLK = ${USE_FSAL_CEPH_SETLK}")
message(STATUS "USE_FSAL_CEPH_LL_LOOKUP_ROOT = ${USE_FSAL_CEPH_LL_LOOKUP_ROOT}")
message(STATUS "USE_FSAL_CEPH_STATX = ${USE_FSAL_CEPH_STATX}")
message(STATUS "USE_FSAL_CEPH_LL_DELEGATION = ${USE_FSAL_CEPH_LL_DELEGATION}")
message(STATUS "USE_FSAL_CEPH_LL_SYNC_INODE = ${USE_FSAL_CEPH_LL_SYNC_INODE}")
message(STATUS "USE_FSAL_RGW = ${USE_FSAL_RGW}")
message(STATUS "USE_FSAL_XFS = ${USE_FSAL_XFS}")
message(STATUS "USE_FSAL_PANFS = ${USE_FSAL_PANFS}")
message(STATUS "USE_FSAL_GPFS = ${USE_FSAL_GPFS}")
message(STATUS "USE_FSAL_GLUSTER = ${USE_FSAL_GLUSTER}")
message(STATUS "USE_FSAL_NULL = ${USE_FSAL_NULL}")
message(STATUS "USE_FSAL_MEM = ${USE_FSAL_MEM}")
message(STATUS "USE_SYSTEM_NTIRPC = ${USE_SYSTEM_NTIRPC}")
message(STATUS "USE_DBUS = ${USE_DBUS}")
message(STATUS "USE_CB_SIMULATOR = ${USE_CB_SIMULATOR}")
message(STATUS "USE_NFSIDMAP = ${USE_NFSIDMAP}")
message(STATUS "ENABLE_ERROR_INJECTION = ${ENABLE_ERROR_INJECTION}")
message(STATUS "ENABLE_VFS_DEBUG_ACL = ${ENABLE_VFS_DEBUG_ACL}")
message(STATUS "ENABLE_RFC_ACL = ${ENABLE_RFC_ACL}")
message(STATUS "USE_CAPS = ${USE_CAPS}")
message(STATUS "USE_BLKID = ${USE_BLKID}")
message(STATUS "STRICT_PACKAGE = ${STRICT_PACKAGE}")
message(STATUS "DISTNAME_HAS_GIT_DATA = ${DISTNAME_HAS_GIT_DATA}" )
message(STATUS "_MSPAC_SUPPORT = ${_MSPAC_SUPPORT}")
message(STATUS "USE_EFENCE = ${USE_EFENCE}")
message(STATUS "_NO_TCP_REGISTER = ${_NO_TCP_REGISTER}")
message(STATUS "RPCBIND = ${RPCBIND}")
message(STATUS "_NO_XATTRD = ${_NO_XATTRD}")
message(STATUS "DEBUG_SAL = ${DEBUG_SAL}")
message(STATUS "_VALGRIND_MEMCHECK = ${_VALGRIND_MEMCHECK}")
message(STATUS "PROXY_HANDLE_MAPPING = ${PROXY_HANDLE_MAPPING}")
message(STATUS "DEBUG_SYMS = ${DEBUG_SYMS}")
message(STATUS "COVERAGE = ${COVERAGE}")
message(STATUS "ENFORCE_GCC = ${ENFORCE_GCC}")
message(STATUS "USE_GTEST = ${USE_GTEST}")
message(STATUS "GTEST_PREFIX = ${GTEST_PREFIX}")
message(STATUS "GTEST_MAIN = ${GTEST_MAIN}")
message(STATUS "PROFILING = ${PROFILING}")
message(STATUS "USE_GSS = ${USE_GSS}")
message(STATUS "TIRPC_EPOLL = ${TIRPC_EPOLL}")
message(STATUS "USE_9P = ${USE_9P}")
message(STATUS "_USE_9P = ${_USE_9P}")
message(STATUS "_USE_9P_RDMA = ${_USE_9P_RDMA}")
message(STATUS "USE_NFS_RDMA = ${USE_NFS_RDMA}")
message(STATUS "USE_NFS3 = ${USE_NFS3}")
message(STATUS "USE_NLM = ${USE_NLM}")
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
message(STATUS "USE_TSAN = ${USE_TSAN}")
message(STATUS "USE_LTTNG = ${USE_LTTNG}")
message(STATUS "USE_BLKIN = ${USE_BLKIN}")
message(STATUS "USE_VSOCK = ${USE_VSOCK}")
message(STATUS "USE_TOOL_MULTILOCK = ${USE_TOOL_MULTILOCK}")
message(STATUS "USE_MAN_PAGE = ${USE_MAN_PAGE}")
message(STATUS "USE_RADOS_RECOV = ${USE_RADOS_RECOV}")
message(STATUS "RADOS_URLS = ${RADOS_URLS}")

#force command line options to be stored in cache
set(USE_FSAL_VFS ${USE_FSAL_VFS}
  CACHE BOOL
   "build VFS FSAL shared library"
   FORCE)

set(USE_FSAL_PROXY ${USE_FSAL_PROXY}
  CACHE BOOL
   "build PROXY FSAL shared library"
   FORCE)

set(USE_FSAL_CEPH ${USE_FSAL_CEPH}
  CACHE BOOL
   "build CEPH FSAL shared library"
   FORCE)

 set(USE_FSAL_RGW ${USE_FSAL_RGW}
  CACHE BOOL
   "build RGW FSAL shared library"
   FORCE)

set(USE_FSAL_XFS ${USE_FSAL_XFS}
  CACHE BOOL
   "build XFS FSAL"
   FORCE)

set(USE_FSAL_PANFS ${USE_FSAL_PANFS}
  CACHE BOOL
   "build PanFS FSAL"
   FORCE)

set(USE_FSAL_GPFS ${USE_FSAL_GPFS}
  CACHE BOOL
   "build GPFS FSAL"
   FORCE)

set(USE_FSAL_GLUSTER ${USE_FSAL_GLUSTER}
  CACHE BOOL
   "build GLUSTER FSAL"
   FORCE)

set(USE_FSAL_MEM ${USE_FSAL_MEM}
  CACHE BOOL
   "build GLUSTER FSAL"
   FORCE)

set(USE_DBUS ${USE_DBUS}
  CACHE BOOL
   "enable DBUS protocol support"
   FORCE)

set(USE_CB_SIMULATOR ${USE_CB_SIMULATOR}
  CACHE BOOL
   "enable callback simulator thread"
   FORCE)

set(USE_NFSIDMAP ${USE_NFSIDMAP}
  CACHE BOOL
   "Use of libnfsidmap for name resolution"
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

set(_MSPAC_SUPPORT ${_MSPAC_SUPPORT}
  CACHE BOOL
   "enable mspac winbind support"
   FORCE)

set(STRICT_PACKAGE ${STRICT_PACKAGE}
  CACHE BOOL
   "enable strict packaging behavior"
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

set(USE_ADMIN_TOOLS ${USE_ADMIN_TOOLS}
  CACHE BOOL
  "Package Admin Scripts"
  FORCE)

set(USE_GUI_ADMIN_TOOLS ${USE_GUI_ADMIN_TOOLS}
  CACHE BOOL
  "Package GUI Admin Scripts"
  FORCE)

set(USE_TSAN ${USE_TSAN}
  CACHE BOOL
  "Enable GCC Thread-Sanitizer"
  FORCE)

set(USE_LTTNG ${USE_LTTNG}
  CACHE BOOL
  "Enable LTTng tracing"
  FORCE)

set(USE_NFS_RDMA ${USE_NFS_RDMA}
  CACHE BOOL
  "enable nfs RDMA"
  FORCE)

set(_USE_NFS_RDMA ${USE_NFS_RDMA}
  CACHE BOOL
  "enable nfs RDMA in config"
  FORCE)

set(USE_RADOS_RECOV ${USE_RADOS_RECOV}
  CACHE BOOL
  "enable rados recovery"
  FORCE)

set(RADOS_URLS ${RADOS_URLS}
  CACHE BOOL
  "enable rados url provider"
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

if(USE_FSAL_PANFS)
    set(BCOND_PANFS "%bcond_without")
else(USE_FSAL_PANFS)
    set(BCOND_PANFS "%bcond_with")
endif(USE_FSAL_PANFS)

if(USE_FSAL_GPFS)
    set(BCOND_GPFS "%bcond_without")
else(USE_FSAL_GPFS)
    set(BCOND_GPFS "%bcond_with")
endif(USE_FSAL_GPFS)

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

if(USE_9P_RDMA)
    set(BCOND_RDMA "%bcond_without")
else(USE_9P_RDMA)
    set(BCOND_RDMA "%bcond_with")
endif(USE_9P_RDMA)

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

# Now create a useable specfile
configure_file(
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec-in.cmake"
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec"
)

configure_file(
  "${PROJECT_SOURCE_DIR}/scripts/systemd/nfs-ganesha-config.service-in.cmake"
  "${PROJECT_SOURCE_DIR}/scripts/systemd/nfs-ganesha-config.service"
)

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

########### add a "make doc" target to call Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
		 ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif(DOXYGEN_FOUND)

# Include thread-sanitizer module
include(${CMAKE_SOURCE_DIR}/cmake/tsan.cmake)
