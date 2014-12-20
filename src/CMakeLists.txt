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

project(nfs-ganesha C)
# Project versioning
set(GANESHA_MAJOR_VERSION 2)
set(GANESHA_MINOR_VERSION 2)

# needs to come after project()
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix for common files" FORCE)
	message(STATUS "override default CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
	SET(SYSCONFDIR "/etc" CACHE PATH "Install prefix for common files" FORCE)
	SET(SYSSTATEDIR "/var" CACHE PATH "Install prefix for common files" FORCE)
ELSE()
	message(STATUS "was set CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
	SET(SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc" CACHE PATH "Install prefix for common files" FORCE)
	SET(SYSSTATEDIR "${CMAKE_INSTALL_PREFIX}/var" CACHE PATH "Install prefix for common files" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
# Patch level is always ".0" for mainline (master).  It is blank for development.
# When starting a stable maintenance branch, this becomes ".N"
# where N is monotonically increasing starting at 1. Remember to include the "." !!
set(GANESHA_PATCH_LEVEL )

# Extra version is for naming development/RC.  It is blank in master/stable branches
# so it can be available to end-users to name local variants/versions
# If used, it is always of the form "-whateveryouwant"
set(GANESHA_EXTRA_VERSION -dev-21)

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
  set(DISTRO "NOT_RH")
 EXECUTE_PROCESS(
      COMMAND cat /etc/system-release
      OUTPUT_VARIABLE SYS_RELEASE
      ERROR_QUIET
   )
  if( ${SYS_RELEASE} MATCHES "Red Hat" )
     message( STATUS "Detected a Linux Red Hat machine" )
     set(DISTRO "RED_HAT")
  elseif( ${SYS_RELEASE} MATCHES "Fedora" )
     message( STATUS "Detected a Linux Fedora machine" )
     set(DISTRO "FEDORA")
  else( ${SYS_RELEASE} MATCHES "Red Hat" )
     message( STATUS "Detected a non Red hat based" )
     set(DISTRO "NOT_RH")
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

# Add maintainer mode for (mainly) strict builds
include(${CMAKE_SOURCE_DIR}/cmake/maintainer_mode.cmake)

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

# Using git related information, build PACKNAME
set( PACKNAME "${GANESHA_VERSION}" )

# Define CPACK component (to deal with sub packages)
set(CPACK_COMPONENTS_ALL daemon fsal headers )
set(CPACK_COMPONENT_DAEMON_DISPLAY_NAME "NFS-Ganesha daemon")

# Include custom config and cpack module
include(${CMAKE_SOURCE_DIR}/cmake/cpack_config.cmake)
include(CPack)

if (FREEBSD)
#default gcc doesn't like using -Wuninitialized without -O on FreeBSD
    set(PLATFORM "FREEBSD")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O -ggdb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -rdynamic")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/freebsd")
    find_library(LIBDL c)  # libc suffices on freebsd
endif(FREEBSD)

if (LINUX)
    set(PLATFORM "LINUX")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_LARGEFILE64_SOURCE")
    set(OS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include/os/linux")
    find_library(LIBDL dl)  # module loader
endif(LINUX)

if (MSVC)
   add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif(MSVC)

IF(FSAL_DESTINATION)
    set( FSAL_DESTINATION ${FSAL_DESTINATION} )
ELSE()
    # Library destination, 32 bits / 64 bits switch is done here
    if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        set( FSAL_DESTINATION "lib64/ganesha" )
    else( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        set( FSAL_DESTINATION "lib/ganesha" )
    endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
ENDIF()

# FSAL selection
# FSALs which are enabled by default but could be disabled
# during the build
option(USE_FSAL_PROXY "build PROXY FSAL shared library" ON)
option(USE_FSAL_VFS "build VFS FSAL shared library" ON)
option(USE_FSAL_CEPH "build CEPH FSAL shared library" ON)
option(USE_FSAL_GPFS "build GPFS FSAL" ON)
option(USE_FSAL_ZFS "build ZFS FSAL" ON)
option(USE_FSAL_LUSTRE "build LUSTRE FSAL" ON)
option(USE_FSAL_XFS "build XFS support in VFS FSAL" ON)
option(USE_FSAL_PANFS "build PanFS support in VFS FSAL" ON)
option(USE_FSAL_GLUSTER "build GLUSTER FSAL shared library" ON)

# FSALs which are disabled by default
option(USE_FSAL_PT "build PT FSAL" OFF)
option(USE_FSAL_HPSS "build HPSS FSAL" OFF)

# SHOOK is a variant of Lustre FSAL
option(USE_FSAL_SHOOK "build SHOOK FSAL" ON)

# Activate FSAL Upcalls for FSAL_LUSTRE
option(USE_FSAL_LUSTRE_UP "add Upcalls support for LUSTRE FSAL" OFF)

# From TI-RPC
option (USE_GSS "enable RPCSEC_GSS support" ON)
option(TIRPC_EPOLL "platform supports EPOLL or emulation" ON)

# Build configure options
option(USE_DBUS "enable DBUS protocol support" OFF)

# Various DBUS enabled features
option(USE_CB_SIMULATOR "enable callback simulator thread" OFF)

option(USE_TIRPC_IPV6 "enable IPv6 support (via IPv6)" OFF)
option(USE_NFSIDMAP "Use of libnfsidmap for name resolution" ON)
option(ENABLE_ERROR_INJECTION "enable error injection" OFF)

# Electric Fence (-lefence) link flag
option(USE_EFENCE "link with efence memory debug library" OFF)

# These are -D_FOO options, why ???  should be flags??
option(_NO_TCP_REGISTER "disable registration of tcp services on portmapper" ON)
option(_NO_PORTMAPPER "disable registration on portmapper" ON)
option(_NO_XATTRD "disable ghost xattr directory and files support" ON)
option(DEBUG_SAL "enable debugging of SAL by keeping list of all locks, stateids, and state owners" OFF)
option(PROXY_HANDLE_MAPPING "enable NFSv3 handle mapping for PROXY FSAL" OFF)

# Debug symbols (-g) build flag
option(DEBUG_SYMS "include debug symbols to binaries (-g option)" OFF)

# Add coverage information to build tree
option(COVERAGE "add flag to generate coverage data at runtime" OFF)

# enable code profiling [-g -pg]
option(PROFILING "turn on code profiling (-g and -pg)" OFF)

# MSPAC support -lwbclient link flag
if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  option(_MSPAC_SUPPORT "enable mspac Winbind support" ON)
else( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )
  option(_MSPAC_SUPPORT "enable mspac Winbind support" OFF)
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_GREATER "2.6" )

# CUnit
option(USE_CUNIT "Use Cunit test framework" OFF)

# NFS RDMA
option(USE_NFS_MSK "enable NFS/MSK support" OFF)

# Enable 9P Support
option(USE_9P "enable 9P support" ON)
option(USE_9P_RDMA "enable 9P_RDMA support" OFF)

# This option will stop cmake compilation if a requested FSAL could not be built
option(STRICT_PACKAGE "Enable strict packaging behavior" OFF )

# This option will trigger "long distro name" aka name that contains git information
option(DISTNAME_HAS_GIT_DATA "Distribution package's name carries git data" OFF )

# Build and package Python admin scripts for managing via DBus
option(USE_ADMIN_TOOLS "Package Admin scripts" OFF)

# Final location for server modules such as FSAL shared objects
# We do this separately because packaging (RPM) installs in a
# "build root" but installs somewhere else (/usr)
set(MODULES_PATH ${CMAKE_INSTALL_PREFIX}
  CACHE PATH
  "Path prefix for dlopen loaded modules")

# Enable GCC Thread-sanitizer
option(USE_TSAN "Enable GCC Thread-Sanitizer" OFF)

# Enable LTTng tracing
option(USE_LTTNG "Enable LTTng tracing" OFF)

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

# Build source locations and parameters
IF(USE_FSAL_CEPH)
  IF(CEPH_PREFIX)
    set(CEPH_PREFIX ${CEPH_PREFIX} CACHE PATH "Path to Ceph installation")
    LIST(APPEND CMAKE_PREFIX_PATH "${CEPH_PREFIX}")
    LIST(APPEND CMAKE_LIBRARY_PATH "${CEPH_PREFIX}/lib")
    LIST(APPEND CMAKE_REQUIRED_INCLUDES "${CEPH_PREFIX}/include")
  ELSE()
    set(CEPH_PREFIX "/usr" CACHE PATH "Path to Ceph installation")
  ENDIF()
ENDIF()

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

IF(USE_FSAL_LUSTRE)
  IF(LUSTRE_PREFIX)
    set(LUSTRE_PREFIX ${LUSTRE_PREFIX} CACHE PATH "Path to Lustre installation")
    LIST(APPEND CMAKE_PREFIX_PATH "${LUSTRE_PREFIX}")
    if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        LIST(APPEND CMAKE_LIBRARY_PATH "${LUSTRE_PREFIX}/lib64")
    else( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        LIST(APPEND CMAKE_LIBRARY_PATH "${LUSTRE_PREFIX}/lib")
    endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    LIST(APPEND CMAKE_REQUIRED_INCLUDES "${LUSTRE_PREFIX}/include")
  ELSE()
    set(LUSTRE_PREFIX "/usr" CACHE PATH "Path to Lustre installation")
  ENDIF()
ENDIF()

IF(USE_FSAL_HPSS)
  IF(HPSS_PREFIX)
    set(HPSS_PREFIX ${HPSS_PREFIX} CACHE PATH "Path to HPSS installation")
  ELSE()
    set(HPSS_PREFIX "/usr" CACHE PATH "Path to HPSS installation")
  ENDIF()

  # If you are not using hpss with krb5, remove the hpsskrb5auth here.
  # It shouldn't be needed, but dlopen() of a dlopen() seems not to work as well as one properly linked...
  find_library(HPSS_LIBRARIES
    NAMES hpss PATHS "${HPSS_PREFIX}/lib")
  find_library(HPSSKRB5_LIBRARY
    NAMES hpsskrb5auth PATHS "${HPSS_PREFIX}/lib")
  LIST(APPEND HPSS_LIBRARIES "${HPSSKRB5_LIBRARY}")
  if(NOT HPSS_LIBRARIES)
    message(WARNING "Cannot find HPSS libraries. Disabling HPSS fsal build")
    set(USE_FSAL_HPSS OFF)
  endif(NOT HPSS_LIBRARIES)
endif(USE_FSAL_HPSS)

IF(USE_FSAL_ZFS)
  IF(ZFS_PREFIX)
    set(ZFS_PREFIX ${ZFS_PREFIX} CACHE PATH "Path to ZFS installation")
    LIST(APPEND CMAKE_PREFIX_PATH "${ZFS_PREFIX}")
    LIST(APPEND CMAKE_LIBRARY_PATH "${ZFS_PREFIX}/lib")
    LIST(APPEND CMAKE_LIBRARY_PATH "${ZFS_PREFIX}/local/lib")
    LIST(APPEND CMAKE_LIBRARY_PATH "${ZFS_PREFIX}/local/lib64")
    LIST(APPEND CMAKE_REQUIRED_INCLUDES "${ZFS_PREFIX}/include")
  ELSE()
    set(ZFS_PREFIX "/usr" CACHE PATH "Path to ZFS installation")
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
IF(LIBIBVERBS_PREFIX)
  set(LIBIBVERBS_PREFIX ${LIBIBVERBS_PREFIX} CACHE PATH "Path to libibverbs installation")
  LIST(APPEND CMAKE_REQUIRED_INCLUDES "${LIBIBVERBS_PREFIX}/include")
  include_directories("${LIBIBVERBS_PREFIX}/usr/include")
ENDIF()
IF(LIBRDMACM_PREFIX)
  set(LIBRDMACM_PREFIX ${LIBRDMACM_PREFIX} CACHE PATH "Path to librdmacm installation")
  LIST(APPEND CMAKE_REQUIRED_INCLUDES "${LIBRDMACM_PREFIX}/include")
  include_directories("${LIBRDMACM_PREFIX}/include")
ENDIF()



if(USE_CB_SIMULATOR AND NOT USE_DBUS)
  message(WARNING "The callback simulator needs DBUS.  Enabling DBUS")
  set(USE_DBUS ON)
endif(USE_CB_SIMULATOR AND NOT USE_DBUS)

if(USE_TIRPC_IPV6)
  set(_USE_TIRPC_IPV6 ON)
endif(USE_TIRPC_IPV6)

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
  check_include_files(sqlite3.h, HAVE_SQLITE3_H)
  check_library_exists(
    sqlite3
    sqlite3_open
    ""
    HAVE_SQLITE3
    )
  if(NOT HAVE_SQLITE3 OR HAVE_SQLITE3_H)
    message(WARNING "Cannot find sqlite3.h or the library. Disabling proxy handle mapping")
    set(PROXY_HANDLE_MAPPING OFF)
  endif(NOT HAVE_SQLITE3 OR HAVE_SQLITE3_H)
ENDIF(PROXY_HANDLE_MAPPING)

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

find_package(Krb5 REQUIRED gssapi)
check_include_files(gssapi.h HAVE_GSSAPI_H)

if(KRB5_FOUND AND HAVE_GSSAPI_H)
  set(HAVE_KRB5 ON)
  set(KRB5_VERSION 194)  # hand code until we do krb5-config --version magic
  set(_HAVE_GSSAPI ON)
else(KRB5_FOUND AND HAVE_GSSAPI_H)
  message(FATAL_ERROR "Cannot find kerberos or GSS libraries")
endif(KRB5_FOUND AND HAVE_GSSAPI_H)

# Validate fsal dependencies

if(USE_FSAL_GLUSTER)
  find_library(HAVE_GFAPI gfapi)
  check_library_exists(
    gfapi
    glfs_h_lookupat
    ""
    HAVE_GFAPI
    )
  check_include_files("glusterfs/api/glfs.h" HAVE_GLUSTER_H)
  if((NOT HAVE_GFAPI) OR (NOT HAVE_GLUSTER_H))
    if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT PACKAGE: Cannot find GLUSTER GFAPI runtime. Disabling GLUSTER fsal build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find GLUSTER GFAPI runtime. Disabling GLUSTER fsal build")
      set(USE_FSAL_GLUSTER OFF)
    endif(STRICT_PACKAGE)
  endif((NOT HAVE_GFAPI) OR (NOT HAVE_GLUSTER_H))
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_CEPH)
  find_library(CEPH_FS cephfs)
  check_library_exists(
    cephfs
    ceph_ll_connectable_x
    ${CEPH_PREFIX}
    CEPH_FS
    )
  if(NOT CEPH_FS)
if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE : Cannot find CEPH runtime.  Disabling CEPH fsal build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find CEPH runtime.  Disabling CEPH fsal build")
      set(USE_FSAL_CEPH OFF)
    endif(STRICT_PACKAGE)
 endif(NOT CEPH_FS)
endif(USE_FSAL_CEPH)

if(USE_FSAL_LUSTRE)
  find_library(HAVE_LUSTREAPI lustreapi)
  check_library_exists(
    lustreapi
    llapi_is_lustre_mnt
    ""
    HAVE_LUSTREAPI
    )
  check_include_files("unistd.h;lustre/lustre_user.h" HAVE_LUSTRE_USER_H)
  if((NOT HAVE_LUSTREAPI) OR (NOT HAVE_LUSTRE_USER_H))
   if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE: Cannot find LUSTRE runtime. Disabling LUSTRE fsal build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find LUSTRE runtime. Disabling LUSTRE fsal build")
      set(USE_FSAL_LUSTRE OFF)
      set(USE_FSAL_SHOOK OFF)
    endif(STRICT_PACKAGE)
  else((NOT HAVE_LUSTREAPI) OR (NOT HAVE_LUSTRE_USER_H))
    include_directories("${LUSTRE_PREFIX}/include")
    if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        link_directories("${LUSTRE_PREFIX}/lib64")
    else( CMAKE_SIZEOF_VOID_P EQUAL 8 )
        link_directories("${LUSTRE_PREFIX}/lib")
    endif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
  endif((NOT HAVE_LUSTREAPI) OR (NOT HAVE_LUSTRE_USER_H))

  check_include_files("lustre/lustreapi.h" HAVE_INCLUDE_LUSTREAPI_H)
  check_include_files("lustre/liblustreapi.h" HAVE_INCLUDE_LIBLUSTREAPI_H)

  if( ( NOT HAVE_INCLUDE_LUSTREAPI_H ) AND ( NOT HAVE_INCLUDE_LIBLUSTREAPI_H ) )
    message( ERROR "Cannot find lustre header files, aborting" )
  endif( ( NOT HAVE_INCLUDE_LUSTREAPI_H ) AND ( NOT HAVE_INCLUDE_LIBLUSTREAPI_H ) )
else(USE_FSAL_LUSTRE)
  message(WARNING "Disabling SHOOK because it needs LUSTRE")
  set(USE_FSAL_SHOOK OFF)
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_LUSTRE AND USE_FSAL_SHOOK)
  check_library_exists(
    shooksvr
    shook_restore
    ""
    HAVE_SHOOK
    )
  check_include_files("unistd.h;attr/xattr.h" HAVE_SHOOK_XATTRS)
  if((NOT HAVE_SHOOK) OR (NOT HAVE_SHOOK_XATTRS))
   if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE: Cannot find SHOOK runtime. Disabling SHOOK build")
    else(STRICT_PACKAGE)
      message(WARNING "Cannot find SHOOK runtime. Disabling SHOOK build")
      set(USE_FSAL_SHOOK OFF)
    endif(STRICT_PACKAGE)
  endif((NOT HAVE_SHOOK) OR (NOT HAVE_SHOOK_XATTRS))
endif(USE_FSAL_LUSTRE AND USE_FSAL_SHOOK)

IF(USE_FSAL_LUSTRE_UP)
  find_library(HAVE_LCAP lcap)
  check_include_files("lcap_client.h" HAVE_LCAP_CLIENT_H)
  IF(NOT HAVE_LCAP OR NOT HAVE_LCAP_CLIENT_H)
    message(WARNING "liblcap is to be installed to use Lustre upcalls")
    set(USE_FSAL_LUSTRE_UP OFF)
  ENDIF(NOT HAVE_LCAP OR NOT HAVE_LCAP_CLIENT_H)
  IF(NOT HAVE_FSAL_LUSTRE)
    message(WARNING "You must trigger Lustre FSAL ON to use Lustre upcalls")
  ENDIF(NOT HAVE_FSAL_LUSTRE)
ENDIF(USE_FSAL_LUSTRE_UP)

if(USE_FSAL_XFS)
  check_library_exists(
    handle
    "open_by_handle"
    ""
    HAVE_XFS_LIB
    )
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

if(USE_FSAL_ZFS)
  check_library_exists(
    zfswrap
    libzfswrap_init
    ${ZFS_PREFIX}/lib
    HAVE_ZFS_LIB
    )
  check_include_files("unistd.h;libzfswrap.h" HAVE_LIBZFSWRAP_H)
  if((NOT HAVE_ZFS_LIB) OR (NOT HAVE_LIBZFSWRAP_H))
   if(STRICT_PACKAGE)
      message(FATAL_ERROR "STRICT_PACKAGE: Cannot find ZFS runtime. Disabling ZFS build")
   else(STRICT_PACKAGE)
      message(WARNING "Cannot find ZFS runtime. Disabling ZFS build")
      set(USE_FSAL_ZFS OFF)
    endif(STRICT_PACKAGE)
  endif((NOT HAVE_ZFS_LIB) OR (NOT HAVE_LIBZFSWRAP_H))
endif(USE_FSAL_ZFS)

# sort out which allocator to use
if(${ALLOCATOR} STREQUAL "jemalloc")
  find_package(JeMalloc)
  if(JEMALLOC_FOUND)
    set(SYSTEM_LIBRARIES ${JEMALLOC_LIBRARIES})
  else(JEMALLOC_FOUND)
    message(WARNING "jemalloc not found, falling back to libc")
    set(ALLOCATOR "libc")
  endif(JEMALLOC_FOUND)
elseif(${ALLOCATOR} STREQUAL "tcmalloc")
  find_package(TcMalloc)
  if(TCMALLOC_FOUND)
    set(SYSTEM_LIBRARIES ${TCMALLOC_LIBRARIES})
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
  else(LTTNG_FOUND)
    message(WARNING "LTTng libraries not found. Disabling USE_LTTNG")
    set(USE_LTTNG OFF)
  endif(LTTNG_FOUND)
endif(USE_LTTNG)

# Cmake 2.6 has issue in managing BISON and FLEX
if( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8" )
   message( status "CMake 2.6 detected, using portability hooks" )
   set(CMAKE_CURRENT_LIST_DIR /usr/share/cmake/Modules )
   set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/portability_cmake_2.8  /usr/share/cmake/Modules ${CMAKE_MODULE_PATH})
endif( "${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}" VERSION_LESS "2.8" )

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}/libntirpc/ntirpc/"
  "${PROJECT_SOURCE_DIR}/include"
  "${OS_INCLUDE_DIR}"
  "${KRB5_INCLUDE_DIRS}"
)

# Fixup loose bits of autotools legacy
set(_USE_9P ${USE_9P})
set(_USE_9P_RDMA ${USE_9P_RDMA})

if(USE_CB_SIMULATOR)
  set(_USE_CB_SIMULATOR ON)
endif(USE_CB_SIMULATOR)


#callers add:
#  target_link_libraries(your_library ${LIBTIRPC_LIBRARIES})
set(LIBTIRPC_INCLUDE_DIR
  "${PROJECT_BINARY_DIR}/libtirpc/ntirpc"
  "${PROJECT_BINARY_DIR}/libtirpc/ntirpc/rpc"
)
set(LIBTIRPC_LIBRARIES "${PROJECT_BINARY_DIR}/libntirpc/src/libntirpc.a")

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
  nlm
  rquota
)

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
  cache_inode
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
  fsalpseudo
  )

if(USE_DBUS)
  set(GANESHA_CORE ${GANESHA_CORE} gshdbus)
endif(USE_DBUS)

set(GANESHA_CORE ${GANESHA_CORE} NodeList)

if(USE_9P_RDMA OR USE_NFS_MSK)
  find_package(PkgConfig)
  pkg_check_modules(MOOSHIKA REQUIRED libmooshika>=0.6)
  set(GANESHA_CORE ${GANESHA_CORE} ${MOOSHIKA_LIBRARIES})
  include_directories(${MOOSHIKA_INCLUDE_DIRS})
  link_directories (${MOOSHIKA_LIBRARY_DIRS})
endif(USE_9P_RDMA OR USE_NFS_MSK)

# All the plumbing in the basement
set(SYSTEM_LIBRARIES
  ${SYSTEM_LIBRARIES}
  gos
  ${LIBDL}
  ${KRB5_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBRT}
)

# Config file; make sure it doesn't clobber an existing one
include(${CMAKE_SOURCE_DIR}/cmake/modules/InstallPackageConfigFile.cmake)
InstallPackageConfigFile(${CMAKE_SOURCE_DIR}/config_samples/vfs.conf ${SYSCONFDIR}/ganesha ganesha.conf)
# Sample config files
install(DIRECTORY config_samples DESTINATION share/doc/ganesha)
# pre-create PREFIX/var/run/ganesha
install(DIRECTORY DESTINATION ${SYSSTATEDIR}/run/ganesha)

IF( NOT EXISTS ${PROJECT_SOURCE_DIR}/libntirpc/CMakeLists.txt )
message( FATAL_ERROR
"No CMakeLists.txt found in libntirpc
This probably means that the directory was not properly populated.
Please run the following command to do that and use cmake again:
    git submodule update --init
Alternatively, if you are not using git, get the current version at
    https://github.com/nfs-ganesha/ntirpc" )
ENDIF( NOT EXISTS ${PROJECT_SOURCE_DIR}/libntirpc/CMakeLists.txt )

add_subdirectory(log)
add_subdirectory(config_parsing)
add_subdirectory(cidr)
add_subdirectory(test)
add_subdirectory(avl)
add_subdirectory(hashtable)
add_subdirectory(NodeList)
add_subdirectory(cache_inode)
add_subdirectory(SAL)
add_subdirectory(libntirpc)
add_subdirectory(RPCAL)
add_subdirectory(Protocols)
add_subdirectory(support)
add_subdirectory(os)

add_subdirectory(FSAL)
add_subdirectory(idmapper)

add_subdirectory(MainNFSD)
add_subdirectory(tools)

if(USE_DBUS)
  add_subdirectory(dbus)
endif(USE_DBUS)

if(USE_LTTNG)
  add_subdirectory(tracing)
endif(USE_LTTNG)

add_subdirectory(scripts)

# display configuration vars

message(STATUS)
message(STATUS "-------------------------------------------------------")
message(STATUS "PLATFORM = ${PLATFORM}")
message(STATUS "VERSION = ${GANESHA_VERSION}")
message(STATUS "BUILD HOST = ${BUILD_HOST_NAME}")
message(STATUS "-------------------------------------------------------")
message(STATUS "USE_FSAL_PROXY = ${USE_FSAL_PROXY}")
message(STATUS "USE_FSAL_VFS = ${USE_FSAL_VFS}")
message(STATUS "USE_FSAL_CEPH = ${USE_FSAL_CEPH}")
message(STATUS "USE_FSAL_HPSS = ${USE_FSAL_HPSS}")
message(STATUS "USE_FSAL_XFS = ${USE_FSAL_XFS}")
message(STATUS "USE_FSAL_PANFS = ${USE_FSAL_PANFS}")
message(STATUS "USE_FSAL_GPFS = ${USE_FSAL_GPFS}")
message(STATUS "USE_FSAL_PT = ${USE_FSAL_PT}")
message(STATUS "USE_FSAL_ZFS = ${USE_FSAL_ZFS}")
message(STATUS "USE_FSAL_LUSTRE = ${USE_FSAL_LUSTRE}")
message(STATUS "USE_FSAL_SHOOK = ${USE_FSAL_SHOOK}")
message(STATUS "USE_FSAL_LUSTRE_UP = ${USE_FSAL_LUSTRE_UP}")
message(STATUS "USE_FSAL_GLUSTER = ${USE_FSAL_GLUSTER}")
message(STATUS "USE_DBUS = ${USE_DBUS}")
message(STATUS "USE_CB_SIMULATOR = ${USE_CB_SIMULATOR}")
message(STATUS "USE_NFSIDMAP = ${USE_NFSIDMAP}")
message(STATUS "ENABLE_ERROR_INJECTION = ${ENABLE_ERROR_INJECTION}")
message(STATUS "USE_CAPS = ${USE_CAPS}")
message(STATUS "USE_BLKID = ${USE_BLKID}")
message(STATUS "STRICT_PACKAGE = ${STRICT_PACKAGE}")
message(STATUS "DISTNAME_HAS_GIT_DATA = ${DISTNAME_HAS_GIT_DATA}" )
message(STATUS "_MSPAC_SUPPORT = ${_MSPAC_SUPPORT}")
message(STATUS "USE_EFENCE = ${USE_EFENCE}")
message(STATUS "_NO_TCP_REGISTER = ${_NO_TCP_REGISTER}")
message(STATUS "_NO_PORTMAPPER = ${_NO_PORTMAPPER}")
message(STATUS "_NO_XATTRD = ${_NO_XATTRD}")
message(STATUS "DEBUG_SAL = ${DEBUG_SAL}")
message(STATUS "PROXY_HANDLE_MAPPING = ${PROXY_HANDLE_MAPPING}")
message(STATUS "DEBUG_SYMS = ${DEBUG_SYMS}")
message(STATUS "COVERAGE = ${COVERAGE}")
message(STATUS "PROFILING = ${PROFILING}")
message(STATUS "USE_GSS = ${USE_GSS}")
message(STATUS "TIRPC_EPOLL = ${TIRPC_EPOLL}")
message(STATUS "USE_9P = ${USE_9P}")
message(STATUS "_USE_9P = ${_USE_9P}")
message(STATUS "_USE_9P_RDMA = ${_USE_9P_RDMA}")
message(STATUS "KRB5_PREFIX = ${KRB5_PREFIX}")
message(STATUS "CEPH_PREFIX = ${CEPH_PREFIX}")
message(STATUS "HPSS_PREFIX = ${HPSS_PREFIX}")
message(STATUS "GLUSTER_PREFIX = ${GLUSTER_PREFIX}")
message(STATUS "ZFS_PREFIX = ${ZFS_PREFIX}")
message(STATUS "LUSTRE_PREFIX = ${LUSTRE_PREFIX}")
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
message(STATUS "_GIT_HEAD_COMMIT = ${_GIT_HEAD_COMMIT}")
message(STATUS "_GIT_HEAD_COMMIT_ABBREV = ${_GIT_HEAD_COMMIT_ABBREV}")
message(STATUS "_GIT_DESCRIBE = ${_GIT_DESCRIBE}")
message(STATUS "ALLOCATOR = ${ALLOCATOR}")
message(STATUS "GOLD_LINKER = ${GOLD_LINKER}")
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
message(STATUS "FSAL_DESTINATION = ${FSAL_DESTINATION}")
message(STATUS "USE_ADMIN_TOOLS = ${USE_ADMIN_TOOLS}")
message(STATUS "MODULES_PATH = ${MODULES_PATH}")
message(STATUS "USE_TSAN = ${USE_TSAN}")
message(STATUS "USE_LTTNG = ${USE_LTTNG}")

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

set(USE_HPSS_FSAL ${USE_HPSS_FSAL}
  CACHE BOOL
   "build HPSS FSAL"
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

set(USE_FSAL_PT ${USE_FSAL_PT}
  CACHE BOOL
   "build PT FSAL"
   FORCE)

set(USE_FSAL_ZFS ${USE_FSAL_ZFS}
  CACHE BOOL
   "build ZFS FSAL"
   FORCE)

set(USE_FSAL_LUSTRE ${USE_FSAL_LUSTRE}
  CACHE BOOL
   "build LUSTRE FSAL"
   FORCE)

set(HAVE_INCLUDE_LUSTREAPI_H ${HAVE_INCLUDE_LUSTREAPI_H}
  CACHE BOOL
   "build LUSTRE FSAL with lustreapi.h"
   FORCE)

set(HAVE_INCLUDE_LIBLUSTREAPI_H ${HAVE_INCLUDE_LIBLUSTREAPI_H}
  CACHE BOOL
   "build LUSTRE FSAL with liblustreapi.h"
   FORCE)

set(USE_FSAL_SHOOK ${USE_FSAL_SHOOK}
  CACHE BOOL
   "build SHOOK FSAL"
   FORCE)

set(USE_FSAL_GLUSTER ${USE_FSAL_GLUSTER}
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

set(ENABLE_ERROR_INJECTION ${ENABLE_ERROR_INJECTION}
  CACHE BOOL
   "enable error injection"
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

set(USE_ADMIN_TOOLS ${USE_ADMIN_TOOLS}
  CACHE BOOL
  "Package Admin Scripts"
  FORCE)

set(USE_TSAN ${USE_TSAN}
  CACHE BOOL
  "Enable GCC Thread-Sanitizer"
  FORCE)

set(USE_LTTNG ${USE_LTTNG}
  CACHE BOOL
  "Enable LTTng tracing"
  FORCE)

set(USE_NFS_MSK ${USE_NFS_MSK}
  CACHE BOOL
  "enable nfs Mooshika"
  FORCE)

set(_USE_NFS_MSK ${USE_NFS_MSK}
  CACHE BOOL
  "enable nfs Mooshika in config"
  FORCE)

# Now create a useable config.h
configure_file(
  "${PROJECT_SOURCE_DIR}/include/config-h.in.cmake"
  "${PROJECT_BINARY_DIR}/include/config.h"
)


########### add a "make dist" and a "make rpm"  ###############

set( PKG_NAME "${CPACK_PACKAGE_FILE_NAME}.tar.gz")
add_custom_target(dist COMMAND ${CMAKE_MAKE_PROGRAM} package_source)

# set "--with" arguments for rpmbuild
# This has to be coherent with chosen FSALs
set( WITH_ARG "")

if(USE_FSAL_LUSTRE)
    set( WITH_ARG "${WITH_ARG} --with lustre")
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_SHOOK)
    set( WITH_ARG "${WITH_ARG} --with shook")
endif(USE_FSAL_SHOOK)

if(USE_FSAL_ZFS)
    set( WITH_ARG "${WITH_ARG} --with zfs")
endif(USE_FSAL_ZFS)

if(USE_FSAL_XFS)
    set( WITH_ARG "${WITH_ARG} --with xfs")
endif(USE_FSAL_XFS)

if(USE_FSAL_PANFS)
    set( WITH_ARG "${WITH_ARG} --with panfs")
endif(USE_FSAL_PANFS)

if(USE_FSAL_GPFS)
    set( WITH_ARG "${WITH_ARG} --with gpfs")
endif(USE_FSAL_GPFS)

if(USE_FSAL_CEPH)
    set( WITH_ARG "${WITH_ARG} --with ceph")
endif(USE_FSAL_CEPH)

if(USE_FSAL_CEPH)
    set( WITH_ARG "${WITH_ARG} --with ceph")
endif(USE_FSAL_CEPH)

if(USE_FSAL_GLUSTER)
    set( WITH_ARG "${WITH_ARG} --with gluster")
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_HPSS)
    set( WITH_ARG "${WITH_ARG} --with hpss")
endif(USE_FSAL_HPSS)

if(USE_FSAL_PT)
    set( WITH_ARG "${WITH_ARG} --with pt")
endif(USE_FSAL_PT)

if(USE_9P_RDMA)
    set( WITH_ARG "${WITH_ARG} --with rdma")
endif(USE_9P_RDMA)

if(${ALLOCATOR} STREQUAL "jemalloc")
    set( WITH_ARG "${WITH_ARG} --with jemalloc")
endif(${ALLOCATOR} STREQUAL "jemalloc")

if(USE_ADMIN_TOOLS)
    set( WITH_ARG "${WITH_ARG} --with utils")
endif(USE_ADMIN_TOOLS)

# Now create a useable specfile
configure_file(
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec-in.cmake"
  "${PROJECT_SOURCE_DIR}/nfs-ganesha.spec"
)

add_custom_target( rpm DEPENDS dist)
add_custom_command(TARGET rpm
                  COMMAND sh -c "rpmbuild ${WITH_ARG} -ta ${PKG_NAME}"
		  VERBATIM
		  DEPENDS dist)

add_custom_target( srpm DEPENDS dist)
add_custom_command(TARGET srpm
                  COMMAND sh -c "rpmbuild --define '_srcrpmdir ${CMAKE_CURRENT_BINARY_DIR}' ${WITH_ARG} -ts ${PKG_NAME}"
		  VERBATIM
		  DEPENDS dist)

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
