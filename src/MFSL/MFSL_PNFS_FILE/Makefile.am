AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libmfslpnfs_file.la

libmfslpnfs_file_la_SOURCES = mfsl_pnfs_file.c

libmfslpnfs_file_la_LIBADD = $(FSAL_LIB)


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

