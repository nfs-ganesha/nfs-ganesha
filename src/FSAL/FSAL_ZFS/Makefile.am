AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if BUILD_SHARED_FSAL

lib_LTLIBRARIES = libfsalzfs.la
libfsalzfs_la_LDFLAGS = -version-number @LIBVERSION@
libfsalzfs_la_LIBADD = ../../SemN/libSemN.la ../libfsalcommon.la @ZFSWRAP_LIBS@

else
noinst_LTLIBRARIES          = libfsalzfs.la

endif

libfsalzfs_la_SOURCES = fsal_access.c   \
                        fsal_attrs.c    \
                        fsal_common.c   \
                        fsal_common.h   \
                        fsal_compat.c   \
                        fsal_context.c  \
                        fsal_convert.c  \
                        fsal_create.c   \
                        fsal_dirs.c     \
                        fsal_fileop.c   \
                        fsal_fsinfo.c   \
                        fsal_init.c     \
                        fsal_internal.c \
                        fsal_internal.h \
                        fsal_local_op.c \
                        fsal_lookup.c   \
                        fsal_rcp.c      \
                        fsal_rename.c   \
                        fsal_stats.c    \
                        fsal_symlinks.c \
                        fsal_tools.c    \
                        fsal_truncate.c \
                        fsal_unlink.c   \
                        fsal_xattrs.c   \
                        ../../include/fsal.h            \
                        ../../include/fsal_types.h      \
                        ../../include/err_fsal.h        \
                        ../../include/FSAL/FSAL_ZFS/fsal_types.h


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

