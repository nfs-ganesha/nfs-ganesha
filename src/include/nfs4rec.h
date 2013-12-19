#ifndef _NFSRECV_H
#define _NFSRECV_H
#define NFS_V4_RECOV_ROOT "/var/lib/nfs/ganesha"
#define NFS_V4_RECOV_LOCAL "/var/lib/nfs/ganesha_local"
#define NFS_V4_RECOV_DIR "v4recov"
#define NFS_V4_OLD_DIR "v4old"
#define NFS_RECOV_EVENTS "/var/lib/nfs/ganesha/recevents" /* Not just v4 */
#define NFS_RECOV_CYCLE 1
#define NFS_RECOV_GC ( 6 * NFS_RECOV_CYCLE )
#define NFS_RECOV_STATE_CNT 10
#define ALL_NODES 0xFFFF
#define IPADDR_STRSZ	32

/* Recovery event definitions */
#define TAKEIP 1
#define RELEASEIP 2
#define NOTSPECIFIED	10

typedef struct nfs_grace_start
{
  int      event;
  int      nodeid;
  char     ipaddr[IPADDR_STRSZ];
} nfs_grace_start_t;

typedef struct nfs_grace_start_array 
{
  int     num_elements;
  nfs_grace_start_t *nfs_grace_start;
} nfs_grace_start_array_t;


#endif /* _NFSRECV_H */
