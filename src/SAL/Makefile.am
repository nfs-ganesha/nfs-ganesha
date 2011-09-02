AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libsal.la

#check_PROGRAMS                = test_cache_inode test_cache_inode_readlink \
#                                test_cache_inode_readdir test_cache_inode_lookup 

libsal_la_SOURCES = state_lock.c                     \
                    state_misc.c                     \
                    nfs4_state.c                     \
                    nfs4_state_id.c                  \
                    nfs4_owner.c                     \
                    nfs4_lease.c                     \
                    ../include/BuddyMalloc.h         \
                    ../include/HashData.h            \
                    ../include/HashTable.h           \
                    ../include/LRU_List.h            \
                    ../include/cache_inode.h         \
                    ../include/config.h              \
                    ../include/err_cache_inode.h     \
                    ../include/fsal.h                \
                    ../include/fsal_types.h          \
                    ../include/log_functions.h       \
                    ../include/log_macros.h          \
                    ../include/nfs4.h                \
                    ../include/nfs_core.h            \
                    ../include/nfs_exports.h         \
                    ../include/nfs_file_handle.h     \
                    ../include/nfs_proto_functions.h \
                    ../include/nfs_tools.h           \
                    ../include/nlm4.h                \
                    ../include/nlm_util.h            \
                    ../include/nsm.h                 \
                    ../include/rpc.h                 \
                    ../include/sal_data.h            \
                    ../include/sal_functions.h       \
                    ../include/stuff_alloc.h

if USE_NLM
libsal_la_SOURCES += nlm_owner.c
endif

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

