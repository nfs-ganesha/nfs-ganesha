
# where `make install` will place files
set(CMAKE_PREFIX_PATH "/usr/")

# FSAL's to build
set(USE_FSAL_GPFS ON)
set(USE_FSAL_VFS ON)
set(USE_FSAL_PROXY  ON)

set(USE_DBUS ON)

# Disable FSAL's we don't use
set(USE_FSAL_CEPH OFF)
set(USE_FSAL_ZFS OFF)
set(USE_FSAL_LUSTRE OFF)
set(_MSPAC_SUPPORT OFF)
set(USE_9P OFF)

message(STATUS "Building gpfs_vfs_pnfs_only configuration")
