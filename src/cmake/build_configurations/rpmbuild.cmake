# Turn on everything in the options for a complete build

set(_HANDLE_MAPPING ON)
set(_NO_XATTRD OFF)
set(USE_DBUS OFF)
set(USE_DBUS_STATS OFF)

set( USE_FSAL_XFS OFF)
set( USE_FSAL_ZFS OFF)
set( USE_FSAL_CEPH OFF)
set( USE_FSAL_LUSTRE ON)

message(STATUS "Building RPM")
