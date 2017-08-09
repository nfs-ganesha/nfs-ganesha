# Only build VFS fsal for FreeBSD 10.1

set(USE_FSAL_PROXY  OFF)
set(USE_FSAL_CEPH OFF)
set(USE_FSAL_GPFS OFF)
set(_MSPAC_SUPPORT OFF)
set(USE_9P OFF)
set(USE_DBUS OFF)

message(STATUS "Building BSD 10.1 configuration")

