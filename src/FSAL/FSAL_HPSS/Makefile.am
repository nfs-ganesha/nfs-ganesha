SUBDIRS = HPSSclapiExt

AM_CFLAGS=$(FSAL_CFLAGS) $(SEC_CFLAGS)

if BUILD_SHARED_FSAL

lib_LTLIBRARIES = libfsalhpss.la
libfsalhpss_la_LDFLAGS = -version-number @LIBVERSION@
libfsalhpss_la_LIBADD = ../../SemN/libSemN.la  $(FSAL_LDFLAGS) HPSSclapiExt/libhpssapiext.la

else
noinst_LTLIBRARIES          = libfsalhpss.la
endif


libfsalhpss_la_SOURCES = fsal_common.h  fsal_compat.c  fsal_convert.h  fsal_internal.h \
			 fsal_access.c  fsal_context.c  fsal_dirs.c    fsal_fsinfo.c \
			 fsal_lock.c    fsal_rcp.c      fsal_truncate.c \
			 fsal_attrs.c   fsal_convert.c  fsal_errors.c  fsal_init.c \
			 fsal_lookup.c  fsal_rename.c   fsal_symlinks.c fsal_unlink.c \
			 fsal_common.c  fsal_create.c   fsal_fileop.c  fsal_internal.c \
			 fsal_objectres.c fsal_stats.c  fsal_tools.c   fsal_xattrs.c  \
                         fsal_local_op.c fsal_quota.c\
			 ../../include/fsal.h ../../include/fsal_types.h ../../include/FSAL/FSAL_HPSS/fsal_types.h \
                         ../../include/err_fsal.h 

check_PROGRAMS 		    = test_fsal_hpss
test_fsal_hpss_SOURCES	    = test_fsal.c 
test_fsal_hpss_LDADD        = $(FSAL_LIB) $(FSAL_LDFLAGS) -lpthread

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

