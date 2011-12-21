AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = $(top_srcdir)/BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

SUBDIRS	= DBExt

if BUILD_SHARED_FSAL

lib_LTLIBRARIES = libfsalposix.la
libfsalposix_la_LDFLAGS = -version-number @LIBVERSION@

if USE_PGSQL
libfsalposix_la_LIBADD = ../../SemN/libSemN.la  ../libfsalcommon.la  ./DBExt/PGSQL/libfsaldbext.la $(FSAL_LDFLAGS)
endif
if USE_MYSQL
libfsalposix_la_LIBADD = ../../SemN/libSemN.la  ../libfsalcommon.la  ./DBExt/MYSQL/libfsaldbext.la $(FSAL_LDFLAGS) 
endif
else

noinst_LTLIBRARIES          = libfsalposix.la

endif

libfsalposix_la_SOURCES = fsal_access.c      \
                          fsal_compat.c      \
                          fsal_context.c     \
                          fsal_dirs.c        \
                          fsal_fsinfo.c      \
                          fsal_lock.c        \
                          fsal_rcp.c         \
                          fsal_truncate.c    \
			  fsal_attrs.c       \
                          fsal_convert.c     \
                          fsal_init.c        \
                          fsal_lookup.c      \
                          fsal_rename.c      \
                          fsal_symlinks.c    \
                          fsal_unlink.c      \
			  fsal_create.c      \
                          fsal_fileop.c      \
                          fsal_internal.c    \
                          fsal_stats.c       \
                          fsal_tools.c       \
                          fsal_quota.c       \
                          fsal_xattrs.c      \
                          fsal_local_op.c    \
			  fsal_internal.h    \
                          fsal_convert.h     \
                          ../../include/fsal.h       \
                          ../../include/fsal_types.h \
                          ../../include/err_fsal.h   \
                          ../../include/FSAL/FSAL_POSIX/fsal_types.h \
                          ../../include/FSAL/FSAL_POSIX/posixdb.h    \
                          ../../include/posixdb_consistency.h




#check_PROGRAMS 		    = test_fsal_posix 
#test_fsal_posix_SOURCES	    = test_fsal.c 
#test_fsal_posix_LDADD       = $(FSAL_LIB) $(FSAL_LDFLAGS) -lpthread

bin_PROGRAMS                               = posix.ganesha.fsal_posixdb_tool
posix_ganesha_fsal_posixdb_tool_SOURCES    = fsal_posixdb_tool.c 
posix_ganesha_fsal_posixdb_tool_LDADD      = $(BUDDY_LIB_FLAGS)                                   \
                                             $(FSAL_LIB)                                          \
                                             $(FSAL_LDFLAGS)                                      \
                                             ../../Log/liblog.la                                  \
                                             ../../ConfigParsing/libConfigParsing.la              \
                                             ../../SemN/libSemN.la                                \
                                             ../../RW_Lock/librwlock.la                           \
                                             ../fsal_strings.lo                                   \
                                             ../fsal_convert.lo                                   \
					     ../fsal_errors.lo					  \
                                             ../../Common/libcommon_utils.la


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

