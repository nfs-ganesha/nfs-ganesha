AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libmfslpnfs_file.la

libmfslpnfs_file_la_SOURCES = mfsl_pnfs_file.c        \
                              pnfs_functions.c        \
                              pnfs_ds_create_file.c   \
                              pnfs_ds_get_location.c  \
                              pnfs_ds_open_file.c     \
                              pnfs_ds_truncate_file.c \
                              pnfs_ds_read_conf.c     \
                              pnfs_ds_unlink_file.c   \
			      pnfs_ds_lookup_file.c   \
                              pnfs_lookup.c           \
                              pnfs_init.c 

libmfslpnfs_file_la_LIBADD = $(FSAL_LIB)


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

