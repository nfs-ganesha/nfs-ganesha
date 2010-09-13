AM_CFLAGS		      = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libidmap.la

check_PROGRAMS                = test_idmapper

libidmap_la_SOURCES = idmapper.c                   \
                      idmapper_cache.c             \
                      ../include/nfs_tools.h       \
                      ../include/HashData.h        \
                      ../include/HashTable.h       \
                      ../include/log_functions.h   \
                      ../include/stuff_alloc.h     \
                      ../include/nfs_core.h        \
                      ../include/config_parsing.h


test_idmapper_SOURCES       = test_idmapper.c

test_idmapper_LDADD = ../Log/liblog.la

new: clean all 

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
