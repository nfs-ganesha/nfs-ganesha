AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalposix.la
lib_LTLIBRARIES          = libfsalposix.la

libfsalposix_la_SOURCES = fsal_convert.c   \
                        fsal_internal.c	 \
                        fsal_internal.h  \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                interface.c sockbuf.c encoding.c nodedb.c redblack.c connection.c connectionpool.c \
	                ../../include/fsal.h                     \
                        ../../include/fsal_types.h	         \
	                ../../include/err_fsal.h	         \
	                ../../include/FSAL/FSAL_POSIX/fsal_types.h \
	                ../../include/FSAL/FSAL_POSIX/fsal_handle_syscalls.h


new: clean all

.PHONY: nodedb

all: nodedb all-am

nodedb:
	cd nodedb && make all

interface.c: nodedb




