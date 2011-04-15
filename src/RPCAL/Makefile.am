noinst_LTLIBRARIES = librpcal.la

if USE_GSSRPC
SUBDIRS = GSSRPC
librpcal_la_LIBADD = GSSRPC/librpcalcore.la
endif

if USE_TIRPC
SUBDIRS = TIRPC
librpcal_la_LIBADD = TIRPC/librpcalcore.la
endif

if USE_ONCRPC
SUBDIRS = ONCRPC
librpcal_la_LIBADD = ONCRPC/librpcalcore.la
endif

librpcal_la_SOURCES = nfs_dupreq.c \
                      ../include/nfs_dupreq.h

librpcal_la_DEPENDENCIES = $(librpcal_la_SOURCES)  $(librpcal_la_LIBADD)
