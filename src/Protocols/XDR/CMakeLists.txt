
########### next target ###############

SET(nfs_mnt_xdr_STAT_SRCS
   xdr_mount.c
   xdr_nfs23.c
   xdr_nfsv41.c
   xdr_rquota.c
   xdr_nlm4.c
   xdr_nsm.c
)

add_library(nfs_mnt_xdr STATIC ${nfs_mnt_xdr_STAT_SRCS})


########### install files ###############
