AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

lib_LTLIBRARIES          = libfsalxfs.la

libfsalxfs_la_SOURCES = fsal_convert.c   \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                xfs_fsal.h

new: clean all
