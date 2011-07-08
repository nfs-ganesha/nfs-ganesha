noinst_LTLIBRARIES            = liboutils_profiling.la 

if USE_FSAL_GPFS
check_PROGRAMS                = test_anon_support test_access_list_types test_mesure_temps test_glist
else
check_PROGRAMS                = test_mesure_temps 
endif 

TESTS = $(check_PROGRAMS)

liboutils_profiling_la_SOURCES = MesureTemps.c ../include/MesureTemps.h

COMMON_LDADD = ../NFS_Protocols/libnfsproto.la                   \
               $(PNFS_LIB)                                       \
               $(SEC_LIB_FLAGS)                                  \
               $(SVC_LIB_FLAGS)                                  \
               $(NFSIDMAP_LIB_FLAGS)                             \
               $(EXT_LDADD)                                      \
               ../IdMapper/libidmap.la                           \
               ../support/libsupport.la                          \
                 ../NodeList/libNodeList.la                      \
               ../$(CACHE_INODE_DIR)/libcache_inode.la           \
               ../File_Content/libcache_content.la               \
               ../File_Content_Policy/libcache_content_policy.la \
               ../HashTable/libhashtable.la                      \
               ../LRU/liblru.la                                  \
               ../BuddyMalloc/libBuddyMalloc.la                  \
               ../FSAL/libfsalcommon.la                          \
               $(MFSL_LIB)                                       \
               $(FSAL_LIB)                                       \
               ../Log/liblog.la                                  \
               ../ConfigParsing/libConfigParsing.la              \
               ../XDR/libnfs_mnt_xdr.la                          \
               ../SemN/libSemN.la                                \
               ../RW_Lock/librwlock.la                           \
               ../Common/libcommon_utils.la                      \
               ../MainNFSD/libMainServices.la

test_access_list_types_LDADD = $(COMMON_LDADD)
test_access_list_types_SOURCES = test_access_list_types.c

#####test_anon_support_LDADD = $(COMMON_LDADD)
#####test_anon_support_SOURCES = test_anon_support.c

test_mesure_temps_SOURCES    = test_mesure_temps.c
test_mesure_temps_LDADD      = liboutils_profiling.la

test_glist_SOURCES           = test_glist.c 

check-am-local:
	make -C $(top_builddir)


new: clean all
