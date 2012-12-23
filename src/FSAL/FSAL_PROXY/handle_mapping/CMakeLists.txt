
########### next target ###############

SET(handlemapping_STAT_SRCS
   handle_mapping.c
   handle_mapping.h
   handle_mapping_db.c
   handle_mapping_db.h
   handle_mapping_internal.h
)

add_library(handlemapping STATIC ${handlemapping_STAT_SRCS})


########### next target ###############

SET(test_handle_mapping_db_SRCS
   test_handle_mapping_db.c
)

add_executable(test_handle_mapping_db ${test_handle_mapping_db_SRCS})

target_link_libraries(test_handle_mapping_db handlemapping hashtable log common_utils rwlock sqlite3)


########### next target ###############

SET(test_handle_mapping_SRCS
   test_handle_mapping.c
)

add_executable(test_handle_mapping ${test_handle_mapping_SRCS})

target_link_libraries(test_handle_mapping handlemapping hashtable log common_utils rwlock sqlite3)


########### install files ###############




#original Makefile.am contents follow:

#AM_CFLAGS = $(SQLITE_CFLAGS)
#
#noinst_LTLIBRARIES          = libhandlemapping.la
#
#libhandlemapping_la_SOURCES = handle_mapping.c  handle_mapping.h  handle_mapping_db.c  handle_mapping_db.h handle_mapping_internal.h
#
#
#check_PROGRAMS              = test_handle_mapping_db test_handle_mapping
#test_handle_mapping_db_SOURCES      = test_handle_mapping_db.c
#test_handle_mapping_db_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la  $(top_srcdir)/Log/liblog.la \
#					$(top_srcdir)/Common/libcommon_utils.la -lsqlite3 
#
#
#test_handle_mapping_SOURCES      = test_handle_mapping.c
#test_handle_mapping_LDADD        = libhandlemapping.la $(top_srcdir)/HashTable/libhashtable.la $(top_srcdir)/Log/liblog.la \
#					$(top_srcdir)/Common/libcommon_utils.la -lsqlite3 
#
#new: clean all
#
