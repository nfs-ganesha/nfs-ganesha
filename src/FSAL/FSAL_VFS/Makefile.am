AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalvfs.la
lib_LTLIBRARIES          = libfsalvfs.la

libfsalvfs_la_SOURCES = fsal_convert.c   \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                vfs_methods.h

new: clean all

