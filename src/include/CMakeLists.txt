#configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

add_subdirectory(FSAL)


########### install files ###############

# the following line should be done if FSAL_FUSE is compiled
#install(FILES  ganesha_fuse_wrap.h COMPONENT headers DESTINATION include)



#original Makefile.am contents follow:

#noinst_HEADERS = abstract_atomic.h               \
#                 abstract_mem.h                  \
#	         ganesha_dbus.h                  \
#		 ganesha_types.h                 \
#                 fsal.h                          \
#                 fsal_pnfs.h                     \
#                 pnfs_utils.h                    \
#                 HashData.h                      \
#                 HashTable.h                     \
#                 MesureTemps.h                   \
#                 HashData.h                      \
#                 HashTable.h                     \
#                 avltree.h                       \
#                 cache_inode_avl.h               \
#                 generic_weakref.h               \
#                 cache_inode_lru.h               \
#                 cache_inode_weakref.h           \
#                 murmur3.h                       \
#                 cidr.h                          \
#                 MesureTemps.h                   \
#                 nodelist.h                      \
#                 fsal.h                          \
#                 fsal_api.h                      \
#                 fsal_convert.h                  \
#                 fsal_handle_syscalls.h          \
#                 fsal_types.h                    \
#                 fsal_up.h                       \
#                 cache_inode.h                   \
#                 common_utils.h                  \
#                 config_parsing.h                \
#                 err_HashTable.h                 \
#                 err_cache_inode.h               \
#                 err_fsal.h                      \
#                 err_ghost_fs.h                  \
#                 err_inject.h                    \
#                 err_rpc.h                       \
#                 extended_types.h                \
#                 external_tools.h                \
#                 log.h                           \
#                 mount.h                         \
#                 nfs23.h                         \
#                 nfs4.h                          \
#                 nfsv41.h                        \
#                 nfs4_acls.h                     \
#                 nfs_core.h                      \
#                 err_inject.h                    \
#                 nfs_creds.h                     \
#                 nfs_dupreq.h                    \
#                 nfs_exports.h                   \
#                 nfs_file_handle.h               \
#                 nfs_proto_functions.h           \
#                 nfs_proto_tools.h               \
#                 nfs_rpc_callback.h              \
#                 nfs_stat.h                      \
#                 nfs_tools.h                     \
#                 rbt_node.h                      \
#                 rbt_tree.h                      \
#                 nfs_ip_stats.h                  \
#                 Connectathon_config_parsing.h   \
#		 ganesha_rpc.h                   \
#                 Rpc_com_tirpc.h                 \
#		 rpc/svc_auth.h                  \
#                 lookup3.h                       \
#                 config.h                        \
#                 sal_data.h                      \
#                 sal_functions.h                 \
#                 snmp_adm.h
#
#if USE_NLM
#noinst_HEADERS += nlm4.h                         \
#                  nlm_util.h                     \
#                  nlm_list.h                     \
#                  nsm.h                          \
#                  nlm_async.h
#endif
#
#if USE_9P
#noinst_HEADERS +=  9p.h
#endif
#
#if USE_FSAL_FUSE
#include_HEADERS = ganesha_fuse_wrap.h
#endif
#
#if USE_RQUOTA
#noinst_HEADERS += rquota.h
#endif
#
#SUBDIRS = FSAL
#
#
