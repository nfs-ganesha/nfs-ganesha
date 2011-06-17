SEC_LIB_FLAGS = 
SVC_FLAGS =
SVC_LIB_FLAGS = 
AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcalcore.la

noinst_HEADERS = oncrpc.h

librpcalcore_la_SOURCES = Svc_oncrpc.c     \
                          Svc_tcp_oncrpc.c \
                          Svc_udp_oncrpc.c 

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make


