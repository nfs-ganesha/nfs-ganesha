AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libnfs_mnt_xdr.la

libnfs_mnt_xdr_la_SOURCES = xdr_mount.c                    \
                            xdr_nfs23.c                    \
                            xdr_nfsv41.c

if USE_RQUOTA
libnfs_mnt_xdr_la_SOURCES += xdr_rquota.c
endif

if USE_NLM
libnfs_mnt_xdr_la_SOURCES += xdr_nlm4.c           \
                             xdr_nsm.c
endif

new: clean all
