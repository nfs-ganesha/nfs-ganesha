AM_CFLAGS = $(SQLITE_CFLAGS)

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = $(top_srcdir)/BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

noinst_LTLIBRARIES          = libhandlemapping.la

libhandlemapping_la_SOURCES = handle_mapping.c  handle_mapping.h  handle_mapping_db.c  handle_mapping_db.h handle_mapping_internal.h


check_PROGRAMS              = test_handle_mapping_db test_handle_mapping
test_handle_mapping_db_SOURCES      = test_handle_mapping_db.c
test_handle_mapping_db_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la  $(top_srcdir)/Log/liblog.la \
				 	$(BUDDY_LIB_FLAGS) \
					$(top_srcdir)/Common/libcommon_utils.la $(top_srcdir)/RW_Lock/librwlock.la -lsqlite3 


test_handle_mapping_SOURCES      = test_handle_mapping.c
test_handle_mapping_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la $(top_srcdir)/Log/liblog.la \
					$(BUDDY_LIB_FLAGS) \
					$(top_srcdir)/Common/libcommon_utils.la $(top_srcdir)/RW_Lock/librwlock.la -lsqlite3 

new: clean all

