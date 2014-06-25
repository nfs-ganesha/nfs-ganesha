/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs_ip_name.c
 * @brief   The management of the IP/name cache.
 */

#include "config.h"
#include "hashtable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Hashtable used to cache the hostname, accessed by their IP addess */
hash_table_t *ht_ip_name;
unsigned int expiration_time;

/**
 * @name Compute the hash value for the entry in IP/name cache
 *
 * @param[in] hparam    Hash table parameter.
 * @param[in] buffcleff The hash key buffer
 *
 * @return the computed hash value.
 *
 * @see hashtable_init
 *
 */
uint32_t ip_name_value_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *buffclef)
{
	return hash_sockaddr(buffclef->addr, true) % hparam->index_size;
}

/**
 * @brief Compute the rbt value for the entry in IP/name cache
 *
 * @param[in] hparam   Hash table parameter
 * @param[in] buffclef Hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see hashtable_init
 *
 */
uint64_t ip_name_rbt_hash_func(hash_parameter_t *hparam,
			       struct gsh_buffdesc *buffclef)
{
	return hash_sockaddr(buffclef->addr, true);
}

/**
 *
 * compare_ip_name: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used
 * as 'compare_key' field in the hashtable storing the nfs duplicated requests.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_ip_name(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	return (cmp_sockaddr(buff1->addr, buff2->addr, true) !=
		0) ? 0 : 1;
}

/**
 * @brief Display the ip_name stored in the buffer
 *
 * @param[in]  pbuff Buffer to display
 * @param[out] str   Output string
 *
 * @return number of character written.
 */
int display_ip_name_key(struct gsh_buffdesc *pbuff, char *str)
{
	sockaddr_t *addr = (sockaddr_t *) (pbuff->addr);

	sprint_sockaddr(addr, str, HASHTABLE_DISPLAY_STRLEN);
	return strlen(str);
}

/**
 * @brief Display the ip_name stored in the buffer
 *
 * @param[in]  pbuff Buffer to display
 * @param[out] str   Output string
 *
 * @return number of character written
 */
int display_ip_name_val(struct gsh_buffdesc *pbuff, char *str)
{
	nfs_ip_name_t *nfs_ip_name = (pbuff->addr);

	return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "%s",
			nfs_ip_name->hostname);
}

/**
 *
 * nfs_ip_name_add: adds an entry in the duplicate requests cache.
 *
 * Adds an entry in the duplicate requests cache.
 *
 * @param ipaddr           [IN]    the ipaddr to be used as key
 * @param hostname         [IN]    the hostname added (found by using gethostbyaddr)
 *
 * @return IP_NAME_SUCCESS if successfull\n.
 * @return IP_NAME_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return IP_NAME_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */

int nfs_ip_name_add(sockaddr_t *ipaddr, char *hostname, size_t size)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffdata;
	nfs_ip_name_t *nfs_ip_name = NULL;
	sockaddr_t *pipaddr = NULL;
	struct timeval tv0, tv1, dur;
	int rc;
	char ipstring[SOCK_NAME_MAX + 1];

	nfs_ip_name = gsh_malloc(sizeof(nfs_ip_name_t));

	if (nfs_ip_name == NULL)
		return IP_NAME_INSERT_MALLOC_ERROR;

	pipaddr = gsh_malloc(sizeof(sockaddr_t));
	if (pipaddr == NULL) {
		gsh_free(nfs_ip_name);
		return IP_NAME_INSERT_MALLOC_ERROR;
	}

	/* I have to keep an integer as key, I wil use the pointer buffkey->addr
	 * for this, this also means that buffkey->len will be 0
	 */
	memcpy(pipaddr, ipaddr, sizeof(sockaddr_t));

	buffkey.addr = (caddr_t) pipaddr;
	buffkey.len = sizeof(sockaddr_t);

	gettimeofday(&tv0, NULL);
	rc = getnameinfo((struct sockaddr *)pipaddr, sizeof(sockaddr_t),
			 nfs_ip_name->hostname, sizeof(nfs_ip_name->hostname),
			 NULL, 0, 0);
	gettimeofday(&tv1, NULL);
	timersub(&tv1, &tv0, &dur);

	sprint_sockaddr(pipaddr, ipstring, sizeof(ipstring));

	/* display warning if DNS resolution took more that 1.0s */
	if (dur.tv_sec >= 1) {
		LogEvent(COMPONENT_DISPATCH,
			 "Warning: long DNS query for %s: %u.%06u sec",
			 ipstring, (unsigned int)dur.tv_sec,
			 (unsigned int)dur.tv_usec);
	}

	/* Ask for the name to be cached */
	if (rc != 0) {
		LogEvent(COMPONENT_DISPATCH,
			 "Cannot resolve address %s, error %s", ipstring,
			 gai_strerror(rc));

		gsh_free(nfs_ip_name);
		gsh_free(pipaddr);
		return IP_NAME_NETDB_ERROR;
	}

	LogDebug(COMPONENT_DISPATCH, "Inserting %s->%s to addr cache", ipstring,
		 nfs_ip_name->hostname);

	/* I build the data with the request pointer
	 * that should be in state 'IN USE'
	 */
	nfs_ip_name->timestamp = time(NULL);

	buffdata.addr = (caddr_t) nfs_ip_name;
	buffdata.len = sizeof(nfs_ip_name_t);

	if (HashTable_Set(ht_ip_name, &buffkey, &buffdata) != HASHTABLE_SUCCESS)
		return IP_NAME_INSERT_MALLOC_ERROR;

	/* Copy the value for the caller */
	strmaxcpy(hostname, nfs_ip_name->hostname, size);

	return IP_NAME_SUCCESS;
}				/* nfs_ip_name_add */

/**
 *
 * nfs_ip_name_get: Tries to get an entry for ip_name cache.
 *
 * Tries to get an entry for ip_name cache.
 *
 * @param ipaddr   [IN]  the ip address requested
 * @param hostname [OUT] the hostname
 *
 * @return the result previously set if *pstatus == IP_NAME_SUCCESS
 *
 */
int nfs_ip_name_get(sockaddr_t *ipaddr, char *hostname, size_t size)
{
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	nfs_ip_name_t *nfs_ip_name;
	char ipstring[SOCK_NAME_MAX + 1];

	sprint_sockaddr(ipaddr, ipstring, sizeof(ipstring));

	buffkey.addr = (caddr_t) ipaddr;
	buffkey.len = sizeof(sockaddr_t);

	if (HashTable_Get(ht_ip_name,
			  &buffkey,
			  &buffval) == HASHTABLE_SUCCESS) {
		nfs_ip_name = buffval.addr;
		strmaxcpy(hostname, nfs_ip_name->hostname, size);

		LogFullDebug(COMPONENT_DISPATCH, "Cache get hit for %s->%s",
			     ipstring, nfs_ip_name->hostname);

		return IP_NAME_SUCCESS;
	}

	LogFullDebug(COMPONENT_DISPATCH, "Cache get miss for %s", ipstring);

	return IP_NAME_NOT_FOUND;
}				/* nfs_ip_name_get */

/**
 *
 * nfs_ip_name_remove: Tries to remove an entry for ip_name cache
 *
 * Tries to remove an entry for ip_name cache.
 *
 * @param ipaddr           [IN]    the ip address to be uncached.
 *
 * @return the result previously set if *pstatus == IP_NAME_SUCCESS
 *
 */
int nfs_ip_name_remove(sockaddr_t *ipaddr)
{
	struct gsh_buffdesc buffkey, old_value;
	nfs_ip_name_t *nfs_ip_name = NULL;
	char ipstring[SOCK_NAME_MAX + 1];

	sprint_sockaddr(ipaddr, ipstring, sizeof(ipstring));

	buffkey.addr = (caddr_t) ipaddr;
	buffkey.len = sizeof(sockaddr_t);

	if (HashTable_Del(ht_ip_name, &buffkey, NULL, &old_value) ==
	    HASHTABLE_SUCCESS) {
		nfs_ip_name = (nfs_ip_name_t *) old_value.addr;

		LogFullDebug(COMPONENT_DISPATCH, "Cache remove hit for %s->%s",
			     ipstring, nfs_ip_name->hostname);

		gsh_free(nfs_ip_name);
		return IP_NAME_SUCCESS;
	}

	LogFullDebug(COMPONENT_DISPATCH, "Cache remove miss for %s", ipstring);

	return IP_NAME_NOT_FOUND;
}				/* nfs_ip_name_remove */

/**
 * @defgroup config_ipnamemap Structure and defaults for NFS_IP_Name
 *
 * @{
 */

/**
 * @brief Default index size for IP-Name hash
 */
#define PRIME_IP_NAME 17

/**
 * @brief Default value for ip_name_param.expiration-time
 */
#define IP_NAME_EXPIRATION 3600


/** @} */

/**
 * @brief NFS_IP_Name configuration stanza
 */

struct ip_name_cache {
	/** Configuration for hash table for NFS Name/IP map.
	    Defautl index size is PRIME_IP_NAME, settable with
	    Index_Size. */
	hash_parameter_t hash_param;
	/** Expiration time for ip-name mappings.  Defautls to
	    IP_NAME_Expiration, and settable with Expiration_Time. */
	uint32_t expiration_time;
};

static struct ip_name_cache ip_name_cache = {
	.hash_param.hash_func_key = ip_name_value_hash_func,
	.hash_param.hash_func_rbt = ip_name_rbt_hash_func,
	.hash_param.compare_key = compare_ip_name,
	.hash_param.key_to_str = display_ip_name_key,
	.hash_param.val_to_str = display_ip_name_val,
	.hash_param.flags = HT_FLAG_NONE,
};

/**
 * @brief IP name cache parameters
 */

static struct config_item ip_name_params[] = {
	CONF_ITEM_UI32("Index_Size", 1, 51, PRIME_IP_NAME,
		       ip_name_cache, hash_param.index_size),
	CONF_ITEM_UI32("Expiration_Time", 1, 60*60*24, IP_NAME_EXPIRATION,
		       ip_name_cache, expiration_time),
	CONFIG_EOL
};

static void *ip_name_init(void *link_mem, void *self_struct)
{
	if (self_struct == NULL)
		return &ip_name_cache;
	else
		return NULL;
}

static int ip_name_commit(void *node, void *link_mem, void *self_struct,
			  struct config_error_type *err_type)
{
	struct ip_name_cache *params = self_struct;

	if (!is_prime(params->hash_param.index_size)) {
		LogCrit(COMPONENT_CONFIG,
			"IP name cache index size must be a prime.");
		return 1;
	}
	return 0;
}

struct config_block nfs_ip_name = {
	.dbus_interface_name = "org.ganesha.nfsd.config.ip_name",
	.blk_desc.name = "NFS_IP_Name",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = ip_name_init,
	.blk_desc.u.blk.params = ip_name_params,
	.blk_desc.u.blk.commit = ip_name_commit
};

/**
 *
 * nfs_Init_ip_name: Init the hashtable for IP/name cache.
 *
 * Perform all the required initialization for hashtable IP/name cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_ip_name(void)
{
	ht_ip_name = hashtable_init(&ip_name_cache.hash_param);

	if (ht_ip_name == NULL) {
		LogCrit(COMPONENT_INIT,
			"NFS IP_NAME: Cannot init IP/name cache");
		return -1;
	}

	/* Set the expiration time */
	expiration_time = ip_name_cache.expiration_time;

	return IP_NAME_SUCCESS;
}				/* nfs_Init_ip_name */
