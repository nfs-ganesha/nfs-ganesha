## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = -I$(top_srcdir)/include -include config.h
#noinst_PROGRAMS = fusexmp fusexmp_fh null hello hello_ll

if IS_LINUX
noinst_PROGRAMS = hello fusexmp fusexmp_use_shared
else
noinst_PROGRAMS = hello fusexmp 
endif

#LDADD = ../MainNFSD/libganeshaNFS.la @libfuse_libs@
#fusexmp_fh_LDADD = ../MainNFSD/libganeshaNFS.la @libfuse_libs@

LDADD = ../MainNFSD/libganeshaNFS.la @EFENCE@ @EXTRA_LIB@
fusexmp_LDADD = ../MainNFSD/.libs/libganeshaNFS.a @EFENCE@ @EXTRA_LIB@
fusexmp_SOURCES = fusexmp_fh.c

if IS_LINUX
fusexmp_use_shared_LDADD = ../MainNFSD/.libs/libganeshaNFS.so @EFENCE@ @EXTRA_LIB@
fusexmp_use_shared_LDFLAGS = -Bdynamic -Wl,-rpath,/usr/lib
fusexmp_use_shared_SOURCES= fusexmp_fh.c 
endif

if USE_GSSRPC
fusexmp_LDADD += $(SEC_LFLAGS) -lgssrpc -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err -ldl
if IS_LINUX
fusexmp_use_shared_LDADD += $(SEC_LFLAGS) -lgssrpc -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err -ldl
endif
else 
if USE_TIRPC
fusexmp_LDADD += -ltirpc
if IS_LINUX
fusexmp_use_shared_LDADD += -ltirpc
endif
endif
endif

if IS_LINUX
if USE_NFS4_ACL
fusexmp_LDADD += -lssl -lcrypto
endif
endif

if USE_NFSIDMAP
fusexmp_LDADD += -lnfsidmap
if IS_LINUX
fusexmp_use_shared_LDADD +=  -lnfsidmap
endif


endif


