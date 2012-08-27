AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalvfs.la
lib_LTLIBRARIES          = libfsalvfs.la

libfsalvfs_la_SOURCES = fsal_convert.c   \
                        vfs_methods.h    \
	                fsal_handle_syscalls.h \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                ../../include/fsal.h                     \
                        ../../include/fsal_types.h	         \
	                ../../include/err_fsal.h

new: clean all

