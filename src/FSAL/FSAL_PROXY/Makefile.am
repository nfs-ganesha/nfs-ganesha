AM_CFLAGS = $(FSAL_CFLAGS) $(SEC_CFLAGS)

lib_LTLIBRARIES          = libfsalproxy.la

libfsalproxy_la_SOURCES = handle.c proxy.c export.c xattrs.c

if ENABLE_HANDLE_MAPPING
libfsalproxy_la_SOURCES += handle_mapping/handle_mapping.c \
		 	   handle_mapping/handle_mapping_db.c
			   handle_mapping/handle_mapping.h \
			   handle_mapping/handle_mapping_internal.h \
		 	   handle_mapping/handle_mapping_db.h
libfsalproxy_la_LIBADD = -lsqlite3
endif

new: clean all

