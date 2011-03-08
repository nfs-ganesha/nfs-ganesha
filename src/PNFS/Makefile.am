AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_PNFS


noinst_LTLIBRARIES          = libpnfs.la

libpnfs_la_SOURCES = pnfs_glue.c 

if USE_PNFS_PARALLEL_FS
SUBDIRS =  LAYOUT4_NFSV4_1_FILES_LUSTRE
libpnfs_la_LIBADD =  LAYOUT4_NFSV4_1_FILES_LUSTRE/libpnfslayoutfile.la
endif 

if USE_PNFS_SPNFS_LIKE
SUBDIRS = LAYOUT4_NFSV4_1_FILES
libpnfs_la_LIBADD =  LAYOUT4_NFSV4_1_FILES/libpnfslayoutfile.la
endif
 
endif
