AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libghostfs.la

libghostfs_la_SOURCES 	    =  ghost_fs.c

check_PROGRAMS 		     = test_ghost_fs
test_ghost_fs_SOURCES    = test_ghost_fs.c 
test_ghost_fs_LDADD      = libghostfs.la -lpthread
