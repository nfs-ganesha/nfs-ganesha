SUBDIRS = kernel

EXTRA_DIST = kernel/append_handle_prg.c					\
	kernel/create_handle_from_fd_prg.c kernel/create_handle_prg.c	\
	kernel/handle.h kernel/handle_ioctl.c				\
	kernel/hardlink_handle_prg.c kernel/open_by_handle.c		\
	kernel/read_handle_prg.c kernel/readlink_handle_prg.c		\
	kernel/truncate_handle_prg.c kernel/README kernel/Makefile

AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsalgpfs.la

libfsalgpfs_la_SOURCES = fsal_access.c fsal_compat.c fsal_context.c	\
	fsal_dirs.c fsal_fsinfo.c fsal_lock.c fsal_rcp.c		\
	fsal_truncate.c fsal_attrs.c fsal_errors.c fsal_init.c		\
	fsal_lookup.c fsal_convert.c fsal_rename.c fsal_symlinks.c	\
	fsal_unlink.c fsal_create.c fsal_fileop.c fsal_internal.c	\
	fsal_objectres.c fsal_stats.c fsal_tools.c fsal_create.c	\
	fsal_fileop.c fsal_internal.c fsal_objectres.c fsal_stats.c	\
	fsal_tools.c fsal_quota.c fsal_xattrs.c fsal_local_op.c		\
	fsal_convert.h fsal_internal.h fsal_xattrs.c			\
	../../include/fsal.h ../../include/fsal_types.h			\
	../../include/err_fsal.h					\
	../../include/FSAL/FSAL_GPFS/fsal_types.h


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

