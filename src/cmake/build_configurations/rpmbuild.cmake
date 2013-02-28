# Turn on everything in the options for a complete build

set(_HANDLE_MAPPING ON)
set(_NO_XATTRD OFF)
set(USE_DBUS OFF)

set( USE_FSAL_VFS ON)

set( USE_FSAL_XFS OFF)
set( USE_FSAL_GPFS OFF)

message(STATUS "Building RPM")
