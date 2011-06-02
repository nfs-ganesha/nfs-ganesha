SEC_LIB_FLAGS = 
# SVC_FLAGS = -I/usr/include/tirpc
# SVC_LIB_FLAGS = -ltirpc
AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcalcore.la

librpcalcore_la_SOURCES = tirpc.h \
                          Svc_tirpc.c    \
                          Svc_vc_tirpc.c \
                          Svc_dg_tirpc.c \
                          Xdr_rec.c

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make


