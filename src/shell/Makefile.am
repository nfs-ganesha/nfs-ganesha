DLOPEN_FLAGS = -rdynamic

AM_CFLAGS                     =   $(DLOPEN_FLAGS) $(FSAL_CFLAGS) $(SEC_CFLAGS)

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

TIRPC_LIB = @TIRPCPATH@/src/libntirpc.la

SEC_LIB_FLAGS =

if USE_FSAL_FUSE

 # no shell for FUSE, because we are called from the binded filesystem

else

bin_PROGRAMS                      = $(FS_NAME).ganeshell 

__FS_NAME__ganeshell_SOURCES    = Getopt.c                            \
                                  shell_vars.c                        \
                                  shell.c                             \
                                  main.c                              \
                                  shell_utils.c                       \
                                  cmd_tools.c                         \
                                  commands_FSAL.c                     \
                                  commands_Cache_inode.c              \
                                  commands_NFS.c                      \
                                  nfs_remote_functions.c              \
                                  commands_NFS_remote.c               \
                                  cmd_nfstools.c                      \
                                  Getopt.h                            \
                                  cmd_nfstools.h                      \
                                  cmd_tools.h                         \
                                  commands.h                          \
                                  nfs_remote_functions.h              \
                                  shell.h                             \
                                  shell_types.h                       \
                                  shell_utils.h                       \
                                  shell_vars.h                        \
                                  ../include/avltree.h                \
                                  ../include/LRU_List.h               \
                                  ../include/cache_content.h          \
                                  ../include/cache_inode.h            \
                                  ../include/config_parsing.h         \
                                  ../include/err_cache_content.h      \
                                  ../include/err_cache_inode.h        \
                                  ../include/err_fsal.h               \
                                  ../include/err_ghost_fs.h           \
                                  ../include/fsal.h                   \
                                  ../include/log.h                    \
                                  ../include/mount.h                  \
                                  ../include/nfs23.h                  \
                                  ../include/nfs4.h                   \
                                  ../include/nfs_core.h               \
                                  ../include/err_inject.h             \
                                  ../include/nfs_file_handle.h        \
                                  ../include/nfs_proto_functions.h    \
                                  ../include/nfs_proto_tools.h        \
                                  ../include/nfs_tools.h


shell_libs =      ../Protocols/NFS/libnfsproto.la                    \
                  ../Cache_inode/libcache_inode.la                   \
                  ../File_Content/libcache_content.la                \
                  ../File_Content_Policy/libcache_content_policy.la  \
                  ../IdMapper/libidmap.la                            \
                  ../support/libsupport.la                           \
		  ../RPCAL/librpcal.la				     \
                  ../HashTable/libhashtable.la                       \
                  ../LRU/liblru.la                                   \
                  ../avl/libavltree.la                               \
                  ../FSAL/libfsalcommon.la                           \
                  $(FSAL_LIB)                                        \
		  ../FSAL/common_methods.lo			     \
		  ../FSAL/common_functions.lo			     \
		  ../FSAL/access_check.lo			     \
                  ../SemN/libSemN.la                                 \
                  ../RW_Lock/librwlock.la                            \
                  ../Log/liblog.la                                   \
                  ../ConfigParsing/libConfigParsing.la               \
                  ../Protocols/XDR/libnfs_mnt_xdr.la                 \
                  $(TIRPC_LIB)                                       \
		  ../Common/libcommon_utils.la


if USE_NODELIST
shell_libs += ../NodeList/libNodeList.la
endif

__FS_NAME__ganeshell_LDADD = $(shell_libs)                           \
                    $(FSAL_LDFLAGS) $(NFSIDMAP_LIB_FLAGS)            \
                    $(NFS4_ACL_LIB_FLAGS)                            \
                    $(SEC_LIB_FLAGS) @EFENCE@ @EXTRA_LIB@ 

__FS_NAME__ganeshell_DEPENDENCIES = $(__FS_NAME__ganeshell_SOURCES) $(shell_libs)

endif

new: clean all

