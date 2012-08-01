AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#noinst_LTLIBRARIES          = libfsalposix.la
lib_LTLIBRARIES          = libfsalposix.la

libfsalposix_la_SOURCES = interface.c fsal_convert.c   \
                        fsal_internal.c	 \
                        fsal_internal.h  \
                        main.c		 \
			export.c	 \
			handle.c	 \
			file.c		 \
			xattrs.c         \
	                sockbuf.c encoding.c nodedb.c redblack.c connection.c connectionpool.c \
	                ../../include/fsal.h                     \
                        ../../include/fsal_types.h	         \
	                ../../include/err_fsal.h	         \
	                ../../include/FSAL/FSAL_POSIX/fsal_handle_syscalls.h


new: clean all

.PHONY: nodedb

all: nodedb all-am

nodedb:
	cd nodedb && make all

interface.c: nodedb

interface.lo: interface.c
sockbuf.lo: sockbuf.c
encoding.lo: encoding.c
nodedb.lo: nodedb.c
redblack.lo: redblack.c
connection.lo: connection.c
connectionpool.lo: connectionpool.c

