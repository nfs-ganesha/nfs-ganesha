if SHARED_FSAL
DLOPEN_FLAGS =
else
DLOPEN_FLAGS = -rdynamic
endif

AM_CFLAGS                     = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = ../BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

if USE_FSAL_UP
FSAL_UP_LIB_FLAGS = ../FSAL_UP/libfsalup.la
else
FSAL_UP_LIB_FLAGS =
endif

if USE_GSSRPC
SEC_LIB_FLAGS =
SVC_FLAGS =
SVC_LIB_FLAGS = -lgssrpc
endif

if HAVE_GSSAPI
SEC_LIB_FLAGS = $(SEC_LFLAGS) -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err -ldl
endif

if USE_NFSIDMAP
NFSIDMAP_LIB_FLAGS = -lnfsidmap
else
NFSIDMAP_LIB_FLAGS =
endif

if USE_NFS4_ACL
NFS4_ACL_LIB_FLAGS = -lssl -lcrypto
else
NFS4_ACL_LIB_FLAGS = 
endif

if USE_SNMP_ADM
STAT_SNMP_FILES = nfs_stats_snmp.c
STAT_SNMP_LIB_FLAGS = ../snmp_adm/libsnmp_adm.la
else
STAT_SNMP_FILES =
STAT_SNMP_LIB_FLAGS =
endif

if USE_STAT_EXPORTER
STAT_EXPORTER_FILE = nfs_stat_exporter_thread.c
else
STAT_EXPORTER_FILE =
endif

if USE_UPCALL_SIMULATOR
UPCALL_SIMULATOR_FILE = fsal_upcall_simulator_thread.c
else
UPCALL_SIMULATOR_FILE =
endif

if USE_NLM
NLM_LIB = ../Protocols/NLM/libnlm.la
else
NLM_LIB =
endif

if USE_9P
DISPATCH_9P_FILES = 9p_dispatcher.c 
_9P_LIB = ../Protocols/9P/lib9p.la
else
DISPATCH_9P_FILES = 
_9P_LIB = 
endif

if USE_FSAL_FUSE
noinst_LTLIBRARIES           = libMainServices.la
lib_LTLIBRARIES              = libganeshaNFS.la

# Here are a set of rules to help you update your library version information:
#
#   1. Start with version information of '0:0:0' for each libtool library.
#   2. Update the version information only immediately before a public release of your software.
#      More frequent updates are unnecessary, and only guarantee that the current interface number gets larger faster.
#   3. If the library source code has changed at all since the last update, then increment revision (c:r:a becomes c:r+1:a).
#   4. If any interfaces have been added, removed, or changed since the last update, increment current, and set revision to 0.
#   5. If any interfaces have been added since the last public release, then increment age.
#   6. If any interfaces have been removed since the last public release, then set age to 0.

libganeshaNFS_la_LDFLAGS = -version-info 1:0:1

else

noinst_LTLIBRARIES             = libMainServices.la

endif

libMainServices_la_SOURCES = nfs_admin_thread.c                   \
                             nfs_stats_thread.c                   \
                             $(STAT_EXPORTER_FILE)                \
                             $(UPCALL_SIMULATOR_FILE)             \
                             nfs_worker_thread.c                  \
                             nfs_tcb.c                  \
                             nfs_file_content_gc_thread.c         \
                             nfs_rpc_dispatcher_thread.c          \
                             $(DISPATCH_9P_FILES)                 \
                             nfs_file_content_flush_thread.c      \
                             nfs_rpc_tcp_socket_manager_thread.c  \
                             nfs_init.c                           \
                             nfs_tools.c                          \
                             nfs_init.h                           \
                             ../include/LRU_List.h                \
                             ../include/nfs_tcb.h                 \
                             ../include/HashTable.h               \
                             ../include/HashData.h                \
                             ../include/rbt_node.h                \
                             ../include/rbt_tree.h                \
                             ../include/avltree.h                 \
                             ../include/log_functions.h           \
                             ../include/nfs_core.h                \
                             ../include/err_rpc.h                 \
                             ../include/err_LRU_List.h            \
                             ../include/err_HashTable.h           \
                             ../include/nfs_dupreq.h              \
                             ../include/nfs_tools.h               \
                             ../include/nfs_exports.h             \
                             ../include/nfs_proto_functions.h     \
                             ../include/nfs_file_handle.h         \
                             ../include/stuff_alloc.h             \
                             ../include/RW_Lock.h                 \
                             ../include/SemN.h                    \
                             ../include/fsal.h                    \
                             ../include/nfs23.h                   \
                             ../include/nfs4.h                    \
                             ../include/mount.h                   \
                             ../include/cache_inode.h             \
                             ../include/cache_content.h           \
                             ../include/config_parsing.h          \
                             ../include/fsal_up.h                 \
                             ../include/SemN.h                    \
                             ../include/external_tools.h          \
                             ../include/err_inject.h              \
                             $(STAT_SNMP_FILES)

libMainServices_la_LIBADD         =   ../Protocols/NFS/libnfsproto.la                   \
                                      $(PNFS_LIB)                                       \
                                      $(NLM_LIB)                                        \
                                      $(SEC_LIB_FLAGS)                                  \
                                      $(SVC_LIB_FLAGS)                                  \
                                      $(NFSIDMAP_LIB_FLAGS)                             \
                                      $(NFS4_ACL_LIB_FLAGS)                             \
                                      $(STAT_SNMP_LIB_FLAGS)                            \
                                      $(BUDDY_LIB_FLAGS)                                \
                                      ../IdMapper/libidmap.la                           \
                                      ../support/libsupport.la                          \
                                      ../NodeList/libNodeList.la                        \
                                      ../avl/libavltree.la                              \
                                      ../$(CACHE_INODE_DIR)/libcache_inode.la           \
                                      ../SAL/libsal.la                                  \
                                      ../File_Content/libcache_content.la               \
                                      ../File_Content_Policy/libcache_content_policy.la \
                                      ../HashTable/libhashtable.la                      \
                                      ../LRU/liblru.la                                  \
                                      ../FSAL/libfsalcommon.la                          \
                                      $(MFSL_LIB)                                       \
                                      $(FSAL_LIB)                                       \
                                      ../Log/liblog.la                                  \
                                      ../ConfigParsing/libConfigParsing.la              \
                                      ../RPCAL/librpcal.la                              \
                                      $(_9P_LIB)                                        \
                                      ../Protocols/XDR/libnfs_mnt_xdr.la                \
                                      ../SemN/libSemN.la                                \
                                      ../RW_Lock/librwlock.la                           \
                                      $(FSAL_UP_LIB_FLAGS)                              \
                                      ../Common/libcommon_utils.la

libMainServices_la_DEPENDENCIES = $(libMainServices_la_SOURCES) $(libMainServices_la_LIBADD)

if USE_FSAL_FUSE

libganeshaNFS_la_SOURCES = fuse_binding.c $(libMainServices_la_SOURCES)

libganeshaNFS_la_LIBADD = $(libMainServices_la_LIBADD) \
                          $(FSAL_LDFLAGS)              \
                           $(EXT_LDADD) $(SEC_LIB_FLAGS)

libganeshaNFS_la_DEPENDENCIES = fuse_binding.c $(libMainServices_la_SOURCES) $(libMainServices_la_LIBADD)

else

bin_PROGRAMS               = $(FS_NAME).ganesha.nfsd

__FS_NAME__ganesha_nfsd_SOURCES    = nfs_main.c  \
                                     nfs_init.h
__FS_NAME__ganesha_nfsd_LDADD      =  ./libMainServices.la \
                                      $(FSAL_LDFLAGS) $(EXT_LDADD)  \
                                      $(SEC_LIB_FLAGS) @EFENCE@ @EXTRA_LIB@

endif


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
