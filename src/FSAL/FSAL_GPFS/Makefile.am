AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsalgpfs.la

noinst_HEADERS = fsal_convert.h fsal_internal.h

libfsalgpfs_la_SOURCES = fsal_access.c fsal_compat.c fsal_context.c	\
	fsal_dirs.c fsal_fsinfo.c fsal_lock.c fsal_rcp.c		\
	fsal_truncate.c fsal_attrs.c fsal_init.c		\
	fsal_lookup.c fsal_convert.c fsal_rename.c fsal_symlinks.c	\
	fsal_unlink.c fsal_create.c fsal_fileop.c fsal_internal.c	\
	fsal_stats.c fsal_tools.c fsal_create.c	\
	fsal_fileop.c fsal_internal.c fsal_stats.c	\
	fsal_xattrs.c fsal_local_op.c		\
	fsal_xattrs.c fsal_up.c

libfsalgpfs_la_LIBADD = -lgpfs                                          \
			../../support/libsupport.la

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

