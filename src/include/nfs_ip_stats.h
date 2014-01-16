#ifndef _NFS_IP_STATS_H
#define _NFS_IP_STATS_H

#include <sys/types.h>
#include <sys/param.h>

#include "ganesha_rpc.h"
#include <dirent.h>		/* for having MAXNAMLEN */
#include <netdb.h>		/* for having MAXHOSTNAMELEN */
#include "hashtable.h"

/* IP/name cache error */
#define IP_NAME_SUCCESS             0
#define IP_NAME_INSERT_MALLOC_ERROR 1
#define IP_NAME_NOT_FOUND           2
#define IP_NAME_NETDB_ERROR         3

#define IP_NAME_PREALLOC_SIZE      200

/* NFS IPaddr cache entry structure */
typedef struct nfs_ip_name__ {
	time_t timestamp;
	char hostname[MAXHOSTNAMELEN + 1];
} nfs_ip_name_t;

int nfs_ip_name_get(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_add(sockaddr_t *ipaddr, char *hostname, size_t size);
int nfs_ip_name_remove(sockaddr_t *ipaddr);

int display_ip_name_key(struct gsh_buffdesc *pbuff, char *str);
int display_ip_name_val(struct gsh_buffdesc *pbuff, char *str);
int compare_ip_name(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2);
uint64_t ip_name_rbt_hash_func(hash_parameter_t *p_hparam,
			       struct gsh_buffdesc *buffclef);
uint32_t ip_name_value_hash_func(hash_parameter_t *p_hparam,
				 struct gsh_buffdesc *buffclef);

#endif
