include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

# All we need to do here is control the
# build of chosen fsals

if(USE_FSAL_HPSS)
  add_subdirectory(FSAL_HPSS)
endif(USE_FSAL_HPSS)

if(USE_FSAL_POSIX)
  add_subdirectory(FSAL_POSIX)
endif(USE_FSAL_POSIX)

if(USE_FSAL_PROXY)
  add_subdirectory(FSAL_PROXY)
endif(USE_FSAL_PROXY)

if(USE_FSAL_CEPH)
  add_subdirectory(FSAL_CEPH)
endif(USE_FSAL_CEPH)

if(USE_FSAL_FUSE)
  add_subdirectory(FSAL_FUSE)
endif(USE_FSAL_FUSE)

if(USE_FSAL_LUSTRE)
  add_subdirectory(FSAL_LUSTRE)
endif(USE_FSAL_LUSTRE)

if(USE_FSAL_XFS)
  add_subdirectory(FSAL_XFS)
endif(USE_FSAL_XFS)

if(USE_FSAL_GPFS)
  add_subdirectory(FSAL_GPFS)
endif(USE_FSAL_GPFS)

if(USE_FSAL_ZFS)
  add_subdirectory(FSAL_ZFS)
endif(USE_FSAL_ZFS)

if(USE_FSAL_VFS)
  add_subdirectory(FSAL_VFS)
endif(USE_FSAL_VFS)


