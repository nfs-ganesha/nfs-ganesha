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


TIRPC_INC_PATH  = -I/usr/include/tirpc
TIRPC_LIB_PATH  = 
TIRPC_LIB_FLAGS = -ltirpc  

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


all: toto-client-tirpc       \
     toto-server-tirpc       \
     toto-client-tirpc-nfs4  \
     toto-client-tirpc-ipv6  \
     toto-server-tirpc-ipv6  

%.o: %.c 
	$(CC) $(INCP_FLAGS) $(RPCSEC_GSS_INC_PATH)  $(GSS_INC_PATH) -c $< -o $(@F)

%gssrpc-nfs4.o:%gssrpc-nfs4.c
	$(CC) $(INCP_FLAGS) $(RPCSEC_GSS_INC_PATH) $(GSS_INC_PATH) -I../../../src/include  -c $< -o $(@F)

clean:
	rm -fr $(CLIENT_GSSRPC) $(DAEMON_GSSRPC) $(CLIENT_GSS) $(DAEMON_GSS) $(CLIENT_NFS_GSSRPC)  
	rm -fr $(DAEMON_GSSRPC) $(CLIENT_GSSRPC) $(DAEMON_TIRPC_IPV6) $(CLIENT_TIRPC_IPV6) 
	rm -f *.o 


new: clean all

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

