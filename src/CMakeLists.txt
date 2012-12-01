# NFS Ganesha  Cmake

# Current version as of Fedora 16.  Not tested with earlier.

cmake_minimum_required(VERSION 2.6.3)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

# Add maintainer mode for (mainly) strict builds

include(${CMAKE_SOURCE_DIR}/cmake/maintainer_mode.cmake)


project(GANESHA C)

set(${PROJECT_NAME}_MAJOR_VERSION 2)
set(${PROJECT_NAME}_MINOR_VERSION 0)
set(${PROJECT_NAME}_PATCH_LEVEL 0)
set(VERSION_COMMENT
  "GANESHA 64 bits compliant. SNMP exported stats. FSAL_PROXY re-exports NFSv3. RPCSEC_GSS support (partial). FUSELIKE added"
)
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC _GIT_HEAD_COMMIT)
git_describe(_GIT_DESCRIBE)

# Linkage to TIRPC repo and commit
set(tirpc_repo git://github.com/linuxbox2/ntirpc.git)
set(tirpc_commit 9d95322cfa99c02e566b9ebeebffe79e840fdb0e)

# FSAL selection
# FSALs which are enabled by default but could be disabled
# during the build

option(USE_FSAL_PROXY "build PROXY FSAL shared library" ON)
option(USE_FSAL_VFS "build VFS FSAL shared library" ON)
option(USE_FSAL_POSIX "build POSIX FSAL shared library" ON)
option(USE_FSAL_CEPH "build CEPH FSAL shared library" ON)
option(USE_FSAL_GPFS "build GPFS FSAL" ON)
option(USE_FSAL_ZFS "build ZFS FSAL" ON)
option(USE_FSAL_LUSTRE "build LUSTRE FSAL" ON)

# FSALs which are disabled by default
option(USE_FSAL_HPSS "build HPSS FSAL" OFF)
option(USE_FSAL_FUSE "build FUSE FSAL" OFF)
option(USE_FSAL_XFS "build XFS FSAL" OFF)

# SHOOK is a variant of Lustre FSAL
option(USE_FSAL_SHOOK "build SHOOK FSAL" OFF)


# Build configure options

option(USE_DBUS "enable DBUS protocol support" OFF)

# TI-RPC former option now on?  Do we really need this anymore?
# from Matt, NO.  deprecate
set(_USE_TIRPC ON)

option(USE_TIRPC_IPV6 "enable IPv6 support (via IPv6)" OFF)
option(USE_NFSIDMAP "Use of libnfsidmap for name resolution" ON)
option(ENABLE_SNMP_ADM "export GANESHA statistics with SNMP" OFF)
option(ENABLE_ERROR_INJECTION "enable error injection" OFF)
option(USE_STAT_EXPORTER
  "export GANESHA NFS request statistics with a dedicated thread and socket" OFF)

# Electric Fence (-lefence) link flag
option(USE_EFENCE "link with efence memory debug library" OFF)

# These are -D_FOO options, why ???  should be flags??
option(_NO_TCP_REGISTER "disable registration of tcp services on portmapper" ON)
option(_NO_PORTMAPPER "disable registration on portmapper" ON)
option(_NO_XATTRD "disable ghost xattr directory and files support" ON)
option(_DEBUG_MEMLEAKS "enable allocator features for tracking memory usage" OFF)
option(_DEBUG_NFS_SHELL "enable extended debug traces for ganeshell utility" OFF)
option(_HANDLE_MAPPING "enable NFSv2/3 handle mapping for PROXY FSAL" OFF)

# Debug symbols (-g) build flag 
option(DEBUG_SYMS "include debug symbols to binaries (-g option)" OFF)

# enable code profiling [-g -pg]
option(PROFILING "turn on code profiling (-g and -pg)" OFF)

# MSPAC support -lwbclient link flag
option(_MSPAC_SUPPORT "enable mspac Winbind support" ON)

# CUnit
option(USE_CUNIT "Use Cunit test framework" OFF)

# Enable 9P Support
option(USE_9P "enable 9P support" ON)
option(USE_9P_RDMA "enable 9P_RDMA support" OFF)

# Compile with "NO_MOUNT_LIST" [Don't use MOUNT PROTOCOL's  client list]
set(_NO_MOUNT_LIST ON)

# Choose a shortcut build config

IF(BUILD_CONFIG)
  INCLUDE(
  ${CMAKE_SOURCE_DIR}/cmake/build_configurations/${BUILD_CONFIG}.cmake)
ENDIF()

# Build source locations and parameters

set(CEPH_PREFIX "/usr" CACHE PATH "Path to Ceph installation")
set(ALLOCATOR "jemalloc" CACHE STRING
  "specify the memory allocator to use: jemalloc|tcmalloc|libc")

# Find packages and libs we need for building
include(CheckIncludeFiles)
include(TestBigEndian)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(LINUX ON)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(FREEBSD ON)
endif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

check_include_files(stdbool.h HAVE_STDBOOL_H)
check_include_files(strings.h HAVE_STRINGS_H)
check_include_files(string.h HAVE_STRING_H)

# X_ATTRD requires the kernel to have xattrs...
if(NOT _NO_XATTRD)
  check_include_files("unistd.h;attr/xattr.h" HAVE_XATTR_H)
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

find_package(Threads REQUIRED)


find_package(Krb5 REQUIRED gssapi)

if(KRB5_FOUND)
  set(HAVE_KRB5 ON)
  set(KRB5_VERSION 194)  # hand code until we do krb5-config --version magic
  set(_HAVE_GSSAPI ON)
endif(KRB5_FOUND)

# needed by deprecated shell??
find_package(Curses)
find_package(Readline)

find_package(OpenSSL REQUIRED) # md5 needed by  ${CMAKE_THREAD_LIBS_INIT} nfs4 acls (why??)
set(SYSTEM_LIBRARIES ${SYSTEM_LIBRARIES} ${OPENSSL_LIBRARIES})

# Validate fsal dependencies
if(USE_FSAL_CEPH)
  find_library(CEPH_FS cephfs PATHS ${CEPH_PREFIX})
  if(CEPH_FS_NOTFOUND)
    message(WARNING "Cannot find CEPH runtime.  Disabling CEPH fsal build")
    set(USE_CEPH OFF)
  endif(CEPH_FS_NOTFOUND)
endif(USE_FSAL_CEPH)

if(USE_FSAL_LUSTRE)
  find_library(LUSTRE_FS lustreapi)
  if(LUSTRE_FS_NOTFOUND)
    message(WARNING "Cannot find LUSTRE runtime. Disabling LUSTRE fsal build")
    set(USE_FSAL_LUSTRE OFF)
  endif(LUSTRE_FS_NOTFOUND)
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_LUSTRE AND USE_FSAL_SHOOK)
  find_file(XATTR_H attr/xattr.h)
  find_library(SHOOK_LIB shooksvr)
  if(XATTR_H_NOTFOUND OR SHOOK_LIB_NOTFOUND)
    message(WARNING "Cannot find SHOOK xattrs or library. Disabling SHOOK build")
    set(USE_FSAL_SHOOK OFF)
  endif(SHOOK_LIB_NOTFOUND)
endif(USE_FSAL_LUSTRE AND USE_FSAL_SHOOK)

if(USE_GPFS)
  find_library(GPFS_LIB gpfs)
  if(GPFS_LIB_NOTFOUND)
    message(WARNING "Cannot find GPFS runtime.  Disabling GPFS build")
    set(USE_GPFS OFF)
  endif(GPFS_LIB_NOTFOUND)
endif(USE_GPFS)

if(USE_ZFS)
  find_library(ZFS_LIB zfswrap)
  if(ZFS_LIB_NOTFOUND)
    message(WARNING "Cannot find ZFS runtime. Disabling ZFS build")
    set(USE_ZFS OFF)
  endif(ZFS_LIB_NOTFOUND)
endif(USE_ZFS)

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
  endif(NOT TCMALLOC_FOUND)
else()
  if(NOT ${ALLOCATOR} STREQUAL "libc")
    message(SEND_ERROR "${ALLOCATOR}  is not a valid allocator option.")
  endif()
endif()

# Find optional libraries/packages
if(USE_EFENCE)
  find_package(efence REQUIRED)
  set(SYSTEM_LIBRARIES ${efence_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_EFENCE)

if(USE_DBUS)
  find_package(DBus REQUIRED)
  set(SYSTEM_LIBRARIES ${DBUS_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_DBUS)

if(USE_NFSIDMAP)
  find_package(NfsIdmap REQUIRED)
  set(SYSTEM_LIBRARIES ${NfsIdmap_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_NFSIDMAP)

if(USE_CUNIT)
  find_package(cunit REQUIRED)
  set(SYSTEM_LIBRARIES ${cunit_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_CUNIT)

if(_MSPAC_SUPPORT)
  find_package(WBclient REQUIRED)
  set(SYSTEM_LIBRARIES ${WBclient_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(_MSPAC_SUPPORT)

add_definitions(-DHAVE_CONFIG_H)

include_directories(
  "${PROJECT_BINARY_DIR}/include"
  "${PROJECT_SOURCE_DIR}/include"
# In the new cmake world there is no directory "${PROJECT_SOURCE_DIR}/libtirpc/tirpc/"  ===>
  "${PROJECT_BINARY_DIR}/libtirpc/tirpc/"
)

# Pull in TIRPC and build it

include(ExternalProject)

ExternalProject_Add(
  ntirpc_external
  SOURCE_DIR "${PROJECT_BINARY_DIR}/libtirpc"
  GIT_REPOSITORY ${tirpc_repo}
  GIT_TAG ${tirpc_commit}
  UPDATE_COMMAND sh "${PROJECT_BINARY_DIR}/libtirpc/autogen.sh"
  CONFIGURE_COMMAND ${PROJECT_BINARY_DIR}/libtirpc/configure
    --enable-gss
    --prefix=${PROJECT_BINARY_DIR}/libtirpc
  BUILD_IN_SOURCE 1
  BUILD_COMMAND make
  INSTALL_COMMAND make install
)
# Tell cmake that this generates the tirpc library
add_library(ntirpc UNKNOWN IMPORTED)
set_property(
  TARGET ntirpc
  PROPERTY IMPORTED_LOCATION "${PROJECT_BINARY_DIR}/libtirpc/lib/libntirpc.a"
)

#callers add:
#  target_link_libraries(your_library ${LIBTIRPC_LIBRARIES})
set(LIBTIRPC_INCLUDE_DIR
  "${PROJECT_BINARY_DIR}/libtirpc/tirpc"
  "${PROJECT_BINARY_DIR}/libtirpc/tirpc/rpc"
)
set(LIBTIRPC_LIBRARIES "${PROJECT_BINARY_DIR}/libtirpc/lib/libntirpc.a")

# Find misc system libs

find_library(LIBRT rt)  # extended Pthreads functions
find_library(LIBDL dl)  # module loader

# Roll up required libraries
set(PROTOCOLS
  nfsproto
  nfs_mnt_xdr
  nlm
  rquota
)

if(USE_9P)
  set(PROTOCOLS ${PROTOCOLS} 9p)
  set(_USE_9P ON)
endif(USE_9P)

if(USE_DBUS)
  set(PROTOCOLS ${PROTOCOLS} gshdbus)
endif(USE_DBUS)

if(USE_TIRPC_IPV6)
  set(_USE_TIRPC_IPV6 ON)
endif(USE_TIRPC_IPV6)

set(SYSTEM_LIBRARIES
  ${LIBTIRPC_LIBRARIES}
  ${KRB5_LIBRARIES}
  gssapi_krb5
  ${SYSTEM_LIBRARIES}
  ${LIBDL}
  ${CMAKE_THREAD_LIBS_INIT}
  ${LIBRT}
)

add_subdirectory(include)
add_subdirectory(Log)
add_subdirectory(Common)
add_subdirectory(ConfigParsing)
add_subdirectory(cidr)
add_subdirectory(SemN)
add_subdirectory(test)
add_subdirectory(avl)
add_subdirectory(HashTable)
add_subdirectory(NodeList)
add_subdirectory(Cache_inode)
add_subdirectory(SAL)
add_subdirectory(RPCAL)
add_subdirectory(Protocols)
add_subdirectory(FSAL_UP)
add_subdirectory(support)

if(USE_FSAL_POSIX)
  add_subdirectory(NodeDB)
endif(USE_FSAL_POSIX)

add_subdirectory(FSAL)
add_subdirectory(IdMapper)

if(ENABLE_SNMP_ADM)
  add_subdirectory(snmp_adm)
endif(ENABLE_SNMP_ADM)

add_subdirectory(MainNFSD)
add_subdirectory(Docs)
add_subdirectory(tools)
add_subdirectory(config_samples)
add_subdirectory(check)
add_subdirectory(debian)

if(USE_FSAL_FUSE)
  add_subdirectory(example-fuse)
endif(USE_FSAL_FUSE)

# display configuration vars

message(STATUS)
message(STATUS "-------------------------------------------------------")
message(STATUS "USE_FSAL_PROXY = ${USE_FSAL_PROXY}")
message(STATUS "USE_FSAL_VFS = ${USE_FSAL_VFS}")
message(STATUS "USE_FSAL_POSIX = ${USE_FSAL_POSIX}")
message(STATUS "USE_FSAL_CEPH = ${USE_FSAL_CEPH}")
message(STATUS "USE_FSAL_HPSS = ${USE_FSAL_HPSS}")
message(STATUS "USE_FSAL_FUSE = ${USE_FSAL_FUSE}")
message(STATUS "USE_FSAL_XFS = ${USE_FSAL_XFS}")
message(STATUS "USE_FSAL_GPFS = ${USE_FSAL_GPFS}")
message(STATUS "USE_FSAL_ZFS = ${USE_FSAL_ZFS}")
message(STATUS "USE_FSAL_LUSTRE = ${USE_FSAL_LUSTRE}")
message(STATUS "USE_FSAL_SHOOK = ${USE_FSAL_SHOOK}")
message(STATUS "USE_DBUS = ${USE_DBUS}")
message(STATUS "USE_TIRPC_IPV6 = ${USE_TIRPC_IPV6}")
message(STATUS "USE_NFSIDMAP = ${USE_NFSIDMAP}")
message(STATUS "ENABLE_SNMP_ADM = ${ENABLE_SNMP_ADM}")
message(STATUS "ENABLE_ERROR_INJECTION = ${ENABLE_ERROR_INJECTION}")
message(STATUS "USE_STAT_EXPORTER = ${USE_STAT_EXPORTER}")
message(STATUS "USE_EFENCE = ${USE_EFENCE}")
message(STATUS "_NO_TCP_REGISTER = ${_NO_TCP_REGISTER}")
message(STATUS "_NO_PORTMAPPER = ${_NO_PORTMAPPER}")
message(STATUS "_NO_XATTRD = ${_NO_XATTRD}")
message(STATUS "_DEBUG_MEMLEAKS = ${_DEBUG_MEMLEAKS}")
message(STATUS "_DEBUG_NFS_SHELL = ${_DEBUG_NFS_SHELL}")
message(STATUS "_HANDLE_MAPPING = ${_HANDLE_MAPPING}")
message(STATUS "DEBUG_SYMS = ${DEBUG_SYMS}")
message(STATUS "PROFILING = ${PROFILING}")
message(STATUS "USE_9P = ${USE_9P}")
message(STATUS "_USE_9P = ${_USE_9P}")
message(STATUS "_USE_9P_RDMA = ${_USE_9P_RDMA}")
message(STATUS "CEPH_PREFIX = ${CEPH_PREFIX}")
message(STATUS "ALLOCATOR = ${ALLOCATOR}")

#force command line options to be stored in cache
set(USE_FSAL_VFS ${USE_FSAL_VFS}
  CACHE BOOL
   "build VFS FSAL shared library"
   FORCE)

set(USE_FSAL_PROXY ${USE_FSAL_PROXY}
  CACHE BOOL
   "build PROXY FSAL shared library"
   FORCE)

set(USE_FSAL_POSIX ${USE_FSAL_POSIX}
  CACHE BOOL
   "build POSIX FSAL shared library"
   FORCE)

set(USE_FSAL_CEPH ${USE_FSAL_CEPH}
  CACHE BOOL
   "build CEPH FSAL shared library"
   FORCE)

set(USE_HPSS_FSAL ${USE_HPSS_FSAL}
  CACHE BOOL
   "build HPSS FSAL"
   FORCE)

set(USE_FSAL_FUSE ${USE_FSAL_FUSE}
  CACHE BOOL
   "build FUSE FSAL"
   FORCE)

set(USE_FSAL_XFS ${USE_FSAL_XFS}
  CACHE BOOL
   "build XFS FSAL"
   FORCE)

set(USE_FSAL_GPFS ${USE_FSAL_GPFS}
  CACHE BOOL
   "build GPFS FSAL"
   FORCE)

set(USE_FSAL_ZFS ${USE_FSAL_ZFS}
  CACHE BOOL
   "build ZFS FSAL"
   FORCE)

set(USE_FSAL_LUSTRE ${USE_FSAL_LUSTRE}
  CACHE BOOL
   "build LUSTRE FSAL"
   FORCE)

set(USE_FSAL_SHOOK ${USE_FSAL_SHOOK}
  CACHE BOOL
   "build SHOOK FSAL"
   FORCE)

set(USE_DBUS ${USE_DBUS}
  CACHE BOOL
   "enable DBUS protocol support"
   FORCE)

set(USE_TIRPC_IPV6 ${USE_TIRPC_IPV6}
  CACHE BOOL
   "enable IPv6 support (via IPv6)"
   FORCE)

set(USE_NFSIDMAP ${USE_NFSIDMAP}
  CACHE BOOL
   "Use of libnfsidmap for name resolution"
   FORCE)

set(ENABLE_SNMP_ADM ${ENABLE_SNMP_ADM}
  CACHE BOOL
   "export GANESHA statistics with SNMP"
   FORCE)

set(ENABLE_ERROR_INJECTION ${ENABLE_ERROR_INJECTION}
  CACHE BOOL
   "enable error injection"
   FORCE)

set(USE_STAT_EXPORTER ${USE_STAT_EXPORTER}
  CACHE BOOL
   "export GANESHA NFS request statistics with a dedicated thread and socket"
   FORCE)

set(_MSPAC_SUPPORT ${_MSPAC_SUPPORT}
  CACHE BOOL
   "enable mspac winbind support"
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

# Now create a useable config.h
configure_file(
  "${PROJECT_SOURCE_DIR}/include/config-h.in.cmake"
  "${PROJECT_BINARY_DIR}/include/config.h"
)

########### install files ###############

install(FILES  libganeshaNFS.pc DESTINATION bin)


