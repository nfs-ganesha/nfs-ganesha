AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalvfs.la
lib_LTLIBRARIES          = libfsalvfs.la

SUBDIRS =
if USE_PANFS_PNFS
SUBDIRS += pnfs_panfs
endif

libfsalvfs_la_SOURCES = fsal_convert.c   \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                vfs_methods.h

if USE_PANFS_PNFS
libfsalvfs_la_LIBADD = ./pnfs_panfs/libfsalpanfs.la
endif


new: clean all

