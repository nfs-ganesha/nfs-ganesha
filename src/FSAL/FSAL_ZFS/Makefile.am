AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalzfs.la
lib_LTLIBRARIES          = libfsalzfs.la

libfsalzfs_la_SOURCES = fsal_convert.c   \
                        fsal_internal.c  \
                        fsal_internal.h  \
                        main.c           \
                        export.c         \
                        handle.c         \
                        file.c           \
                        xattrs.c         \
                        zfs_methods.h    \
                        ../../include/FSAL/FSAL_ZFS/fsal_types.h 

libfsalzfs_la_CFLAGS                = $(FSAL_CFLAGS) $(SEC_CFLAGS) @ZFS_CFLAGS@
libfsalzfs_la_LDFLAGS               = @ZFS_LDFLAGS@
libfsalzfs_la_LIBADD                = @ZFS_LIB@

new: clean all


