AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libsal.la

#check_PROGRAMS                = test_cache_inode test_cache_inode_readlink \
#                                test_cache_inode_readdir test_cache_inode_lookup 

libsal_la_SOURCES = state_lock.c \
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

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

