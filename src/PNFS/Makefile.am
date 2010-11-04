AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if USE_PNFS

SUBDIRS = LAYOUT4_NFSV4_1_FILES

noinst_LTLIBRARIES          = libpnfs.la

libpnfs_la_SOURCES = pnfs_glue.c 

libpnfs_la_LIBADD =  LAYOUT4_NFSV4_1_FILES/libpnfslayoutfile.la
 
endif
