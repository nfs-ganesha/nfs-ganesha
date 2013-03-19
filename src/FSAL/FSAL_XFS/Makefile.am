AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsalxfs.la

libfsalxfs_la_SOURCES = fsal_context.c     \
                        fsal_convert.c     \
                        fsal_compat.c      \
                        fsal_dirs.c        \
                        fsal_fsinfo.c      \
                        fsal_lock.c        \
                        fsal_rcp.c         \
                        fsal_truncate.c    \
			fsal_attrs.c       \
                        fsal_init.c        \
                        fsal_lookup.c      \
                        fsal_rename.c      \
                        fsal_symlinks.c    \
                        fsal_unlink.c      \
			fsal_create.c      \
                        fsal_fileop.c      \
                        fsal_internal.c    \
                        fsal_stats.c       \
                        fsal_tools.c       \
			fsal_internal.h    \
                        fsal_xattrs.c      \
                        fsal_quota.c       \
                        ../../include/fsal_convert.h \
                        ../../include/fsal.h         \
                        ../../include/fsal_types.h   \
                        ../../include/err_fsal.h     \
                        ../../include/FSAL/FSAL_XFS/fsal_types.h


new: clean all

