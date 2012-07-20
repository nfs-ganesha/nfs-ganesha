AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS) -ldl

noinst_LTLIBRARIES          = libfsalcommon.la

libfsalcommon_la_SOURCES = fsal_errors.c           \
                           fsal_convert.c          \
                           fsal_commonlib.c	   \
			   fsal_manager.c          \
			   access_check.c	   \
			   common_functions.c      \
			   fsal_config.c	   \
			   default_methods.c       \
                           common_pnfs.c           \
			   ../include/FSAL/access_check.h \
			   ../include/FSAL/common_functions.h \
			   ../include/FSAL/fsal_commonlib.h \
			   ../include/FSAL/fsal_init.h \
                           ../include/fsal.h       \
                           ../include/fsal_types.h \
                           ../include/err_fsal.h   \
                           ../include/fsal_convert.h 


# What's next is required to allow dynamic FSAL to compile
# COMMON_* functions are in the current directory, it must be compiled before the FSAL_* directories

SUBDIRS = . 

if USE_FSAL_HPSS
SUBDIRS += FSAL_HPSS
endif

if USE_FSAL_POSIX
SUBDIRS += FSAL_POSIX
endif

if USE_FSAL_PROXY
SUBDIRS += FSAL_PROXY
endif

if USE_FSAL_CEPH
SUBDIRS += FSAL_CEPH
endif

if USE_FSAL_FUSE
SUBDIRS += FSAL_FUSELIKE
endif

if USE_FSAL_LUSTRE
SUBDIRS += FSAL_LUSTRE
endif

if USE_FSAL_SHOOK
SUBDIRS += FSAL_LUSTRE
endif

if USE_FSAL_XFS
SUBDIRS += FSAL_XFS
endif

if USE_FSAL_GPFS
SUBDIRS += FSAL_GPFS
endif

if USE_FSAL_ZFS
SUBDIRS += FSAL_ZFS
endif

if USE_FSAL_VFS
SUBDIRS += FSAL_VFS
endif

