AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libnfs_mnt_xdr.la

libnfs_mnt_xdr_la_SOURCES = xdr_mount.c                    \
                            xdr_nfs23.c                    \
                            xdr_nfsv41.c                   \
			    xdr_rquota.c		   \
			    xdr_nlm4.c		           \
                            xdr_nsm.c

new: clean all
