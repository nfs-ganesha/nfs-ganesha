AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

lib_LTLIBRARIES          = libfsalgpfs.la


libfsalgpfs_la_SOURCES = main.c				\
			 export.c			\
			 handle.c			\
			 file.c				\
			 fsal_up.c			\
			 fsal_ds.c			\
			 fsal_mds.c			\
			 fsal_unlink.c			\
			 fsal_truncate.c		\
			 fsal_symlinks.c		\
			 fsal_rename.c			\
			 fsal_create.c			\
			 fsal_fileop.c			\
			 fsal_attrs.c			\
			 fsal_lock.c			\
			 fsal_share.c			\
			 fsal_lookup.c			\
			 fsal_convert.c			\
			 fsal_convert.h			\
			 fsal_internal.c		\
			 fsal_internal.h		\
	                 ../../include/fsal.h		\
                         ../../include/fsal_types.h	\
	                 ../../include/err_fsal.h

new: clean all

