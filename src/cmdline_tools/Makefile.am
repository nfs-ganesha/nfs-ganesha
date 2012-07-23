DLOPEN_FLAGS = -rdynamic

EXTRA_DIST = ganesha_log_level

TIRPC_LIB = @TIRPCPATH@/src/libntirpc.la
TIRPC_STATIC_LIB = @TIRPCPATH@/src/.libs/libntirpc.a

AM_CFLAGS    = $(DLOPEN_FLAGS) -Wimplicit -I$(srcdir)/../MainNFSD $(FSAL_CFLAGS) $(SEC_CFLAGS)

bin_PROGRAMS = $(FS_NAME).ganesha.convertFH $(FS_NAME).ganesha.printFH
bin_SCRIPTS = ganesha_log_level

__FS_NAME__ganesha_convertFH_SOURCES = ConvertFh.c	\
../MainNFSD/nfs_init.h

__FS_NAME__ganesha_printFH_SOURCES = PrintFh.c	\
../MainNFSD/nfs_init.h

__FS_NAME__ganesha_convertFH_LDADD = ../MainNFSD/libMainServices.la	\
	$(FSAL_LDFLAGS) \
	$(TIRPC_STATIC_LIB) \
	$(SEC_LIB_FLAGS) \
	@EFENCE@ \
	@EXTRA_LIB@

__FS_NAME__ganesha_printFH_LDADD = ../MainNFSD/libMainServices.la	\
	$(FSAL_LDFLAGS) \
	$(TIRPC_STATIC_LIB) \
	$(SEC_LIB_FLAGS) \
	@EFENCE@ \
	@EXTRA_LIB@

if USE_SNMP_ADM
__FS_NAME__ganesha_convertFH_LDADD += ../snmp_adm/libsnmp_adm.la	\
	$(EXT_LDADD)
__FS_NAME__ganesha_printFH_LDADD += ../snmp_adm/libsnmp_adm.la	\
	$(EXT_LDADD)
endif

if USE_9P_RDMA
__FS_NAME__ganesha_convertFH_LDADD += -lmsk -lrdmacm -libverbs
__FS_NAME__ganesha_printFH_LDADD += -lmsk -lrdmacm -libverbs
endif

if USE_NFSIDMAP
__FS_NAME__ganesha_convertFH_LDADD += -lnfsidmap
__FS_NAME__ganesha_printFH_LDADD += -lnfsidmap
endif

