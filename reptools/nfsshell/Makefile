#
# Makefile
#
# Copyright (c) 1990..1998 by Leendert van Doorn <leendert@cs.vu.nl>
# All rights reserved.
#
# For solaris 2.x you need
#	CFLAGS=-DSYSV
#	LIBS=-lsocket -L/usr/ucblib -R/usr/ucblib -lrpcsoc -lnsl
#
# For AIX you need
#	CFLAGS=-DAIX
#	LIBS=
#
# For Linux you need (don't use the GNU lines below)
#       CFLAGS=-DREADLINE -I/usr/local/include
#	LIBS=-L/usr/local/lib -lreadline -lhistory -lncurses
#
# For GNU readline support you need to add
	CFLAGS=-DREADLINE -I/usr/local/include
	LIBS=-L/usr/local/lib -lreadline -lhistory -ltermcap
#

# uncomment the following 4 lines for Solaris 2.x
#CC		= gcc
#CFLAGS		= -DSYSV -DREADLINE -I/usr/local/include
#LIBS		= -lsocket -L/usr/ucblib -R/usr/ucblib -lrpcsoc -lnsl \
		  -L/usr/local/lib -lreadline -lhistory -ltermlib

# uncomment the following 3 lines for AIX
#CC		= gcc
#CFLAGS		= -DAIX
#LIBS		=

# uncomment the following 3 lines for linux (tested on 2.0.33/redhat 5)
#CC		= gcc
#CFLAGS		= -DREADLINE -I/usr/local/include
#LIBS		= -L/usr/local/lib -lreadline -lhistory -lncurses

RPCGEN		= rpcgen
RGFLAGS		= -C

NFS_OBJECTS	= mount_clnt.o mount_xdr.o nfs_prot_clnt.o nfs_prot_xdr.o nfs.o
STEAL_OBJECTS	= steal.o nfs_prot_clnt.o nfs_prot_xdr.o
RPCGEN_MOUNT	= mount.h mount_clnt.c mount_svc.c mount_xdr.c
RPCGEN_NFS_PROT	= nfs_prot.h nfs_prot_clnt.c nfs_prot_svc.c nfs_prot_xdr.c

nfsshell:	$(NFS_OBJECTS)
	$(CC) -g -o nfsshell $(NFS_OBJECTS) $(LIBS)

steal:	$(STEAL_OBJECTS)
	$(CC) -g -o steal $(STEAL_OBJECTS) $(LIBS)

lint-nfs:
	lint nfs.c mount_clnt.c mount_xdr.c

lint-steal:
	lint steal.c nfs_prot_clnt.c nfs_prot_xdr.c

tar: clean
	(cd ..; tar cvf nfsshell.tar nfs; gzip -9f nfsshell.tar; \
	 uuencode nfsshell.tar.gz < nfsshell.tar.gz > nfsshell.tar.gz.uue)

clean:
	rm -f nfsshell steal $(NFS_OBJECTS) $(STEAL_OBJECTS)

clobber: clean
	rm -f $(RPCGEN_MOUNT) $(RPCGEN_NFS_PROT)

$(RPCGEN_MOUNT):; $(RPCGEN) $(RGFLAGS) mount.x
$(RPCGEN_NFS_PROT):; $(RPCGEN) $(RGFLAGS) nfs_prot.x

