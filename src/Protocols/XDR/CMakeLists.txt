
########### next target ###############

SET(nfs_mnt_xdr_STAT_SRCS
   xdr_mount.c
   xdr_nfs23.c
   xdr_nfsv41.c
   xdr_nlm4.c
   xdr_nsm.c
   xdr_nfsacl.c
)

if(USE_RQUOTA)
  SET(nfs_mnt_xdr_STAT_SRCS
    ${nfs_mnt_xdr_STAT_SRCS}
    xdr_rquota.c
  )
endif(USE_RQUOTA)

add_library(nfs_mnt_xdr OBJECT ${nfs_mnt_xdr_STAT_SRCS})
add_sanitizers(nfs_mnt_xdr)
set_target_properties(nfs_mnt_xdr PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
