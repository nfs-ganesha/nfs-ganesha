AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libcache_content.la

#check_PROGRAMS                = test_threshold 

libcache_content_la_SOURCES = cache_content_init.c            \
                              cache_content_rdwr.c            \
                              cache_content_truncate.c        \
                              cache_content_read_conf.c       \
                              cache_content_add_entry.c       \
                              cache_content_release_entry.c   \
                              cache_content_flush.c           \
                              cache_content_misc.c            \
                              cache_content_gc.c              \
                              cache_content_crash_recover.c   \
                              cache_content_emergency_flush.c \
                              ../include/cache_content.h      \
                              ../include/stuff_alloc.h        \
                              ../include/LRU_List.h           \
                              ../include/log_functions.h      \
                              ../include/HashData.h           \
                              ../include/HashTable.h          \
                              ../include/fsal.h               \
                              ../include/cache_inode.h        \
                              ../include/err_cache_content.h

new: clean all 

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
