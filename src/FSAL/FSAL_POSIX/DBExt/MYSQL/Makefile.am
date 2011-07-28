AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsaldbext.la

libfsaldbext_la_SOURCES = posixdb_flush.c 	\
			  posixdb_internal.c    \
			  posixdb_internal.h    \
			  posixdb_add.c      	\
			  posixdb_consistency.c \
			  posixdb_info.c 	\
		          posixdb_lock.c	\
		          posixdb_delete.c      \
		          posixdb_getChildren.c \
			  posixdb_replace.c     \
			  posixdb_connect.c 

#check_PROGRAMS 		    = test_posixdb
#test_posixdb_SOURCES	    = test_posixdb.c
#test_posixdb_LDADD	    = libfsaldbext.la

EXTRA_DIST = posixdb_mysql5.sql

indent:
	indent -gnu -nut -i4 -npsl -di 15 -cd 50 -npcs -prs -l100 -hnl -bli0 *.[ch]
	rm *~

