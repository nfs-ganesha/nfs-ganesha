AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcal.la
check_PROGRAMS = test_rpctools

EXTRA_DIST = rpcal.h

TESTS = test_rpctools

test_rpctools_SOURCES = test_rpctools.c
test_rpctools_LDADD = librpcal.la ../HashTable/libhashtable.la

SUBDIRS = gssd

librpcal_la_SOURCES = nfs_dupreq.c \
                      rpc_tools.c

if HAVE_GSSAPI
librpcal_la_SOURCES += gss_extra.c
endif

librpcal_la_DEPENDENCIES = $(librpcal_la_SOURCES)  $(librpcal_la_LIBADD)
