#ifndef _NFS_IP_STATS_H
#define _NFS_IP_STATS_H

#include <sys/types.h>
#include <sys/param.h>

#include "gsh_rpc.h"
#include <netdb.h>		/* for having MAXHOSTNAMELEN */
#include "hashtable.h"

/* IP/name cache error */
#define IP_NAME_SUCCESS             0
#define IP_NAME_INSERT_MALLOC_ERROR 1
#define IP_NAME_NOT_FOUND           2

#define IP_NAME_PREALLOC_SIZE      200

/* NFS IPaddr cache entry structure */
typedef struct nfs_ip_name__ {
	time_t timestamp;
	char hostname[];
} nfs_ip_name_t;

int nfs_ip_name_get(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_add(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_remove(sockaddr_t *ipaddr);

#endif
