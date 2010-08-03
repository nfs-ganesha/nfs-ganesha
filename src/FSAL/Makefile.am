AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsalcommon.la

libfsalcommon_la_SOURCES = fsal_strings.c          \
                           fsal_errors.c           \
                           fsal_convert.c          \
                           fsal_glue.c             \
                           ../include/fsal.h       \
                           ../include/fsal_types.h \
                           ../include/err_fsal.h   \
                           ../include/fsal_convert.h 

if USE_FSAL_GHOSTFS
SUBDIRS = FSAL_GHOST_FS
endif

if USE_FSAL_HPSS
SUBDIRS = FSAL_HPSS
endif

if USE_FSAL_POSIX
SUBDIRS = FSAL_POSIX
endif

if USE_FSAL_PROXY
SUBDIRS = FSAL_PROXY
endif

if USE_FSAL_SNMP
SUBDIRS = FSAL_SNMP
endif

if USE_FSAL_FUSE
SUBDIRS = FSAL_FUSELIKE
endif

if USE_FSAL_LUSTRE
SUBDIRS = FSAL_LUSTRE
endif

if USE_FSAL_XFS
SUBDIRS = FSAL_XFS
endif

if USE_FSAL_GPFS
SUBDIRS = FSAL_GPFS
endif

if USE_FSAL_TEMPLATE
SUBDIRS = FSAL_TEMPLATE 
endif

if USE_FSAL_ZFS
SUBDIRS = FSAL_ZFS
endif

