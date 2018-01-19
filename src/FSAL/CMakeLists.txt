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

if(USE_FSAL_VFS OR USE_FSAL_LUSTRE OR USE_FSAL_XFS OR USE_FSAL_PANFS)
  add_subdirectory(FSAL_VFS)
endif(USE_FSAL_VFS OR USE_FSAL_LUSTRE OR USE_FSAL_XFS OR USE_FSAL_PANFS)

if(USE_FSAL_GLUSTER)
  add_subdirectory(FSAL_GLUSTER)
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_MEM)
  add_subdirectory(FSAL_MEM)
endif(USE_FSAL_MEM)
