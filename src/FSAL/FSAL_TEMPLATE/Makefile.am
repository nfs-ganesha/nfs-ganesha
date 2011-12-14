AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libfsaltemplate.la

libfsaltemplate_la_SOURCES = fsal_access.c \
                        fsal_context.c  \
                        fsal_dirs.c     \
                        fsal_fsinfo.c   \
                        fsal_lock.c     \
                        fsal_rcp.c      \
                        fsal_strings.c  \
                        fsal_truncate.c \
			fsal_attrs.c    \
                        fsal_convert.c  \
                        fsal_init.c     \
                        fsal_lookup.c   \
                        fsal_rename.c   \
                        fsal_symlinks.c \
                        fsal_unlink.c   \
			fsal_create.c   \
                        fsal_fileop.c   \
                        fsal_internal.c \
                        fsal_objectres.c\
                        fsal_stats.c    \
                        fsal_tools.c    \
			fsal_create.c   \
                        fsal_fileop.c   \
                        fsal_internal.c \
                        fsal_objectres.c\
                        fsal_stats.c    \
                        fsal_tools.c    \
                        fsal_quota.c    \
                        fsal_xattrs.c   \
                        fsal_local_op.c \
			fsal_internal.h \
                        fsal_convert.h  \
                        fsal_xattrs.c   \
                        fsal_quota.c    \
                        ../../include/fsal.h            \
                        ../../include/fsal_types.h      \
                        ../../include/err_fsal.h        \
                        ../../include/FSAL/FSAL_TEMPLATE/fsal_types.h

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

