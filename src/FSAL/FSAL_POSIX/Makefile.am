lib_LTLIBRARIES          = libfsalposix.la

libfsalposix_la_SOURCES = fsal_convert.c   \
                        fsal_internal.c	 \
                        fsal_internal.h  \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                ../../include/FSAL/FSAL_POSIX/fsal_handle_syscalls.h

libfsalposix_la_CFLAGS   = $(FSAL_CFLAGS) $(SEC_CFLAGS) @POSIX_CFLAGS@ -I../../NodeDB
libfsalposix_la_LDFLAGS  = @POSIX_LDFLAGS@
libfsalposix_la_LIBADD   = @POSIX_LIB@

new: clean all

