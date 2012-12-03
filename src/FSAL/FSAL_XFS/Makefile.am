AM_CFLAG              = $(FSAL_CFLAGS) $(SEC_CFLAGS)
libfsalxfs_la_LIBADD  = -lhandle

lib_LTLIBRARIES       = libfsalxfs.la

libfsalxfs_la_SOURCES = fsal_convert.c   \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
	                xfs_fsal.h

new: clean all
