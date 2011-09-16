AM_CFLAGS                     = -Wimplicit $(FSAL_CFLAGS) $(SEC_CFLAGS)

used_libs  = $(FSAL_LIB) $(FSAL_LDFLAGS)             \
             ../../Log/liblog.la                     \
             ../../HashTable/libhashtable.la         \
             ../../BuddyMalloc/libBuddyMalloc.la     \
             ../../RW_Lock/librwlock.la              \
             ../../SemN/libSemN.la                   \
             ../../ConfigParsing/libConfigParsing.la \
             ../../Common/libcommon_utils.la         \
             -lpthread

noinst_LTLIBRARIES          = libfsalfuse.la


libfsalfuse_la_SOURCES = fsal_access.c  fsal_context.c  fsal_dirs.c    fsal_fsinfo.c \
			 fsal_rcp.c     fsal_compat.c   fsal_truncate.c \
			 fsal_attrs.c   fsal_convert.c  fsal_init.c      fsal_lookup.c \
			 fsal_rename.c  fsal_symlinks.c  fsal_unlink.c fsal_local_op.c \
			 fsal_common.c  fsal_create.c   fsal_fileop.c  fsal_internal.c \
			 fsal_stats.c   fsal_tools.c     fsal_xattrs.c \
                         fsal_common.h  fsal_convert.h  fsal_internal.h \
			 fsal_fuse_helper.c namespace.c namespace.h \
			 ../../include/fsal.h ../../include/fsal_types.h \
			 ../../include/FSAL/FSAL_FUSELIKE/fsal_types.h \
			 ../../include/ganesha_fuse_wrap.h

check_PROGRAMS 		    = test_namespace_fuse

test_namespace_fuse_SOURCES  = test_ns.c
test_namespace_fuse_LDADD     = $(used_libs)

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

