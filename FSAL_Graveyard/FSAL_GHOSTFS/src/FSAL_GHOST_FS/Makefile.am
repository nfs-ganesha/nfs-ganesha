AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

SUBDIRS = GHOST_FS 

noinst_LTLIBRARIES          = libfsalghostfs.la

libfsalghostfs_la_SOURCES = fsal_access.c  fsal_context.c  fsal_dirs.c       \
                            fsal_fsinfo.c    fsal_lock.c       fsal_rcp.c    \
                            fsal_strings.c   fsal_truncate.c                 \
			    fsal_attrs.c   fsal_convertions.c  fsal_errors.c \
                            fsal_init.c      fsal_lookup.c     fsal_rename.c \
                            fsal_symlinks.c  fsal_unlink.c                   \
			    fsal_create.c   fsal_fileop.c  fsal_internal.c   \
                            fsal_objectres.c  fsal_stats.c   fsal_tools.c    \
                            fsal_xattrs.c                                    \
                            fsal_quota.c                                     \
                            fsal_local_op.c                                  \
			    fsal_convertions.h fsal_internal.h               \
                            ../../include/fsal.h                             \
                            ../../include/err_fsal.h                         \
                            ../../include/FSAL/FSAL_GHOST_FS/fsal_types.h    \
                            ../../include/FSAL/FSAL_GHOST_FS/ghost_fs.h      \
                            ../../include/err_ghost_fs.h


check_PROGRAMS 		     = test_fsal_ghostfs

test_fsal_ghostfs_SOURCES    = test_fsal.c 
test_fsal_ghostfs_LDADD      = $(FSAL_LIB) $(FSAL_LDFLAGS) -lpthread

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

