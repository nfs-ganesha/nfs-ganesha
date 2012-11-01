AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalvfs.la
lib_LTLIBRARIES          = libfsalvfs.la

SUBDIRS =
SUBDIRS += pnfs_panfs

libfsalvfs_la_SOURCES = fsal_convert.c   \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                vfs_methods.h

libfsalvfs_la_LIBADD = ./pnfs_panfs/libfsalpanfs.la

new: clean all
