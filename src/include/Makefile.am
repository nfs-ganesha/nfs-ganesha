noinst_HEADERS = BuddyMalloc.h                   \
                 fsal.h                          \
                 mfsl.h                          \
                 HashData.h                      \
                 HashTable.h                     \
                 LRU_List.h                      \
                 MesureTemps.h                   \
                 RW_Lock.h                       \
                 HashData.h                      \
                 HashTable.h                     \
                 LRU_List.h                      \
                 MesureTemps.h                   \
                 RW_Lock.h                       \
                 SemN.h                          \
                 nodelist.h                      \
                 fsal_types.h                    \
                 fsal_glue.h                     \
                 fsal_glue_const.h               \
                 mfsl_types.h                    \
                 cache_content.h                 \
                 cache_content_policy.h          \
                 cache_inode.h                   \
                 common_utils.h                  \
                 config_parsing.h                \
                 err_HashTable.h                 \
                 err_LRU_List.h                  \
                 err_cache_content.h             \
                 err_cache_inode.h               \
                 err_fsal.h                      \
                 err_mfsl.h                      \
                 err_ghost_fs.h                  \
                 err_rpc.h                       \
                 extended_types.h                \
                 external_tools.h                \
                 log_functions.h                 \
                 log_macros.h                    \
                 mount.h                         \
                 nfs23.h                         \
                 nfs4.h                          \
                 nfsv40.h                        \
                 nfsv41.h                        \
                 nfs41_session.h                 \
                 pnfs.h                          \
                 nfs_core.h                      \
                 nfs_creds.h                     \
                 nfs_dupreq.h                    \
                 nfs_exports.h                   \
                 nfs_file_handle.h               \
                 nfs_proto_functions.h           \
                 nfs_proto_tools.h               \
                 nfs_stat.h                      \
                 nfs_tools.h                     \
                 posixdb_consistency.h           \
                 rbt_node.h                      \
                 rbt_tree.h                      \
                 stuff_alloc.h                   \
                 nfs_ip_stats.h                  \
                 Connectathon_config_parsing.h   \
                 Rpc_com_tirpc.h                 \
                 solaris_port.h

if USE_NLM
noinst_HEADERS += nlm4.h nlm_util.h nlm_list.h 
endif

if USE_FSAL_FUSE
include_HEADERS = ganesha_fuse_wrap.h
endif


SUBDIRS = FSAL MFSL PNFS

doc:
	doxygen ./doxygen.conf.stuff_alloc
	rep=`grep OUTPUT_DIRECTORY doxygen.conf.stuff_alloc | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

