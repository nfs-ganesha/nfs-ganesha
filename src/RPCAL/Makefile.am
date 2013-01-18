AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcal.la
check_PROGRAMS = test_rpctools

TESTS = test_rpctools

test_rpctools_SOURCES = test_rpctools.c
test_rpctools_LDADD = librpcal.la ../HashTable/libhashtable.la ../RW_Lock/librwlock.la

SUBDIRS = gssd

librpcal_la_SOURCES = nfs_dupreq.c \
                      rpc_tools.c \
                      gss_extra.c \
                      ../include/nfs_dupreq.h

librpcal_la_DEPENDENCIES = $(librpcal_la_SOURCES)  $(librpcal_la_LIBADD)
