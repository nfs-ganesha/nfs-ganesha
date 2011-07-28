AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libmfslasync.la

libmfslasync_la_SOURCES = mfsl_async.c              \
                          mfsl_async_read_conf.c    \
                          mfsl_async_init.c         \
                          mfsl_async_mkdir.c        \
                          mfsl_async_create.c       \
                          mfsl_async_link.c         \
                          mfsl_async_unlink.c       \
                          mfsl_async_rename.c       \
                          mfsl_async_symlink.c      \
                          mfsl_async_truncate.c     \
                          mfsl_async_setattr.c      \
                          mfsl_async_getattr.c      \
                          mfsl_async_lookup.c       \
                          mfsl_async_open_by_name.c \
                          mfsl_async_synclet.c      \
                          mfsl_async_hash.c


libmfslasync_la_LIBADD = $(FSAL_LIB)

#check_PROGRAMS 	     = test_mfsl_template
#test_mfsl_template_SOURCES  = test_fsal.c 
#test_mfsl_template_LDADD    = $(FSAL_LIB) $(FSAL_LDFLAGS) -lpthread


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

