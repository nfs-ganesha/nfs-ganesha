#
# Makefile de l'outil de gestion des retentions
#
# @(#): $Id: GNUmakefile,v 1.6 2003/10/03 07:22:30 deniel Exp $
#
CC     = gcc -g -Werror -DHAVE_KRB5
RPCGEN = /usr/bin/rpcgen 

INCP_FLAGS = -I. 
LIBP_FLAGS      = 
LIB_FLAGS       = -lpthread

GSS_LIB_FLAGS =  -lgssapi_krb5 -lkrb5 -lk5crypto  -lresolv  -ldl
#GSS_LIB_PATH =  -L/usr/local/lib 
GSS_LIB_PATH =  
#GSS_INC_PATH = -I/usr/local/include/gssglue
GSS_INC_PATH = 

#RPCSEC_GSS_INC_PATH  = -I/usr/local/include/
RPCSEC_GSS_INC_PATH  = 
#RPCSEC_GSS_LIB_PATH  = -L/usr/local/lib
RPCSEC_GSS_LIB_PATH  = 
RPCSEC_GSS_LIB_FLAGS = -lrpcsecgss -lgssglue -lgssapi -lgssapi_krb5 -lkrb5 -lk5crypto  -lresolv  -ldl

GSSRPC_INC_PATH  = 
GSSRPC_LIB_PATH  = 
GSSRPC_LIB_FLAGS = -lgssrpc -lgssapi_krb5 -lkrb5 -lk5crypto -lresolv  -ldl


TIRPC_INC_PATH  = -I/usr/include/tirpc
TIRPC_LIB_PATH  = 
TIRPC_LIB_FLAGS = -ltirpc  

DAEMON_GSS        = toto-server-gss
SRC_DAEMON_GSS    = toto-server-gss.c tools-gss.c 
OBJ_DAEMON_GSS    = $(SRC_DAEMON_GSS:.c=.o)  

CLIENT_GSS	  = toto-client-gss
SRC_CLIENT_GSS    = toto-client-gss.c tools-gss.c  
OBJ_CLIENT_GSS    = $(SRC_CLIENT_GSS:.c=.o) 

DAEMON_RPCSEC_GSS      = toto-server-rpcsecgss
SRC_DAEMON_RPCSEC_GSS  = toto-server-rpcsecgss.c tools-gss.c
OBJ_DAEMON_RPCSEC_GSS  = $(SRC_DAEMON_RPCSEC_GSS:.c=.o) 

CLIENT_RPCSEC_GSS      = toto-client-rpcsecgss
SRC_CLIENT_RPCSEC_GSS  = toto-client-rpcsecgss.c tools-gss.c
OBJ_CLIENT_RPCSEC_GSS  = $(SRC_CLIENT_RPCSEC_GSS:.c=.o) 

DAEMON_GSSRPC      = toto-server-gssrpc
SRC_DAEMON_GSSRPC  = toto-server-gssrpc.c tools-gss.c
OBJ_DAEMON_GSSRPC  = $(SRC_DAEMON_GSSRPC:.c=.o) 

CLIENT_GSSRPC      = toto-client-gssrpc
SRC_CLIENT_GSSRPC  = toto-client-gssrpc.c tools-gss.c
OBJ_CLIENT_GSSRPC  = $(SRC_CLIENT_GSSRPC:.c=.o) 

CLIENT_NFS_GSSRPC      = toto-client-gssrpc-nfs4
SRC_CLIENT_NFS_GSSRPC  = toto-client-gssrpc-nfs4.c tools-gss.c
OBJ_CLIENT_NFS_GSSRPC  = $(SRC_CLIENT_NFS_GSSRPC:.c=.o) 

CLIENT_TIRPC      = toto-client-tirpc
SRC_CLIENT_TIRPC  = toto-client-tirpc.c
OBJ_CLIENT_TIRPC  = $(SRC_CLIENT_TIRPC:.c=.o) 

DAEMON_TIRPC      = toto-server-tirpc
SRC_DAEMON_TIRPC  = toto-server-tirpc.c
OBJ_DAEMON_TIRPC  = $(SRC_DAEMON_TIRPC:.c=.o) 

CLIENT_NFS_TIRPC      = toto-client-tirpc-nfs4
SRC_CLIENT_NFS_TIRPC  = toto-client-tirpc-nfs4.c 
OBJ_CLIENT_NFS_TIRPC  = $(SRC_CLIENT_NFS_TIRPC:.c=.o) 

CLIENT_TIRPC_IPV6      = toto-client-tirpc-ipv6
SRC_CLIENT_TIRPC_IPV6  = toto-client-tirpc-ipv6.c
OBJ_CLIENT_TIRPC_IPV6  = $(SRC_CLIENT_TIRPC_IPV6:.c=.o) 

DAEMON_TIRPC_IPV6      = toto-server-tirpc-ipv6
SRC_DAEMON_TIRPC_IPV6  = toto-server-tirpc-ipv6.c
OBJ_DAEMON_TIRPC_IPV6  = $(SRC_DAEMON_TIRPC_IPV6:.c=.o) 

CLIENT_NFS_TIRPC_IPV6      = toto-client-tirpc-ipv6-nfs4
SRC_CLIENT_NFS_TIRPC_IPV6  = toto-client-tirpc-ipv6-nfs4.c 
OBJ_CLIENT_NFS_TIRPC_IPV6  = $(SRC_CLIENT_NFS_TIRPC_IPV6:.c=.o) 


all: toto-server-gss              \
     toto-client-gss              \
     toto-server-gssrpc           \
     toto-client-gssrpc           \
     toto-client-gssrpc-nfs4      \
     toto-client-tirpc            \
     toto-server-tirpc            \
     toto-client-tirpc-nfs4       \
     toto-server-tirpc-ipv6       \
     toto-client-tirpc-ipv6       \
     toto-client-tirpc-ipv6-nfs4  \
     toto-server-rpcsecgss        \
     toto-client-rpcsecgss   

%.o: %.c 
	$(CC) $(INCP_FLAGS) $(RPCSEC_GSS_INC_PATH)  $(GSS_INC_PATH) -c $< -o $(@F)

%gssrpc-nfs4.o:%gssrpc-nfs4.c
	$(CC) $(INCP_FLAGS) $(RPCSEC_GSS_INC_PATH) $(GSS_INC_PATH) -I../../../src/include  -c $< -o $(@F)

clean:
	rm -fr $(CLIENT_GSSRPC) $(DAEMON_GSSRPC) $(CLIENT_GSS) $(DAEMON_GSS) $(CLIENT_NFS_GSSRPC)  
	rm -fr $(DAEMON_GSSRPC) $(CLIENT_GSSRPC) $(DAEMON_TIRPC_IPV6) $(CLIENT_TIRPC_IPV6) 
	rm -fr $(CLIENT_TIRPC)  $(DAEMON_TIRPC) $(CLIENT_NFS_TIRPC)
	rm -fr $(CLIENT_TIRPC_IPV6)  $(DAEMON_TIRPC_IPV6) $(CLIENT_NFS_TIRPC_IPV6)
	rm -f *.o 


new: clean all

$(DAEMON_GSS):  $(INC_DAEMON) $(OBJ_DAEMON_GSS) 
	$(CC) $(INCP_FLAGS) $(GSS_INC_PATH) $(GSS_LIB_PATH)  $(LIBP_FLAGS) $(OBJ_DAEMON_GSS) -o $@ $(LIB_FLAGS) $(GSS_LIB_FLAGS)

$(CLIENT_GSS): $(INC_CLIENT) $(OBJ_CLIENT_GSS)
	$(CC) $(INCP_FLAGS) $(GSS_INC_PATH) $(GSS_LIB_PATH)  $(LIBP_FLAGS) $(OBJ_CLIENT_GSS) -o $@ $(LIB_FLAGS) $(GSS_LIB_FLAGS)

$(DAEMON_GSSRPC): $(OBJ_DAEMON_GSSRPC)
	$(CC) $(INCP_FLAGS) $(GSSRPC_INC_PATH) $(GSSRPC_LIB_PATH)  $(LIBP_FLAGS) $(OBJ_DAEMON_GSSRPC) -o $@ $(LIB_FLAGS) $(GSSRPC_LIB_FLAGS) 

$(CLIENT_GSSRPC): $(OBJ_CLIENT_GSSRPC)
	$(CC) $(INCP_FLAGS) $(GSSRPC_INC_PATH) $(GSSRPC_LIB_PATH)  $(LIBP_FLAGS) $(OBJ_CLIENT_GSSRPC) -o $@ $(LIB_FLAGS) $(GSSRPC_LIB_FLAGS) 

$(DAEMON_RPCSEC_GSS): $(OBJ_DAEMON_RPCSEC_GSS)
	$(CC) $(INCP_FLAGS)  $(RPCSEC_GSS_INC_PATH) $(LIBP_FLAGS)  $(OBJ_DAEMON_RPCSEC_GSS) -o $@ $(LIB_FLAGS) $(RPCSEC_GSS_LIB_FLAGS) 

$(CLIENT_RPCSEC_GSS): $(OBJ_CLIENT_RPCSEC_GSS)
	$(CC) $(INCP_FLAGS)  $(RPCSEC_GSS_INC_PATH) $(LIBP_FLAGS) $(OBJ_CLIENT_RPCSEC_GSS) -o $@ $(LIB_FLAGS) $(RPCSEC_GSS_LIB_FLAGS) 

$(CLIENT_TIRPC): $(SRC_CLIENT_TIRPC)
	$(CC) -g $(INCP_FLAGS) $(TIRPC_INC_PATH) $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_CLIENT_TIRPC) -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(DAEMON_TIRPC): $(SRC_DAEMON_TIRPC)
	$(CC) -g $(INCP_FLAGS) $(TIRPC_INC_PATH) $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_DAEMON_TIRPC) -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(CLIENT_TIRPC_IPV6): $(SRC_CLIENT_TIRPC_IPV6)
	$(CC) -g $(INCP_FLAGS) $(TIRPC_INC_PATH) $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_CLIENT_TIRPC_IPV6) -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(DAEMON_TIRPC_IPV6): $(SRC_DAEMON_TIRPC_IPV6)
	$(CC) -g $(INCP_FLAGS) $(TIRPC_INC_PATH) $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_DAEMON_TIRPC_IPV6) -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(CLIENT_NFS_TIRPC): $(SRC_CLIENT_NFS_TIRPC)
	$(CC) $(INCP_FLAGS) $(TIRPC_INC_PATH) -I../../../src/include $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_CLIENT_NFS_TIRPC) ../../../src/XDR/.libs/libnfs_mnt_xdr.a -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(CLIENT_NFS_TIRPC_IPV6): $(SRC_CLIENT_NFS_TIRPC_IPV6)
	$(CC) $(INCP_FLAGS) $(TIRPC_INC_PATH) -I../../../src/include $(TIRPC_LIB_PATH)  $(LIBP_FLAGS) $(SRC_CLIENT_NFS_TIRPC_IPV6) ../../../src/XDR/.libs/libnfs_mnt_xdr.a -o $@ $(LIB_FLAGS) $(TIRPC_LIB_FLAGS) 

$(CLIENT_NFS_GSSRPC): $(OBJ_CLIENT_NFS_GSSRPC)
	$(CC) $(INCP_FLAGS) $(GSSRPC_INC_PATH) -I../../../src/include $(GSSRPC_LIB_PATH)  $(LIBP_FLAGS) $(OBJ_CLIENT_NFS_GSSRPC) ../../../src/XDR/.libs/libnfs_mnt_xdr.a -o $@ $(LIB_FLAGS) $(GSSRPC_LIB_FLAGS) 

