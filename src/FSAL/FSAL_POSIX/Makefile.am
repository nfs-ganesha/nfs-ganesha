AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

SUBDIRS	= DBExt

noinst_LTLIBRARIES          = libfsalposix.la

libfsalposix_la_SOURCES = fsal_access.c      \
                          fsal_context.c     \
                          fsal_dirs.c        \
                          fsal_fsinfo.c      \
                          fsal_lock.c        \
                          fsal_rcp.c         \
                          fsal_strings.c     \
                          fsal_truncate.c    \
			  fsal_attrs.c       \
                          fsal_convert.c     \
                          fsal_errors.c      \
                          fsal_init.c        \
                          fsal_lookup.c      \
                          fsal_rename.c      \
                          fsal_symlinks.c    \
                          fsal_unlink.c      \
			  fsal_create.c      \
                          fsal_fileop.c      \
                          fsal_internal.c    \
                          fsal_objectres.c   \
                          fsal_stats.c       \
                          fsal_tools.c       \
                          fsal_xattrs.c      \
			  fsal_create.c      \
                          fsal_fileop.c      \
                          fsal_internal.c    \
                          fsal_objectres.c   \
                          fsal_stats.c       \
                          fsal_tools.c       \
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
posix_ganesha_fsal_posixdb_tool_LDADD      = ../../BuddyMalloc/libBuddyMalloc.la                  \
                                             $(FSAL_LIB)                                          \
                                             $(FSAL_LDFLAGS)                                      \
                                             ../../Log/liblog.la                                  \
                                             ../../ConfigParsing/libConfigParsing.la              \
                                             ../../SemN/libSemN.la                                \
                                             ../../RW_Lock/librwlock.la                           \
                                             ../../Common/libcommon_utils.la


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

