AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libcache_inode.la

#check_PROGRAMS                = test_cache_inode test_cache_inode_readlink \
#                                test_cache_inode_readdir test_cache_inode_lookup 

libcache_inode_la_SOURCES = cache_inode_access.c             \
                            cache_inode_getattr.c            \
                            cache_inode_remove.c             \
                            cache_inode_link.c               \
                            cache_inode_readdir.c            \
                            cache_inode_rename.c             \
                            cache_inode_lookup.c             \
                            cache_inode_lookupp.c            \
                            cache_inode_readlink.c           \
                            cache_inode_rdwr.c               \
                            cache_inode_commit.c             \
                            cache_inode_truncate.c           \
                            cache_inode_get.c                \
                            cache_inode_setattr.c            \
                            cache_inode_renew_entry.c        \
                            cache_inode_invalidate.c         \
                            cache_inode_misc.c               \
                            cache_inode_create.c             \
                            cache_inode_make_root.c          \
                            cache_inode_statfs.c             \
                            cache_inode_init.c               \
                            cache_inode_gc.c                 \
                            cache_inode_read_conf.c          \
                            cache_inode_add_data_cache.c     \
                            cache_inode_open_close.c         \
                            cache_inode_release_data_cache.c \
			    cache_inode_fsal_hash.c          \
			    cache_inode_kill_entry.c         \
                            ../include/cache_inode.h         \
                            ../include/BuddyMalloc.h         \
                            ../include/stuff_alloc.h         \
			    ../include/fsal.h                \
                            ../include/fsal_types.h          \
                            ../include/LRU_List.h            \
                            ../include/log_functions.h       \
                            ../include/HashData.h            \
                            ../include/HashTable.h           \
                            ../include/err_cache_inode.h


#test_cache_inode_lookup_SOURCES    = test_cache_inode_lookup.c
#test_cache_inode_readdir_SOURCES   = test_cache_inode_readdir.c
#test_cache_inode_readlink_SOURCES  = test_cache_inode_readlink.c
#test_cache_inode_SOURCES           = test_cache_inode.c

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

