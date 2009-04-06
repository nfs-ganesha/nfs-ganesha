AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsaldbext.la

libfsaldbext_la_SOURCES = posixdb_add.c      posixdb_consistency.c  posixdb_flush.c        posixdb_info.c      posixdb_lock.c \
		          posixdb_connect.c  posixdb_delete.c       posixdb_getChildren.c  posixdb_internal.c  posixdb_replace.c \
			  posixdb_internal.h

#check_PROGRAMS 		    = test_posixdb
#test_posixdb_SOURCES	    = test_posixdb.c
#test_posixdb_LDADD	    = libfsaldbext.la

EXTRA_DIST = posixdb_v7.sql posixdb_v8.sql
