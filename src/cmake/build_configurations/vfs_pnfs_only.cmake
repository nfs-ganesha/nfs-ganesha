# Only build VFS fsal and other pnfs useful options

set(USE_FSAL_PROXY  OFF)
set(USE_FSAL_POSIX  OFF)
set(USE_FSAL_CEPH OFF)
set(USE_FSAL_GPFS OFF)
set(USE_FSAL_ZFS OFF)
set(USE_FSAL_LUSTRE OFF)
set(_MSPAC_SUPPORT OFF)
set(USE_9P OFF)
set(USE_DBUS ON)

message(STATUS "Building everything")
