# Add the directory for stackable FSALs
add_subdirectory(Stackable_FSALs)

# Add the directory for Pseudo FSAL
add_subdirectory(FSAL_PSEUDO)

# All we need to do here is control the
# build of chosen fsals

if(USE_FSAL_PROXY)
  add_subdirectory(FSAL_PROXY)
endif(USE_FSAL_PROXY)

if(USE_FSAL_CEPH)
  add_subdirectory(FSAL_CEPH)
endif(USE_FSAL_CEPH)

if(USE_FSAL_RGW)
  add_subdirectory(FSAL_RGW)
endif(USE_FSAL_RGW)

if(USE_FSAL_GPFS)
  add_subdirectory(FSAL_GPFS)
endif(USE_FSAL_GPFS)

if(USE_FSAL_ZFS)
  add_subdirectory(FSAL_ZFS)
endif(USE_FSAL_ZFS)

if(USE_FSAL_VFS)
  add_subdirectory(FSAL_VFS)
endif(USE_FSAL_VFS)

if(USE_FSAL_SCALITY)
  add_subdirectory(FSAL_SCALITY)
endif(USE_FSAL_SCALITY)

if(USE_FSAL_GLUSTER)
  add_subdirectory(FSAL_GLUSTER)
endif(USE_FSAL_GLUSTER)
