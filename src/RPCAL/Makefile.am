noinst_LTLIBRARIES = librpcal.la

if USE_GSSRPC
SUBDIRS = GSSRPC
librpcal_la_SOURCES = dummy.c
librpcal_la_LIBADD = GSSRPC/librpcalcore.la
else 
if USE_TIRPC
SUBDIRS = TIRPC
librpcal_la_SOURCES = dummy.c
librpcal_la_LIBADD = TIRPC/librpcalcore.la
else
SUBDIRS = ONCRPC
librpcal_la_SOURCES = dummy.c
librpcal_la_LIBADD = ONCRPC/librpcalcore.la
endif
endif

librpcal_la_DEPENDENCIES = $(librpcal_la_SOURCES)  $(librpcal_la_LIBADD)
