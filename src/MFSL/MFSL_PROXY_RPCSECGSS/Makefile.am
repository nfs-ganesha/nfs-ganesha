AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES          = libmfslproxy_rpcsecgss.la

libmfslproxy_rpcsecgss_la_SOURCES = mfsl_proxy_rpcsecgss.c

libmfslproxy_rpcsecgss_la_LIBADD = $(FSAL_LIB)

#check_PROGRAMS 	     = test_mfsl_template
#test_mfsl_template_SOURCES  = test_fsal.c 
#test_mfsl_template_LDADD    = $(FSAL_LIB) $(FSAL_LDFLAGS) -lpthread


new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

