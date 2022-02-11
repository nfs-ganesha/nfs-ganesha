// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include <netdb.h>

#include "gsh_config.h"
#include "sal_functions.h"
#include "nsm.h"
#include "log.h"
#include "client_mgr.h"
#include "fsal.h"

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
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     key    The NSM client
 *
 * @return the bytes remaining in the buffer.
 */
int display_nsm_client(struct display_buffer *dspbuf, state_nsm_client_t *key)
{
	int b_left;

	if (key == NULL)
		return display_printf(dspbuf, "NSM Client <NULL>");

	b_left = display_printf(dspbuf, "NSM Client %p: ", key);

	if (b_left <= 0)
		return b_left;

	if (nfs_param.core_param.nsm_use_caller_name)
		b_left = display_printf(dspbuf, "caller_name=");
	else
		b_left = display_printf(dspbuf, "addr=");

	if (b_left <= 0)
		return b_left;

	b_left = display_len_cat(dspbuf,
				 key->ssc_nlm_caller_name,
				 key->ssc_nlm_caller_name_len);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, " ssc_client=%p %s refcount=%d",
			      key->ssc_client,
			      atomic_fetch_int32_t(&key->ssc_monitored)
					? "monitored" : "unmonitored",
			      atomic_fetch_int32_t(&key->ssc_refcount));
}

/**
 * @brief Display an NSM client in the hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   The key or val
 */
int display_nsm_client_key_val(struct display_buffer *dspbuf,
			       struct gsh_buffdesc *buff)
{
	return display_nsm_client(dspbuf, buff->addr);
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_nsm_client(&dspbuf1, client1);
		display_nsm_client(&dspbuf2, client2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (client1 == NULL || client2 == NULL)
		return 1;

	if (client1 == client2)
		return 0;

	/* Since we always have a caller name in the key and records whether
	 * nsm_use_caller_name is true or not, we don't ever compare ssc_client,
	 * we always just compare the caller name.
	 *
	 * This makes SM_NOTIFY work because we can't know the port number
	 * which is part of identifing ssc_client. We only care about the
	 * address.
	 */

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
	unsigned int sum = 0;
	unsigned int i;

	/* Since we always have a caller name in the key and records whether
	 * nsm_use_caller_name is true or not, we don't ever compare ssc_client,
	 * we always just compare the caller name.
	 *
	 * This makes SM_NOTIFY work because we can't know the port number
	 * which is part of identifing ssc_client. We only care about the
	 * address.
	 */

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
		sum += (unsigned char)pkey->ssc_nlm_caller_name[i];

	res = (unsigned long)sum + (unsigned long)pkey->ssc_nlm_caller_name_len;

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
	unsigned int sum = 0;
	unsigned int i;

	/* Since we always have a caller name in the key and records whether
	 * nsm_use_caller_name is true or not, we don't ever compare ssc_client,
	 * we always just compare the caller name.
	 *
	 * This makes SM_NOTIFY work because we can't know the port number
	 * which is part of identifing ssc_client. We only care about the
	 * address.
	 */

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
		sum += (unsigned char)pkey->ssc_nlm_caller_name[i];

	res = (unsigned long)sum + (unsigned long)pkey->ssc_nlm_caller_name_len;

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
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     key    The NLM client
 *
 * @return the bytes remaining in the buffer.
 */
int display_nlm_client(struct display_buffer *dspbuf, state_nlm_client_t *key)
{
	int b_left;

	if (key == NULL)
		return display_printf(dspbuf, "NLM Client <NULL>");

	b_left = display_printf(dspbuf, "NLM Client %p: {", key);

	if (b_left <= 0)
		return b_left;

	b_left = display_nsm_client(dspbuf, key->slc_nsm_client);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf, "} caller_name=");

	if (b_left <= 0)
		return b_left;

	b_left = display_len_cat(dspbuf,
				 key->slc_nlm_caller_name,
				 key->slc_nlm_caller_name_len);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, " type=%s refcount=%d",
			      xprt_type_to_str(key->slc_client_type),
			      atomic_fetch_int32_t(&key->slc_refcount));
}

/**
 * @brief Display an NLM client in the hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   The key or val
 */
int display_nlm_client_key_val(struct display_buffer *dspbuf,
			       struct gsh_buffdesc *buff)
{
	return display_nlm_client(dspbuf, buff->addr);
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_nlm_client(&dspbuf1, client1);
		display_nlm_client(&dspbuf2, client2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (client1 == NULL || client2 == NULL)
		return 1;

	if (client1 == client2)
		return 0;

	if (compare_nsm_client(client1->slc_nsm_client, client2->slc_nsm_client)
	    != 0)
		return 1;

	if (cmp_sockaddr(&client1->slc_server_addr,
			 &client2->slc_server_addr,
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
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     key    The NLM owner
 *
 * @return the bytes remaining in the buffer.
 */
int display_nlm_owner(struct display_buffer *dspbuf, state_owner_t *owner)
{
	int b_left;

	if (owner == NULL)
		return display_printf(dspbuf, "STATE_LOCK_OWNER_NLM <NULL>");

	b_left = display_printf(dspbuf, "STATE_LOCK_OWNER_NLM %p: {", owner);

	if (b_left <= 0)
		return b_left;

	b_left =
	    display_nlm_client(dspbuf, owner->so_owner.so_nlm_owner.so_client);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf, "} oh=");

	if (b_left <= 0)
		return b_left;

	b_left = display_opaque_value(dspbuf,
				      owner->so_owner_val,
				      owner->so_owner_len);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, " svid=%d refcount=%d",
			      owner->so_owner.so_nlm_owner.so_nlm_svid,
			      atomic_fetch_int32_t(&owner->so_refcount));
}

/**
 * @brief Display an NLM owner in the hash table
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   The key or val
 */
int display_nlm_owner_key_val(struct display_buffer *dspbuf,
			      struct gsh_buffdesc *buff)
{
	return display_nlm_owner(dspbuf, buff->addr);
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_nlm_owner(&dspbuf1, owner1);
		display_nlm_owner(&dspbuf2, owner2);
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
	.display_key = display_nsm_client_key_val,
	.display_val = display_nsm_client_key_val,
	.flags = HT_FLAG_NONE,
};

static hash_parameter_t nlm_client_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nlm_client_value_hash_func,
	.hash_func_rbt = nlm_client_rbt_hash_func,
	.compare_key = compare_nlm_client_key,
	.display_key = display_nlm_client_key_val,
	.display_val = display_nlm_client_key_val,
	.flags = HT_FLAG_NONE,
};

static hash_parameter_t nlm_owner_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = nlm_owner_value_hash_func,
	.hash_func_rbt = nlm_owner_rbt_hash_func,
	.compare_key = compare_nlm_owner_key,
	.display_key = display_nlm_owner_key_val,
	.display_val = display_nlm_owner_key_val,
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
void _inc_nsm_client_ref(state_nsm_client_t *client,
			 char *file, int line, char *function)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool strvalid = false;
	int32_t refcount;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(&dspbuf, client);
		strvalid = true;
		/* Note that the way the logging below works, we will log at
		 * FullDebug even if it is turned off in the middle of the
		 * execution of this function since we don't test subsequently.
		 */
	}

	refcount = atomic_inc_int32_t(&client->ssc_refcount);

	if (strvalid) {
		DisplayLogComponentLevel(
			COMPONENT_STATE, file, line, function, NIV_FULL_DEBUG,
			"Increment refcount now=%" PRId32 " {%s}",
			refcount, str);
	}
}

/**
 * @brief Free an NSM client
 *
 * @param[in] client The client to free
 */
void free_nsm_client(state_nsm_client_t *client)
{
	gsh_free(client->ssc_nlm_caller_name);

	if (client->ssc_client != NULL)
		put_gsh_client(client->ssc_client);

	PTHREAD_MUTEX_destroy(&client->ssc_mutex);

	gsh_free(client);
}

/**
 * @brief Relinquish a reference on an NSM client
 *
 * @param[in] client The client to release
 */
void _dec_nsm_client_ref(state_nsm_client_t *client,
			char *file, int line, char *function)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	int32_t refcount;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(&dspbuf, client);
		str_valid = true;
		/* Note that the way the logging below works, we will log at
		 * FullDebug even if it is turned off in the middle of the
		 * execution of this function since we don't test subsequently.
		 */
	}

	refcount = atomic_dec_int32_t(&client->ssc_refcount);

	if (refcount > 0) {
		if (str_valid) {
			DisplayLogComponentLevel(
				COMPONENT_STATE, file, line, function,
				NIV_FULL_DEBUG,
				"Decrement refcount now=%" PRId32 " {%s}",
				refcount, str);
		}

		return;
	}

	if (str_valid) {
		DisplayLogComponentLevel(
			COMPONENT_STATE, file, line, function, NIV_FULL_DEBUG,
			"Try to remove {%s}", str);
	}

	buffkey.addr = client;
	buffkey.len = sizeof(*client);

	/* Since the refcnt is zero, another thread that needs this
	 * entry may delete this nsm client to insert its own.
	 * So expect not to find this nsm client or find someone
	 * else's nsm client!
	 */
	rc = hashtable_getlatch(ht_nsm_client, &buffkey, &old_value, true,
				&latch);
	switch (rc) {
	case HASHTABLE_SUCCESS:
		if (old_value.addr == client) { /* our nsm client */
			hashtable_deletelatched(ht_nsm_client, &buffkey,
						&latch, NULL, NULL);
		}
		break;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	default:
		if (!str_valid)
			display_nsm_client(&dspbuf, client);

		if (isLevel(COMPONENT_STATE, NIV_CRIT)) {
			DisplayLogComponentLevel(
				COMPONENT_STATE, file, line, function, NIV_CRIT,
				"Error %s, could not find {%s}",
				hash_table_err_to_str(rc), str);
		}

		return;
	}

	hashtable_releaselatched(ht_nsm_client, &latch);

	if (str_valid) {
		DisplayLogComponentLevel(
			COMPONENT_STATE, file, line, function, NIV_FULL_DEBUG,
			"Free {%s}", str);
	}

	nsm_unmonitor(client);
	free_nsm_client(client);
}

/**
 * @brief Get an NSM client
 *
 * @param[in] care        Care status
 * @param[in] caller_name Caller name
 *
 * @return NSM client or NULL.
 */
state_nsm_client_t *get_nsm_client(care_t care,  char *caller_name)
{
	state_nsm_client_t key;
	state_nsm_client_t *pclient;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	char hostaddr_str[SOCK_NAME_MAX];

	if (caller_name == NULL)
		return NULL;

	memset(&key, 0, sizeof(key));

	if (nfs_param.core_param.nsm_use_caller_name ||
	    op_ctx->client == NULL) {
		/* If nsm_use_caller_name is false but op_ctx->client is NULL
		 * we are being called for SM_NOTIFY. caller name is supposed to
		 * be an IP address.
		 */
		key.ssc_nlm_caller_name_len = strlen(caller_name);

		if (key.ssc_nlm_caller_name_len > LM_MAXSTRLEN)
			return NULL;

		key.ssc_nlm_caller_name = caller_name;
		LogFullDebug(COMPONENT_STATE,
			     "Using caller_name %s",
			     caller_name);
	} else {
		sockaddr_t alt_host;
		sockaddr_t *host = NULL;

		if (isFullDebug(COMPONENT_STATE)) {
			char str[LOG_BUFF_LEN] = "\0";
			struct display_buffer db = {sizeof(str), str, str};

			display_sockaddr(&db, op_ctx->caller_addr);
			LogFullDebug(COMPONENT_STATE,
				     "Using address %s as caller name",
				     str);
		}

		/* Fixup any encapsulated IPv4 addresses */
		host = convert_ipv6_to_ipv4(op_ctx->caller_addr, &alt_host);

		/* Generate caller name from fixed up address */
		if (!sprint_sockip(host, hostaddr_str, sizeof(hostaddr_str))) {
			LogCrit(COMPONENT_STATE,
				"Could not generate caller name");
			return NULL;
		}

		LogFullDebug(COMPONENT_STATE,
			     "Using caller address %s",
			     hostaddr_str);

		key.ssc_nlm_caller_name = hostaddr_str;
		key.ssc_nlm_caller_name_len = strlen(key.ssc_nlm_caller_name);
		key.ssc_client = op_ctx->client;
	}

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(&dspbuf, &key);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nsm_client, &buffkey, &buffval, true,
				&latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		pclient = buffval.addr;
		if (atomic_inc_int32_t(&pclient->ssc_refcount) == 1) {
			/* This nsm client is in the process of getting
			 * deleted. Delete it from the hash table and
			 * pretend as though we didn't find it.
			 */
			(void)atomic_dec_int32_t(&pclient->ssc_refcount);
			hashtable_deletelatched(ht_nsm_client, &buffkey,
						&latch, NULL, NULL);
			break;
		}

		/* Return the found NSM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			/* Clear out key and display found client */
			display_reset_buffer(&dspbuf);
			display_nsm_client(&dspbuf, pclient);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		hashtable_releaselatched(ht_nsm_client, &latch);

		if (care == CARE_MONITOR && !nsm_monitor(pclient)) {
			dec_nsm_client_ref(pclient);
			pclient = NULL;
		}

		return pclient;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	default:
		display_nsm_client(&dspbuf, &key);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return NULL;
	}

	/* Not found, but we don't care, return NULL */
	if (care == CARE_NOT) {
		/* Return the found NSM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nsm_client(&dspbuf, &key);
			LogFullDebug(COMPONENT_STATE, "Ignoring {%s}", str);
		}

		hashtable_releaselatched(ht_nsm_client, &latch);

		return NULL;
	}

	pclient = gsh_malloc(sizeof(*pclient));

	/* Copy everything over */
	memcpy(pclient, &key, sizeof(key));

	PTHREAD_MUTEX_init(&pclient->ssc_mutex, NULL);

	/* Need deep copy of caller name */
	pclient->ssc_nlm_caller_name = gsh_strdup(key.ssc_nlm_caller_name);

	glist_init(&pclient->ssc_lock_list);
	glist_init(&pclient->ssc_share_list);
	pclient->ssc_refcount = 1;

	if (op_ctx->client != NULL) {
		pclient->ssc_client = op_ctx->client;
		inc_gsh_client_refcount(op_ctx->client);
	}

	if (isFullDebug(COMPONENT_STATE)) {
		display_nsm_client(&dspbuf, pclient);
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
		display_nsm_client(&dspbuf, pclient);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		PTHREAD_MUTEX_destroy(&pclient->ssc_mutex);
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

	gsh_free(client->slc_nlm_caller_name);

	/* free the callback client */
	if (client->slc_callback_clnt != NULL)
		CLNT_DESTROY(client->slc_callback_clnt);

	gsh_free(client);
}

/**
 * @brief Take a reference on an NLM client
 *
 * @param[in] client The client to reference
 */
void inc_nlm_client_ref(state_nlm_client_t *client)
{
	(void) atomic_inc_int32_t(&client->slc_refcount);
}

/**
 * @brief Relinquish a reference on an NLM client
 *
 * @param[in] client The client to release
 */
void dec_nlm_client_ref(state_nlm_client_t *client)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	struct gsh_buffdesc old_key;
	int32_t refcount;

	if (isDebug(COMPONENT_STATE)) {
		display_nlm_client(&dspbuf, client);
		str_valid = true;
	}

	refcount = atomic_dec_int32_t(&client->slc_refcount);

	if (refcount > 0) {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "Decrement refcount now=%" PRId32 " {%s}",
				     refcount, str);

		return;
	}

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Try to remove {%s}", str);

	buffkey.addr = client;
	buffkey.len = sizeof(*client);

	/* Get the hash table entry and hold latch */
	rc = hashtable_getlatch(ht_nlm_client, &buffkey, &old_value, true,
				&latch);

	/* Since the refcnt is zero, another thread that needs this
	 * entry may delete this nlm client to insert its own nlm
	 * client. So expect not to find this nlm client or find someone
	 * else's nlm client!
	 */
	switch (rc) {
	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	case HASHTABLE_SUCCESS:
		if (old_value.addr == client) { /* our nlm client */
			hashtable_deletelatched(ht_nlm_client, &buffkey,
						&latch, &old_key, &old_value);
		}
		break;

	default:
		if (!str_valid)
			display_nlm_client(&dspbuf, client);
		LogCrit(COMPONENT_STATE,
			"Error %s, could not find {%s}, client=%p",
			hash_table_err_to_str(rc), str, client);
		return;
	}


	/* Release the latch */
	hashtable_releaselatched(ht_nlm_client, &latch);

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	free_nlm_client(client);
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
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	sockaddr_t local_addr;
	socklen_t addr_len;
	uint32_t refcount;

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
		memcpy(&(key.slc_server_addr), &local_addr, sizeof(sockaddr_t));
	}

	if (key.slc_nlm_caller_name_len > LM_MAXSTRLEN)
		return NULL;

	key.slc_nlm_caller_name = caller_name;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_client(&dspbuf, &key);
		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);
	}

	buffkey.addr = &key;
	buffkey.len = sizeof(key);

	rc = hashtable_getlatch(ht_nlm_client, &buffkey, &buffval, true,
				&latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		pclient = buffval.addr;

		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_client(&dspbuf, pclient);
			LogFullDebug(COMPONENT_STATE, "Found {%s}", str);
		}

		refcount = atomic_inc_int32_t(&pclient->slc_refcount);
		if (refcount == 1) {
			/* This nlm client is in the process of getting
			 * deleted. Let us delete it from the hash table
			 * and pretend as though it isn't found in the
			 * hash table. The thread that is trying to
			 * delete this entry will not find it in the
			 * hash table but will free its nlm client.
			 */
			(void)atomic_dec_int32_t(&pclient->slc_refcount);
			hashtable_deletelatched(ht_nlm_client, &buffkey,
						&latch, NULL, NULL);
			goto not_found;
		}

		hashtable_releaselatched(ht_nlm_client, &latch);

		if (care == CARE_MONITOR && !nsm_monitor(nsm_client)) {
			dec_nlm_client_ref(pclient);
			pclient = NULL;
		}

		return pclient;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		goto not_found;

	default:
		display_nlm_client(&dspbuf, &key);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return NULL;
	}

not_found:
	/* Not found, but we don't care, return NULL */
	if (care == CARE_NOT) {
		/* Return the found NLM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_nlm_client(&dspbuf, &key);
			LogFullDebug(COMPONENT_STATE, "Ignoring {%s}", str);
		}

		hashtable_releaselatched(ht_nlm_client, &latch);

		return NULL;
	}

	pclient = gsh_malloc(sizeof(*pclient));

	/* Copy everything over */
	memcpy(pclient, &key, sizeof(key));

	pclient->slc_nlm_caller_name = gsh_strdup(key.slc_nlm_caller_name);

	/* Take a reference to the NSM Client */
	inc_nsm_client_ref(nsm_client);

	pclient->slc_refcount = 1;

	if (isFullDebug(COMPONENT_STATE)) {
		display_nlm_client(&dspbuf, pclient);
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
		display_nlm_client(&dspbuf, pclient);

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
