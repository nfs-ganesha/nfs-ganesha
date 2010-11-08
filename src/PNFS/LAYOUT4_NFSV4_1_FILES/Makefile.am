AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES = libpnfslayoutfile.la

libpnfslayoutfile_la_SOURCES = pnfs_init.c                 \
                               pnfs_encode_getdeviceinfo.c \
                               pnfs_encode_layoutget.c     \
                               pnfs_connect.c              \
                               pnfs_do_mount.c             \
                               pnfs_lookup.c               \
                               pnfs_ds_create_file.c       \
                               pnfs_ds_open_file.c         \
                               pnfs_ds_lookup_file.c       \
                               pnfs_ds_read_conf.c         \
                               pnfs_ds_unlink_file.c       \
                               pnfs_nfsv41_macros.h 

new: clean all
