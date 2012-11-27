add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(cache_inode_STAT_SRCS
   cache_inode_access.c
   cache_inode_getattr.c
   cache_inode_remove.c
   cache_inode_link.c
   cache_inode_readdir.c
   cache_inode_rename.c
   cache_inode_lookup.c
   cache_inode_lookupp.c
   cache_inode_readlink.c
   cache_inode_rdwr.c
   cache_inode_commit.c
   cache_inode_truncate.c
   cache_inode_get.c
   cache_inode_setattr.c
   cache_inode_invalidate.c
   cache_inode_misc.c
   cache_inode_create.c
   cache_inode_make_root.c
   cache_inode_statfs.c
   cache_inode_init.c
   cache_inode_read_conf.c
   cache_inode_open_close.c
   cache_inode_fsal_hash.c
   cache_inode_kill_entry.c
   cache_inode_avl.c
   cache_inode_lru.c
   cache_inode_weakref.c
)

add_library(cache_inode STATIC ${cache_inode_STAT_SRCS})

target_link_libraries(cache_inode
  hashtable
  avltree
  weakref
  hash
)


########### install files ###############
