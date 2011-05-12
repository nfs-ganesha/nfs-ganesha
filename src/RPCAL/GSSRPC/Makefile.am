SEC_LIB_FLAGS = $(SEC_LFLAGS) -lgssrpc -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err -ldl
SVC_FLAGS =
SVC_LIB_FLAGS = 
AM_CFLAGS = -Wimplicit $(DLOPEN_FLAGS)  $(FSAL_CFLAGS) $(SEC_CFLAGS) $(SVC_FLAGS)

noinst_LTLIBRARIES = librpcalcore.la

librpcalcore_la_SOURCES = Svc_gssrpc.c     \
                          Svc_tcp_gssrpc.c \
                          Svc_udp_gssrpc.c

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make


