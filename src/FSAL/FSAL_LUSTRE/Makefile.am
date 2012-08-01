AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_FSAL_LUSTRE

#noinst_LTLIBRARIES          = libfsallustre.la
lib_LTLIBRARIES          = libfsallustre.la

libfsallustre_la_SOURCES = fsal_convert.c   \
                           fsal_internal.c  \
                           fsal_internal.h  \
                           main.c           \
                           export.c         \
                           handle.c         \
                           file.c           \
                           xattrs.c         \
                          ../../include/fsal.h       \
                          ../../include/fsal_types.h \
                          ../../include/err_fsal.h   \
                          ../../include/FSAL/FSAL_LUSTRE/fsal_types.h \
                          ../../include/FSAL/FSAL_LUSTRE/fsal_handle.h

libfsallustre_la_CFLAGS                = $(FSAL_CFLAGS) $(SEC_CFLAGS) @LUSTRE_CFLAGS@
libfsallustre_la_LDFLAGS               = @LUSTRE_LDFLAGS@
libfsallustre_la_LIBADD                = @LUSTRE_LIB@

if USE_PNFS_MDS
libfsallustre_la_SOURCES += fsal_mds.c        \
                            fsal_pnfs_tools.c
endif

if USE_PNFS_DS
libfsallustre_la_SOURCES += fsal_ds.c 
endif

endif

if USE_FSAL_SHOOK

#noinst_LTLIBRARIES          = libfsalshook.la
lib_LTLIBRARIES          = libfsalshook.la

libfsalshook_la_SOURCES = fsal_convert.c   \
                          fsal_internal.c  \
                          fsal_internal.h  \
                          main.c           \
                          export.c         \
                          handle.c         \
                          file.c           \
                          xattrs.c         \
                          ../../include/fsal.h       \
                          ../../include/fsal_types.h \
                          ../../include/err_fsal.h   \
                          ../../include/FSAL/FSAL_LUSTRE/fsal_types.h

libfsalshook_la_CFLAGS                = $(FSAL_CFLAGS) $(SEC_CFLAGS) @LUSTRE_CFLAGS@
libfsalshook_la_LDFLAGS               = @LUSTRE_LDFLAGS@
libfsalshook_la_LIBADD                = @LUSTRE_LIB@

if USE_PNFS_MDS
libfsalshook_la_SOURCES += fsal_mds.c
endif

if USE_PNFS_DS
libfsalshook_la_SOURCES += fsal_ds.c
endif


endif


new: clean all


