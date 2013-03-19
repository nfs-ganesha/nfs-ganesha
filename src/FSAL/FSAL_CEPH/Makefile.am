AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libfsalceph.la

if USE_PNFS
noinst_LTLIBRARIES += libfsalpnfsceph.la
endif

libfsalceph_la_SOURCES = \
                        fsal_context.c  \
                        fsal_compat.c   \
                        fsal_dirs.c     \
                        fsal_fsinfo.c   \
                        fsal_rcp.c      \
                        fsal_truncate.c \
			fsal_attrs.c    \
                        fsal_convert.c  \
                        fsal_init.c     \
                        fsal_lookup.c   \
                        fsal_rename.c   \
                        fsal_symlinks.c \
                        fsal_unlink.c   \
			fsal_create.c   \
                        fsal_errors.c   \
                        fsal_fileop.c   \
                        fsal_internal.c \
                        fsal_stats.c    \
                        fsal_tools.c    \
			fsal_create.c   \
                        fsal_fileop.c   \
                        fsal_internal.c \
                        fsal_stats.c    \
                        fsal_tools.c    \
                        fsal_xattrs.c   \
			fsal_internal.h \
                        fsal_convert.h  \
                        fsal_xattrs.c   \
                        ../../include/fsal.h            \
                        ../../include/fsal_types.h      \
                        ../../include/err_fsal.h        \
                        ../../include/FSAL/FSAL_CEPH/fsal_types.h

if USE_PNFS
libfsalpnfsceph_la_SOURCES = fsal_pnfscompat.c                         \
			     ../../include/fsal.h                      \
			     ../../include/fsal_types.h                \
			     ../../include/err_fsal.h                  \
			     ../../include/pnfs_common.h               \
			     ../../include/fsal_pnfs.h                 \
			     ../../include/fsal_pnfs_files.h           \
			     ../../include/FSAL/FSAL_CEPH/fsal_types.h

if USE_PNFS_MDS
libfsalpnfsceph_la_SOURCES += fsal_mds.c
endif

if USE_PNFS_DS
libfsalpnfsceph_la_SOURCES += fsal_ds.c
endif
endif

new: clean all
