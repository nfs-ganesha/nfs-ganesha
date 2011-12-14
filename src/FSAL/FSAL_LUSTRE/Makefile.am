AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_FSAL_LUSTRE

if BUILD_SHARED_FSAL
lib_LTLIBRARIES = libfsallustre.la
libfsallustre_la_LDFLAGS = -version-number @LIBVERSION@
libfsallustre_la_LIBADD = ../../SemN/libSemN.la -llustreapi  ../libfsalcommon.la 
else
noinst_LTLIBRARIES          = libfsallustre.la
endif

libfsallustre_la_SOURCES = fsal_access.c      \
                          fsal_context.c     \
                          fsal_compat.c      \
                          fsal_dirs.c        \
                          fsal_fsinfo.c      \
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
                          fsal_xattrs.c      \
                          fsal_local_op.c    \
                          fsal_lock.c     \
			  fsal_internal.h    \
                          fsal_convert.h     \
                          ../../include/fsal.h       \
                          ../../include/fsal_types.h \
                          ../../include/err_fsal.h   \
                          ../../include/FSAL/FSAL_LUSTRE/fsal_types.h

if USE_PNFS
SUBDIRS = PNFS
libfsallustre_la_LIBADD = Protocols/PNFS/libfsallustrepnfs.la
libfsallustre_la_DEPENDENCIES =  $(libfsallustre_la_SOURCES) $(libfsallustre_la_LIBADD)
endif 

endif

if USE_FSAL_SHOOK

if BUILD_SHARED_FSAL
lib_LTLIBRARIES = libfsalshook.la
libfsalshook_la_LDFLAGS = -version-number @LIBVERSION@
libfsalshook_la_LIBADD = ../../SemN/libSemN.la 
else
noinst_LTLIBRARIES          = libfsalshook.la
endif

libfsalshook_la_SOURCES = fsal_access.c      \
                          fsal_context.c     \
                          fsal_compat.c      \
                          fsal_dirs.c        \
                          fsal_fsinfo.c      \
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
                          fsal_xattrs.c      \
                          fsal_local_op.c    \
                          fsal_lock.c     \
			  fsal_internal.h    \
                          fsal_convert.h     \
                          ../../include/fsal.h       \
                          ../../include/fsal_types.h \
                          ../../include/err_fsal.h   \
                          ../../include/FSAL/FSAL_LUSTRE/fsal_types.h


if USE_PNFS
SUBDIRS = PNFS
libfsalshook_la_LIBADD = PNFS/libfsallustrepnfs.la
libfsalshook_la_DEPENDENCIES =  $(libfsalshook_la_SOURCES) $(libfsalshook_la_LIBADD)
endif 


endif


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

