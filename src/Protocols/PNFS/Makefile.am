AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_PNFS

noinst_LTLIBRARIES          = libpnfs.la

libpnfs_la_SOURCES = pnfs_common.c pnfs_glue.c 

if USE_PNFS_PARALLEL_FS
SUBDIRS =  PARALLEL_FS
libpnfs_la_LIBADD =  PARALLEL_FS/libpnfslayoutfile.la
endif 

if USE_PNFS_FSAL
SUBDIRS = FSAL
libpnfs_la_LIBADD =  FSAL/libpnfslayoutfile.la
endif

endif


