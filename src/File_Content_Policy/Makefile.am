AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libcache_content_policy.la



libcache_content_policy_la_SOURCES = cache_content_cache_behaviour.c   \
                                     ../include/cache_content.h        \
                                     ../include/cache_content_policy.h \
                                     ../include/stuff_alloc.h          \
                                     ../include/LRU_List.h             \
                                     ../include/log_functions.h        \
                                     ../include/HashData.h             \
                                     ../include/HashTable.h            \
                                     ../include/fsal.h                 \
                                     ../include/cache_inode.h


new: clean all 

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
