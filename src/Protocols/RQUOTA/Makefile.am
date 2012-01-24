AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES            = librquota.la



librquota_la_SOURCES = rquota_Null.c            \
                       rquota_getquota.c        \
                       rquota_getactivequota.c  \
                       rquota_setquota.c        \
                       rquota_setactivequota.c


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
