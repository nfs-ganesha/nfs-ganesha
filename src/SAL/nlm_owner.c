/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @defgroup SAL State abstraction layer
 * @{
 */

 /**
  * @file nlm_owner.c
  * @brief The management of the NLM owner cache.
  */

#include "config.h"
#include <string.h>
#include <ctype.h>
#include "sal_functions.h"
#include "nsm.h"

/**
 * @brief NSM clients
 */
hash_table_t *ht_nsm_client;

/**
 * @brief NLM Clients
 */
hash_table_t *ht_nlm_client;

/**
 * @brief NLM owners
 */
hash_table_t *ht_nlm_owner;

/*******************************************************************************
 *
 * NSM Client Routines
 *
 ******************************************************************************/

/**
 * @brief Display an NSM client
 *
 * @param[in]  key The NSM client
 * @param[out] str Output buffer
 *
 * @return Length of output string.
 */
int display_nsm_client(state_nsm_client_t *key, char *str)
{
	char *strtmp = str;

	if (key == NULL)
		return sprintf(str, "NSM Client <NULL>");

	strtmp += sprintf(strtmp, "NSM Client %p: ", key);

	if (nfs_param.core_param.nsm_use_caller_name)
		strtmp += sprintf(strtmp, "caller_name=");
	else
		strtmp += sprintf(strtmp, "addr=");

	memcpy(strtmp, key->ssc_nlm_caller_name, key->ssc_nlm_caller_name_len);
	strtmp += key->ssc_nlm_caller_name_len;

	strtmp +=
	    sprintf(strtmp, " %s refcount=%d",
		    atomic_fetch_int32_t(&key->
					 ssc_monitored) ? "monitored" :
		    "unmonitored", atomic_fetch_int32_t(&key->ssc_refcount));

	return strtmp - str;
}

/**
 * @brief Display an NSM client in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nsm_client_key(struct gsh_buffdesc *buff, char *str)
{
	return display_nsm_client(buff->addr, str);
}

/**
 * @brief Display an NSM client in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nsm_client_val(struct gsh_buffdesc *buff, char *str)
{
	return display_nsm_client(buff->addr, str);
}

/**
 * @brief Compare NSM clients
 *
 * @param[in] client1 A client
 * @param[in] client2 Another client
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nsm_client(state_nsm_client_t *client1,
		       state_nsm_client_t *client2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_nsm_client(client1, str1);
		display_nsm_client(client2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (client1 == NULL || client2 == NULL)
		return 1;

	if (client1 == client2)
		return 0;

	if (!nfs_param.core_param.nsm_use_caller_name) {
		if (cmp_sockaddr(&client1->ssc_client_addr,
				 &client2->ssc_client_addr,
				 true) == 0)
			return 1;
		return 0;
	}

	if (client1->ssc_nlm_caller_name_len !=
	    client2->ssc_nlm_caller_name_len)
		return 1;

	return memcmp(client1->ssc_nlm_caller_name,
		      client2->ssc_nlm_caller_name,
		      client1->ssc_nlm_caller_name_len);
}

/**
 * @brief Compare NSM clients in the hash table
 *
 * @param[in] buff1 A key
 * @param[in] buff2 Another key
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nsm_client_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2)
{
	return compare_nsm_client(buff1->addr, buff2->addr);

}

/**
 * @brief Calculate hash index for an NSM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t nsm_client_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key)
{
	unsigned long res;
	state_nsm_client_t *pkey = key->addr;

	if (nfs_param.core_param.nsm_use_caller_name) {
		unsigned int sum = 0;
		unsigned int i;

		/* Compute the sum of all the characters */
		for (i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
			sum += (unsigned char)pkey->ssc_nlm_caller_name[i];

		res =
		    (unsigned long)sum +
		    (unsigned long)pkey->ssc_nlm_caller_name_len;
	} else {
		res =
		    hash_sockaddr(&pkey->ssc_client_addr,
				  true) % hparam->index_size;
	}

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (unsigned long)(res % hparam->index_size);
}

/**
 * @brief Calculate RBT hash for an NSM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t nsm_client_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	unsigned long res;
	state_nsm_client_t *pkey = key->addr;

	if (nfs_param.core_param.nsm_use_caller_name) {
		unsigned int sum = 0;
		unsigned int i;

		/* Compute the sum of all the characters */
		for (i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
			sum += (unsigned char)pkey->ssc_nlm_caller_name[i];

		res =
		    (unsigned long)sum +
		    (unsigned long)pkey->ssc_nlm_caller_name_len;
	} else {
		res = hash_sockaddr(&pkey->ssc_client_addr, true);
	}

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

	return res;
}				/* nsm_client_rbt_hash_func */

/*******************************************************************************
 *
 * NLM Client Routines
 *
 ******************************************************************************/

/**
 * @brief Display an NLM client
 *
 * @param[in]  key The NLM client
 * @param[out] str Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_client(state_nlm_client_t *key, char *str)
{
	char *strtmp = str;

	if (key == NULL)
		return sprintf(str, "NLM Client <NULL>");

	strtmp += sprintf(strtmp, "NLM Client %p: {", key);
	strtmp += display_nsm_client(key->slc_nsm_client, strtmp);
	strtmp += sprintf(strtmp, "} caller_name=");
	memcpy(strtmp, key->slc_nlm_caller_name, key->slc_nlm_caller_name_len);
	strtmp += key->slc_nlm_caller_name_len;
	strtmp +=
	    sprintf(strtmp, " type=%s", xprt_type_to_str(key->slc_client_type));
	strtmp +=
	    sprintf(strtmp, " refcount=%d",
		    atomic_fetch_int32_t(&key->slc_refcount));

	return strtmp - str;
}

/**
 * @brief Display an NLM client in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_client_key(struct gsh_buffdesc *buff, char *str)
{
	return display_nlm_client(buff->addr, str);
}

/**
 * @brief Display an NLM client in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_client_val(struct gsh_buffdesc *buff, char *str)
{
	return display_nlm_client(buff->addr, str);
}

/**
 * @brief Compare NLM clients
 *
 * @param[in] client1 A client
 * @param[in] client2 Another client
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_client(state_nlm_client_t *client1,
		       state_nlm_client_t *client2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_nlm_client(client1, str1);
		display_nlm_client(client2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (client1 == NULL || client2 == NULL)
		return 1;

	if (client1 == client2)
		return 0;

	if (compare_nsm_client(client1->slc_nsm_client, client2->slc_nsm_client)
	    != 0)
		return 1;

	if (cmp_sockaddr
	    (&client1->slc_server_addr, &client2->slc_server_addr,
	     true) == 0)
		return 1;

	if (client1->slc_client_type != client2->slc_client_type)
		return 1;

	if (client1->slc_nlm_caller_name_len !=
	    client2->slc_nlm_caller_name_len)
		return 1;

	return memcmp(client1->slc_nlm_caller_name,
		      client2->slc_nlm_caller_name,
		      client1->slc_nlm_caller_name_len);
}

/**
 * @brief Compare NLM clients in the hash table
 *
 * @param[in] buff1 A key
 * @param[in] buff2 Another key
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_client_key(struct gsh_buffdesc *buff1,
			   struct gsh_buffdesc *buff2)
{
	return compare_nlm_client(buff1->addr, buff2->addr);

}

/**
 * @brief Calculate hash index for an NLM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t nlm_client_value_hash_func(hash_parameter_t *hparam,
				    struct gsh_buffdesc *key)
{
	uint32_t sum = 0;
	unsigned int i;
	unsigned long res;
	state_nlm_client_t *pkey = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->slc_nlm_caller_name_len; i++)
		sum += (unsigned char)pkey->slc_nlm_caller_name[i];

	res = (unsigned long)sum + (unsigned long)pkey->slc_nlm_caller_name_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (unsigned long)(res % hparam->index_size);
}

/**
 * @brief Calculate RBT hash for an NLM key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t nlm_client_rbt_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	state_nlm_client_t *pkey = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->slc_nlm_caller_name_len; i++)
		sum += (unsigned char)pkey->slc_nlm_caller_name[i];

	res = (unsigned long)sum + (unsigned long)pkey->slc_nlm_caller_name_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

	return res;
}				/* nlm_client_rbt_hash_func */

/*******************************************************************************
 *
 * NLM Owner Routines
 *
 ******************************************************************************/

/**
 * @brief Display an NLM cowner
 *
 * @param[in]  key The NLM owner
 * @param[out] str Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_owner(state_owner_t *key, char *str)
{
	char *strtmp = str;

	if (key == NULL)
		return sprintf(str, "STATE_LOCK_OWNER_NLM <NULL>");

	strtmp += sprintf(strtmp, "STATE_LOCK_OWNER_NLM %p: {", key);

	strtmp +=
	    display_nlm_client(key->so_owner.so_nlm_owner.so_client, strtmp);

	strtmp += sprintf(strtmp, "} oh=");

	strtmp +=
	    DisplayOpaqueValue(key->so_owner_val, key->so_owner_len, strtmp);

	strtmp +=
	    sprintf(strtmp, " svid=%d", key->so_owner.so_nlm_owner.so_nlm_svid);
	strtmp +=
	    sprintf(strtmp, " refcount=%d",
		    atomic_fetch_int32_t(&key->so_refcount));

	return strtmp - str;
}

/**
 * @brief Display an NLM owner in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_owner_key(struct gsh_buffdesc *buff, char *str)
{
	return display_nlm_owner(buff->addr, str);
}

/**
 * @brief Display an NLM owner in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_nlm_owner_val(struct gsh_buffdesc *buff, char *str)
{
	return display_nlm_owner(buff->addr, str);
}

/**
 * @brief Compare NLM owners
 *
 * @param[in] owner1 A client
 * @param[in] owner2 Another client
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_owner(state_owner_t *owner1, state_owner_t *owner2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_nlm_owner(owner1, str1);
		display_nlm_owner(owner2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (owner1 == NULL || owner2 == NULL)
		return 1;

	if (owner1 == owner2)
		return 0;

	if (compare_nlm_client
	    (owner1->so_owner.so_nlm_owner.so_client,
	     owner2->so_owner.so_nlm_owner.so_client) != 0)
		return 1;

	if (owner1->so_owner.so_nlm_owner.so_nlm_svid !=
	    owner2->so_owner.so_nlm_owner.so_nlm_svid)
		return 1;

	if (owner1->so_owner_len != owner2->so_owner_len)
		return 1;

	return memcmp(owner1->so_owner_val, owner2->so_owner_val,
		      owner1->so_owner_len);
}

/**
 * @brief Compare NLM owners in the hash table
 *
 * @param[in] buff1 A key
 * @param[in] buff2 Another key
 *
 * @retval 0 on equality.
 * @retval 1 on inequality.
 */
int compare_nlm_owner_key(struct gsh_buffdesc *buff1,
			  struct gsh_buffdesc *buff2)
{
	return compare_nlm_owner(buff1->addr, buff2->addr);

}

/**
 * @brief Calculate hash index for an NLM owner key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t nlm_owner_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	state_owner_t *pkey = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++)
		sum += (unsigned char)pkey->so_owner_val[i];

	res =
	    (unsigned long)(pkey->so_owner.so_nlm_owner.so_nlm_svid) +
	    (unsigned long)sum + (unsigned long)pkey->so_owner_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (unsigned long)(res % hparam->index_size);

}

/**
 * @brief Calculate RBT hash for an NLM owner key
 *
 * @todo Replace with a good hash function.
 *
 * @param[in]  hparam Hash params
 * @param[out] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t nlm_owner_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	state_owner_t *pkey = key->addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++)
		sum += (unsigned char)pkey->so_owner_val[i];

	res =
	    (unsigned long)(pkey->so_owner.so_nlm_owner.so_nlm_svid) +
	    (unsigned long)sum + (unsigned long)pkey->so_owner_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

	return res;
}				/* state_id_rbt_hash_func */

static hash_parameter_t nsm_client_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nsm_client_value_hash_func,
	.hash_func_rbt = nsm_client_rbt_hash_func,
	.compare_key = compare_nsm_client_key,
	.key_to_str = display_nsm_client_key,
	.val_to_str = display_nsm_client_val,
	.flags = HT_FLAG_NONE,
};

static hash_parameter_t nlm_client_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nlm_client_value_hash_func,
	.hash_func_rbt = nlm_client_rbt_hash_func,
	.compare_key = compare_nlm_client_key,
	.key_to_str = display_nlm_client_key,
	.val_to_str = display_nlm_client_val,
	.flags = HT_FLAG_NONE,
};

static hash_parameter_t nlm_owner_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nlm_owner_value_hash_func,
	.hash_func_rbt = nlm_owner_rbt_hash_func,
	.compare_key = compare_nlm_owner_key,
	.key_to_str = display_nlm_owner_key,
	.val_to_str = display_nlm_owner_val,
	.flags = HT_FLAG_NONE,
};

/**
 * @brief Init the hashtables for NLM support
 *
 * @return 0 if successful, -1 otherwise
 */
int Init_nlm_hash(void)
{
	ht_nsm_client = hashtable_init(&nsm_client_hash_param);

	if (ht_nsm_client == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NSM Client cache");
		return -1;
	}

	ht_nlm_client = hashtable_init(&nlm_client_hash_param);

	if (ht_nlm_client == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NLM Client cache");
		return -1;
	}

	ht_nlm_owner = hashtable_init(&nlm_owner_hash_param);

	if (ht_nlm_owner == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init NLM Owner cache");
		return -1;
	}

	return 0;
}				/* Init_nlm_hash */

/*******************************************************************************
 *
 * NSM Client Routines
 *
 ******************************************************************************/

/**
 * @brief Take a reference on NSM client
 *
 * @param[in] client The client to ref
 */
void inc_nsm_client_ref(state_nsm_client_t *client)
{
	atomic_inc_int32_t(&client->ssc_refcount);
}

/**
 * @brief Free an NSM client
 *
 * @param[in] client Client to free
 */
void free_nsm_client(state_nsm_client_t *client)
{
	if (client->ssc_nlm_caller_name != NULL)
		gsh_free(client->ssc_nlm_caller_name);

	pthread_mutex_destroy(&client->ssc_mutex);

	gsh_free(client);
}

/**
 * @brief Relinquish a reference on an NSM client
 *
 * @param[in] client The client to release
 */
void dec_nsm_client_ref(state_nsm_client_t *client)
{
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;
	int32_t refcount;

	if (isDebug(COMPONENT_STATE))
		display_nsm_client(client, str);

	refcount = atomic_dec_int32_t(&client->ssc_refcount);

	if (refcount > 0) {
		LogFullDebug(COMPONENT_STATE,
			     "Decrement refcount now=%" PRId32 " {%s}",
			     refcount, str);

		return;
	}

	LogFullDebug(COMPONENT_STATE, "Try to remove {%s}", str);

	buffkey.addr = client;
	buffkey.len = sizeof(*client);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(ht_nsm_client, &buffkey, &old_value, true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_nsm_client, &latch);

		display_nsm_client(client, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	refcount = atomic_fetch_int32_t(&client->ssc_refcount);

	if (refcount > 0) {
		LogDebug(COMPONENT_STATE,
			 "Did not release refcount now=%" PRId32 " {%s}",
			 refcount, str);

		hashtable_releaselatched(ht_nsm_client, &latch);

		return;
	}

	/* use the key to delete the entry */
	rc = hashtable_deletelatched(ht_nsm_client, &buffkey, &latch, &old_key,
				     &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_nsm_client, &latch);

		display_nsm_client(client, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not remove {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	nsm_unmonitor(old_value.addr);
	free_nsm_client(old_value.addr);
}

/**
 * @brief Get an NSM client
 *
 * @param[in] care        Care status
 * @param[in] xprt        RPC transport
 * @param[in] caller_name Caller name
 *
 * @return NSM client or NULL.
 */
state_nsm_client_t *get_nsm_client(care_t care, SVCXPRT *xprt,
				   char *caller_name)
{
	state_nsm_client_t key;
	state_nsm_client_t *pclient;
	char sock_name[SOCK_NAME_MAX + 1];
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;

	if (caller_name == NULL)
		return NULL;

	memset(&key, 0, sizeof(key));

	if (nfs_param.core_param.nsm_use_caller_name) {
		key.ssc_nlm_caller_name_len = strlen(caller_name);

		if (key.ssc_nlm_caller_name_len > LM_MAXSTRLEN)
			return NULL;

		key.ssc_nlm_caller_name = caller_name;
	} else if (xprt == NULL) {
		int rc =
		    ipstring_to_sockaddr(caller_name, &key.ssc_client_addr);
		if (rc != 0) {
			LogEvent(COMPONENT_STATE,
				 "Error %s, converting caller_name %s to an ipaddress",
				 gai_strerror(rc), caller_name);

			return NULL;
		}

		key.ssc_nlm_caller_name_len = strlen(caller_name);

		if (key.ssc_nlm_caller_name_len > LM_MAXSTRLEN)
			return NULL;

		key.ssc_nlm_caller_name = caller_name;
	} else {
		key.ssc_nlm_caller_name = sock_name;

		if (copy_xprt_addr(&key.ssc_client_addr, xprt) == 0) {
			LogCrit(COMPONENT_STATE,
				"Error converting caller_name %s to an ipaddress",
				caller_name);

			return NULL;
		}

		if (sprint_sockip
		    (&key.ssc_client_addr, key.ssc_nlm_caller_name,
		     sizeof(sock_name)) == 0) {
			LogCrit(COMPONENT_STATE,
				"Error converting caller_name %s to an ipaddress",
				caller_name);

			return NULL;
		}

		key.ssc_nlm_caller_name_len = strlen(key.ssc_nlm_caller_name);
	}

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(&key, str);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nsm_client, &buffkey, &buffval, true,
				&latch);

	/* If we found it, return it */
	if (rc == HASHTABLE_SUCCESS) {
		pclient = buffval.addr;

		/* Return the found NSM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nsm_client(pclient, str);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		/* Increment refcount under hash latch.
		 * This prevents dec ref from removing this entry from hash
		 * if a race occurs.
		 */
		inc_nsm_client_ref(pclient);

		hashtable_releaselatched(ht_nsm_client, &latch);

		if (care == CARE_MONITOR && !nsm_monitor(pclient)) {
			dec_nsm_client_ref(pclient);
			pclient = NULL;
		}

		return pclient;
	}

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		display_nsm_client(&key, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return NULL;
	}

	/* Not found, but we don't care, return NULL */
	if (care == CARE_NOT) {
		/* Return the found NSM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nsm_client(&key, str);
			LogFullDebug(COMPONENT_STATE, "Ignoring {%s}", str);
		}

		hashtable_releaselatched(ht_nsm_client, &latch);

		return NULL;
	}

	pclient = gsh_malloc(sizeof(*pclient));

	if (pclient == NULL) {
		display_nsm_client(&key, str);
		LogCrit(COMPONENT_STATE, "No memory for {%s}", str);

		return NULL;
	}

	/* Copy everything over */
	memcpy(pclient, &key, sizeof(key));

	if (pthread_mutex_init(&pclient->ssc_mutex, NULL) == -1) {
		/* Mutex initialization failed, free the created client */
		display_nsm_client(&key, str);
		LogCrit(COMPONENT_STATE, "Could not init mutex for {%s}", str);

		gsh_free(pclient);
		return NULL;
	}

	pclient->ssc_nlm_caller_name = gsh_strdup(key.ssc_nlm_caller_name);

	if (pclient->ssc_nlm_caller_name == NULL) {
		/* Discard the created client */
		free_nsm_client(pclient);
		return NULL;
	}

	glist_init(&pclient->ssc_lock_list);
	glist_init(&pclient->ssc_share_list);
	pclient->ssc_refcount = 1;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(pclient, str);
		LogFullDebug(COMPONENT_STATE, "New {%s}", str);
	}

	buffkey.addr = pclient;
	buffkey.len = sizeof(*pclient);
	buffval.addr = pclient;
	buffval.len = sizeof(*pclient);

	rc = hashtable_setlatched(ht_nsm_client, &buffval, &buffval, &latch,
				  false, NULL, NULL);

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_SUCCESS) {
		display_nsm_client(pclient, str);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		free_nsm_client(pclient);

		return NULL;
	}

	if (care != CARE_MONITOR || nsm_monitor(pclient))
		return pclient;

	/* Failed to monitor, release client reference
	 * and almost certainly remove it from the hash table.
	 */
	dec_nsm_client_ref(pclient);

	return NULL;
}

/*******************************************************************************
 *
 * NLM Client Routines
 *
 ******************************************************************************/

/**
 * @brief Free an NLM client
 *
 * @param[in] client The client to free
 */
void free_nlm_client(state_nlm_client_t *client)
{
	if (client->slc_nsm_client != NULL)
		dec_nsm_client_ref(client->slc_nsm_client);

	if (client->slc_nlm_caller_name != NULL)
		gsh_free(client->slc_nlm_caller_name);

	gsh_free(client);
}

/**
 * @brief Take a reference on an NLM client
 *
 * @param[in] client Client to reference
 */
void inc_nlm_client_ref(state_nlm_client_t *client)
{
	atomic_inc_int32_t(&client->slc_refcount);
}

/**
 * @brief Relinquish a reference on an NLM client
 *
 * @param[in] client Client to release
 */
void dec_nlm_client_ref(state_nlm_client_t *client)
{
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;
	int32_t refcount;

	if (isDebug(COMPONENT_STATE))
		display_nlm_client(client, str);

	refcount = atomic_dec_int32_t(&client->slc_refcount);

	if (refcount > 0) {
		LogFullDebug(COMPONENT_STATE,
			     "Decrement refcount now=%" PRId32 " {%s}",
			     refcount, str);

		return;
	}

	LogFullDebug(COMPONENT_STATE, "Try to remove {%s}", str);

	buffkey.addr = client;
	buffkey.len = sizeof(*client);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(ht_nlm_client, &buffkey, &old_value, true,
				&latch);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_nlm_client, &latch);

		display_nlm_client(client, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	refcount = atomic_fetch_int32_t(&client->slc_refcount);

	if (refcount > 0) {
		LogDebug(COMPONENT_STATE,
			 "Did not release refcount now=%" PRId32 " {%s}",
			 refcount, str);

		hashtable_releaselatched(ht_nlm_client, &latch);

		return;
	}

	/* use the key to delete the entry */
	rc = hashtable_deletelatched(ht_nlm_client, &buffkey, &latch, &old_key,
				     &old_value);

	if (rc != HASHTABLE_SUCCESS) {
		if (rc == HASHTABLE_ERROR_NO_SUCH_KEY)
			hashtable_releaselatched(ht_nlm_client, &latch);

		display_nlm_client(client, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not remove {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	free_nlm_client(old_value.addr);
}

/**
 * @brief Get an NLM client
 *
 * @param[in] care        Care status
 * @param[in] xprt        RPC transport
 * @param[in] nsm_client  Related NSM client
 * @param[in] caller_name Caller name
 *
 * @return NLM client or NULL.
 */
state_nlm_client_t *get_nlm_client(care_t care, SVCXPRT *xprt,
				   state_nsm_client_t *nsm_client,
				   char *caller_name)
{
	state_nlm_client_t key;
	state_nlm_client_t *pclient;
	char str[HASHTABLE_DISPLAY_STRLEN];
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	struct sockaddr_storage local_addr;
	socklen_t addr_len;

	if (caller_name == NULL)
		return NULL;

	memset(&key, 0, sizeof(key));

	key.slc_nsm_client = nsm_client;
	key.slc_nlm_caller_name_len = strlen(caller_name);
	key.slc_client_type = svc_get_xprt_type(xprt);

	addr_len = sizeof(local_addr);
	if (getsockname(xprt->xp_fd, (struct sockaddr *)&local_addr, &addr_len)
	    == -1) {
		LogEvent(COMPONENT_CLIENTID, "Failed to get local addr.");
	} else {
		memcpy(&(key.slc_server_addr), &local_addr,
		       sizeof(struct sockaddr_storage));
	}

	if (key.slc_nlm_caller_name_len > LM_MAXSTRLEN)
		return NULL;

	key.slc_nlm_caller_name = caller_name;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_client(&key, str);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nlm_client, &buffkey, &buffval, true,
				&latch);

	/* If we found it, return it */
	if (rc == HASHTABLE_SUCCESS) {
		pclient = buffval.addr;

		/* Return the found NLM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_client(pclient, str);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		/* Increment refcount under hash latch.
		 * This prevents dec ref from removing this entry from hash
		 * if a race occurs.
		 */
		inc_nlm_client_ref(pclient);

		hashtable_releaselatched(ht_nlm_client, &latch);

		if (care == CARE_MONITOR && !nsm_monitor(nsm_client)) {
			dec_nlm_client_ref(pclient);
			pclient = NULL;
		}

		return pclient;
	}

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_ERROR_NO_SUCH_KEY) {
		display_nlm_client(&key, str);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return NULL;
	}

	/* Not found, but we don't care, return NULL */
	if (care == CARE_NOT) {
		/* Return the found NLM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_client(&key, str);
			LogFullDebug(COMPONENT_STATE, "Ignoring {%s}", str);
		}

		hashtable_releaselatched(ht_nlm_client, &latch);

		return NULL;
	}

	pclient = gsh_malloc(sizeof(*pclient));

	if (pclient == NULL) {
		display_nlm_client(&key, str);
		LogCrit(COMPONENT_STATE, "No memory for {%s}", str);

		return NULL;
	}

	/* Copy everything over */
	memcpy(pclient, &key, sizeof(key));

	pclient->slc_nlm_caller_name = gsh_strdup(key.slc_nlm_caller_name);

	/* Take a reference to the NSM Client */
	inc_nsm_client_ref(nsm_client);

	if (pclient->slc_nlm_caller_name == NULL) {
		/* Discard the created client */
		free_nlm_client(pclient);
		return NULL;
	}

	pclient->slc_refcount = 1;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_client(pclient, str);
		LogFullDebug(COMPONENT_STATE, "New {%s}", str);
	}

	buffkey.addr = pclient;
	buffkey.len = sizeof(*pclient);
	buffval.addr = pclient;
	buffval.len = sizeof(*pclient);

	rc = hashtable_setlatched(ht_nlm_client, &buffval, &buffval, &latch,
				  false, NULL, NULL);

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_SUCCESS) {
		display_nlm_client(pclient, str);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		free_nlm_client(pclient);

		return NULL;
	}

	if (care != CARE_MONITOR || nsm_monitor(nsm_client))
		return pclient;

	/* Failed to monitor, release client reference
	 * and almost certainly remove it from the hash table.
	 */
	dec_nlm_client_ref(pclient);

	return NULL;
}

/*******************************************************************************
 *
 * NLM Owner Routines
 *
 ******************************************************************************/

/**
 * @brief Free an NLM owner object
 *
 * @param[in] owner Stored owner
 */
void free_nlm_owner(state_owner_t *owner)
{
	if (owner->so_owner.so_nlm_owner.so_client != NULL)
		dec_nlm_client_ref(owner->so_owner.so_nlm_owner.so_client);
}

/**
 * @brief Initialize an NLM owner object
 *
 * @param[in] owner Stored owner
 */
static void init_nlm_owner(state_owner_t *owner)
{
	inc_nlm_client_ref(owner->so_owner.so_nlm_owner.so_client);

	glist_init(&owner->so_owner.so_nlm_owner.so_nlm_shares);
}

/**
 * @brief Get an NLM owner
 *
 * @param[in] care   Care status
 * @param[in] client Related NLM client
 * @param[in] oh     Object handle
 * @param[in] svid   Owner ID
 */
state_owner_t *get_nlm_owner(care_t care, state_nlm_client_t *client,
			     netobj *oh, uint32_t svid)
{
	state_owner_t key;

	if (client == NULL || oh == NULL || oh->n_len > MAX_NETOBJ_SZ)
		return NULL;

	memset(&key, 0, sizeof(key));

	key.so_type = STATE_LOCK_OWNER_NLM;
	key.so_owner.so_nlm_owner.so_client = client;
	key.so_owner.so_nlm_owner.so_nlm_svid = svid;
	key.so_owner_len = oh->n_len;
	key.so_owner_val = oh->n_bytes;

	return get_state_owner(care, &key, init_nlm_owner, NULL);
}

/** @} */
