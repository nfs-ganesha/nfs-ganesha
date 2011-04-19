AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES          = libfsallustrepnfs.la


libfsallustrepnfs_la_SOURCES = fsal_pnfs_functions.c


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

