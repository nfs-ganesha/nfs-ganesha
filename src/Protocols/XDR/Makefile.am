AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libnfs_mnt_xdr.la

libnfs_mnt_xdr_la_SOURCES = xdr_mount.c               \
                            xdr_nfs23.c                \
                            ../../include/nfs23.h      \
                            ../../include/mount.h      \
                            ../../include/nfs_core.h   \
                            ../../include/err_inject.h \
                            ../../include/extended_types.h

if USE_QUOTA
libnfs_mnt_xdr_la_SOURCES += xdr_rquota.c \
                             ../../include/rquota.h
endif

if USE_NLM
libnfs_mnt_xdr_la_SOURCES += xdr_nlm4.c           \
                             xdr_nsm.c            \
                             ../../include/nlm4.h \
                             ../../include/nsm.h
endif

if USE_NFS4_1
libnfs_mnt_xdr_la_SOURCES +=  xdr_nfsv41.c            \
                              ../../include/nfsv41.h     
endif
 
if USE_NFS4_0
libnfs_mnt_xdr_la_SOURCES +=  xdr_nfs4.c            \
                              ../../include/nfs4.h     
endif

new: clean all
