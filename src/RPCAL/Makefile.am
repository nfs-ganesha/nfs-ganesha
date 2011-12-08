AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcal.la
check_PROGRAMS = test_rpctools

EXTRA_DIST = rpcal.h

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = ../BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

TESTS = test_rpctools

test_rpctools_SOURCES = test_rpctools.c
test_rpctools_LDADD = librpcal.la $(BUDDY_LIB_FLAGS) ../HashTable/libhashtable.la ../RW_Lock/librwlock.la

if USE_TIRPC
SUBDIRS = TIRPC
librpcal_la_LIBADD = TIRPC/librpcalcore.la
else
if USE_GSSRPC
SUBDIRS = GSSRPC
librpcal_la_LIBADD = GSSRPC/librpcalcore.la
else
if USE_ONCRPC
SUBDIRS = ONCRPC
librpcal_la_LIBADD = ONCRPC/librpcalcore.la
else
librpcal_la_LIBADD =
endif
endif
endif

librpcal_la_SOURCES = nfs_dupreq.c \
                      rpc_tools.c \
                      ../include/nfs_dupreq.h

if HAVE_GSSAPI
librpcal_la_SOURCES += AuthGss_HashTable.c \
                       Svc_auth.c       \
                       Svc_auth_gss.c   \
                       Svc_auth_none.c  \
                       Svc_auth_unix.c
endif

librpcal_la_DEPENDENCIES = $(librpcal_la_SOURCES)  $(librpcal_la_LIBADD)
