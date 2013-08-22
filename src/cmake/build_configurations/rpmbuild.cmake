# Turn on everything in the options for a complete build

set(_HANDLE_MAPPING ON)
set(_NO_XATTRD OFF)
set(USE_DBUS OFF)
set(USE_DBUS_STATS OFF)

# Turn all FSALs ON, if one can't be
# built, cmake will turn it OFF
set( USE_FSAL_XFS ON)
set( USE_FSAL_ZFS ON)
set( USE_FSAL_CEPH ON)
set( USE_FSAL_GPFS ON)
set( USE_FSAL_LUSTRE ON)
set( USE_FSAL_SHOOK ON)

# FSAL HPSS is still a special case for the moment
set(USE_FSAL_HPSS OFF)

message(STATUS "Building RPM")
