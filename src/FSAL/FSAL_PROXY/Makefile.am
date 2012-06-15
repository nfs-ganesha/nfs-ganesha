AM_CFLAGS = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if ENABLE_HANDLE_MAPPING
SUBDIRS = handle_mapping
endif

lib_LTLIBRARIES          = libfsalproxy.la

libfsalproxy_la_SOURCES = handle.c proxy.c export.c xattrs.c

new: clean all

