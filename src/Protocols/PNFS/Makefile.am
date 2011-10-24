AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_PNFS


noinst_LTLIBRARIES          = libpnfs.la

libpnfs_la_SOURCES = pnfs_glue.c 

if USE_PNFS_PARALLEL_FS
SUBDIRS =  PARALLEL_FS
libpnfs_la_LIBADD =  PARALLEL_FS/libpnfslayoutfile.la
endif 

if USE_PNFS_OSD
SUBDIRS = OSD
libpnfs_la_LIBADD =  OSD/libpnfslayoutfile.la
endif
 
if USE_PNFS_BLOCK
SUBDIRS = BLOCK
libpnfs_la_LIBADD =  BLOCK/libpnfslayoutfile.la
endif
 
endif
