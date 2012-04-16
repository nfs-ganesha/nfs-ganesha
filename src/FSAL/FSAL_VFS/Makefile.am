AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsalvfs.la

libfsalvfs_la_SOURCES = fsal_convert.c   \
                        fsal_internal.c	 \
                        fsal_internal.h  \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                ../../include/fsal.h                     \
                        ../../include/fsal_types.h	         \
	                ../../include/err_fsal.h	         \
	                ../../include/FSAL/FSAL_VFS/fsal_types.h \
	                ../../include/FSAL/FSAL_VFS/fsal_handle_syscalls.h


new: clean all

