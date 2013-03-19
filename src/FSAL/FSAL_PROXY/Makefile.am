AM_CFLAGS = $(FSAL_CFLAGS) $(SEC_CFLAGS)

if ENABLE_HANDLE_MAPPING
SUBDIRS = handle_mapping
endif

noinst_LTLIBRARIES          = libfsalproxy.la

libfsalproxy_la_SOURCES = fsal_context.c        \
                          fsal_dirs.c           \
                          fsal_fsinfo.c         \
                          fsal_rcp.c            \
                          fsal_truncate.c       \
			  fsal_attrs.c          \
                          fsal_init.c           \
                          fsal_lookup.c         \
                          fsal_rename.c         \
                          fsal_symlinks.c       \
                          fsal_unlink.c         \
			  fsal_common.c         \
                          fsal_create.c         \
                          fsal_fileop.c         \
                          fsal_internal.c       \
                          fsal_stats.c          \
                          fsal_tools.c          \
                          fsal_xattrs.c         \
                          fsal_compat.c         \
                          fsal_proxy_internal.c \
                          fsal_proxy_clientid.c \
                          fsal_common.h         \
                          fsal_convert.h        \
                          fsal_internal.h       \
                          fsal_nfsv4_macros.h   \
                          ../../include/fsal.h                           \
                          ../../include/fsal_types.h                     \
                          ../../include/FSAL/FSAL_PROXY/fsal_types.h     \
                          ../../include/err_fsal.h

new: clean all

