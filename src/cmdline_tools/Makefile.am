if SHARED_FSAL
DLOPEN_FLAGS =
else
DLOPEN_FLAGS = -rdynamic
endif

EXTRA_DIST = ganesha_log_level

AM_CFLAGS    = $(DLOPEN_FLAGS) -Wimplicit -I../MainNFSD $(FSAL_CFLAGS) $(SEC_CFLAGS)

bin_PROGRAMS = $(FS_NAME).ganesha.convertFH
bin_SCRIPTS = ganesha_log_level

__FS_NAME__ganesha_convertFH_SOURCES = ConvertFh.c	\
../MainNFSD/nfs_init.h

__FS_NAME__ganesha_convertFH_LDADD = ../MainNFSD/libMainServices.la	\
$(FSAL_LDFLAGS) $(SEC_LIB_FLAGS) @EFENCE@ @EXTRA_LIB@

if USE_SNMP_ADM
__FS_NAME__ganesha_convertFH_LDADD += ../snmp_adm/libsnmp_adm.la	\
$(EXT_LDADD)
endif

if USE_NFSIDMAP
__FS_NAME__ganesha_convertFH_LDADD += -lnfsidmap
endif
