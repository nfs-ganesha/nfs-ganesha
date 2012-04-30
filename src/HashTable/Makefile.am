noinst_LTLIBRARIES            = libhashtable.la

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = ../BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

libhashtable_la_SOURCES       = HashTable.c                \
                                ../include/HashTable.h     \
                                ../include/HashData.h      \
                                ../include/err_HashTable.h
   
new: clean all
