/*
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
 * @file 9p_owner.c
 * @brief Management of the 9P owner cache.
 */

#include "config.h"
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "log.h"
#include "hashtable.h"
#include "nfs_core.h"
#include "sal_functions.h"

/**
 * @brief Hash table for 9p owners
 */
hash_table_t *ht_9p_owner;

/**
 * @brief Display a 9p owner
 *
 * @param[in]  key The 9P owner
 * @param[out] str Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner(state_owner_t *key, char *str)
{
	char *strtmp = str;

	if (key == NULL)
		return sprintf(str, "<NULL>");

	strtmp += sprintf(strtmp, "STATE_LOCK_OWNER_9P %p", key);
	strtmp +=
	    sprint_sockaddr((sockaddr_t *) &
			    (key->so_owner.so_9p_owner.client_addr), strtmp,
			    SOCK_NAME_MAX);

	strtmp +=
	    sprintf(strtmp, " proc_id=%u", key->so_owner.so_9p_owner.proc_id);

	strtmp +=
	    sprintf(strtmp, " refcount=%d",
		    atomic_fetch_int32_t(&key->so_refcount));

	return strtmp - str;
}

/**
 * @brief Display owner from hash key
 *
 * @param[in]  buff Buffer pointing to owner
 * @param[out] str  Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner_key(struct gsh_buffdesc *buff, char *str)
{
	return display_9p_owner(buff->addr, str);
}

/**
 * @brief Display owner from hash value
 *
 * @param[in]  buff Buffer pointing to owner
 * @param[out] str  Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner_val(struct gsh_buffdesc *buff, char *str)
{
	return display_9p_owner(buff->addr, str);
}

/**
 * @brief Compare two 9p owners
 *
 * @param[in] owner1 One owner
 * @param[in] owner2 Another owner
 *
 * @retval 1 if they differ.
 * @retval 0 if they're identical.
 */

int compare_9p_owner(state_owner_t *owner1, state_owner_t *owner2)
{
	if (isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE)) {
		char str1[HASHTABLE_DISPLAY_STRLEN];
		char str2[HASHTABLE_DISPLAY_STRLEN];

		display_9p_owner(owner1, str1);
		display_9p_owner(owner2, str2);
		LogFullDebug(COMPONENT_STATE, "{%s} vs {%s}", str1, str2);
	}

	if (owner1 == NULL || owner2 == NULL)
		return 1;

	if (owner1 == owner2)
		return 0;

	if (owner1->so_owner.so_9p_owner.proc_id !=
	    owner2->so_owner.so_9p_owner.proc_id)
		return 1;
#if 0
	if (memcmp
	    (&owner1->so_owner.so_9p_owner.client_addr,
	     &owner2->so_owner.so_9p_owner.client_addr,
	     sizeof(struct sockaddr_storage)))
		return 1;
#endif

	if (owner1->so_owner_len != owner2->so_owner_len)
		return 1;

	return memcmp(owner1->so_owner_val, owner2->so_owner_val,
		      owner1->so_owner_len);
}

/**
 * @brief Compare two keys in the 9p owner hash table
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another key
 *
 * @retval 1 if they differ.
 * @retval 0 if they're the same.
 */

int compare_9p_owner_key(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
	return compare_9p_owner(buff1->addr, buff2->addr);

}

/**
 * @brief Get the hash index from a 9p owner
 *
 * @param[in] hparam Hash parameters
 * @param[in] key The key to hash
 *
 * @return The hash index.
 */

uint32_t _9p_owner_value_hash_func(hash_parameter_t *hparam,
				   struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	state_owner_t *pkey = key->addr;

	struct sockaddr_in *paddr =
	    (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++)
		sum += (unsigned char)pkey->so_owner_val[i];

	res =
	    (unsigned long)(pkey->so_owner.so_9p_owner.proc_id) +
	    (unsigned long)paddr->sin_addr.s_addr + (unsigned long)sum +
	    (unsigned long)pkey->so_owner_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (unsigned long)(res % hparam->index_size);

}

/**
 * @brief Get the RBT hash from a 9p owner
 *
 * @param[in] hparam Hash parameters
 * @param[in] key The key to hash
 *
 * @return The RBT hash.
 */

uint64_t _9p_owner_rbt_hash_func(hash_parameter_t *hparam,
				 struct gsh_buffdesc *key)
{
	unsigned int sum = 0;
	unsigned int i;
	unsigned long res;
	state_owner_t *pkey = key->addr;

	struct sockaddr_in *paddr =
	    (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr;

	/* Compute the sum of all the characters */
	for (i = 0; i < pkey->so_owner_len; i++)
		sum += (unsigned char)pkey->so_owner_val[i];

	res =
	    (unsigned long)(pkey->so_owner.so_9p_owner.proc_id) +
	    (unsigned long)paddr->sin_addr.s_addr + (unsigned long)sum +
	    (unsigned long)pkey->so_owner_len;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

	return res;
}

static hash_parameter_t _9p_owner_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = _9p_owner_value_hash_func,
	.hash_func_rbt = _9p_owner_rbt_hash_func,
	.compare_key = compare_9p_owner_key,
	.key_to_str = display_9p_owner_key,
	.val_to_str = display_9p_owner_val,
	.flags = HT_FLAG_NONE,
};

/**
 * @brief Init the hashtable for 9P Owner cache
 *
 * @retval 0 if successful.
 * @retval -1 otherwise.
 */

int Init_9p_hash(void)
{
	ht_9p_owner = hashtable_init(&_9p_owner_hash_param);

	if (ht_9p_owner == NULL) {
		LogCrit(COMPONENT_STATE, "Cannot init 9P Owner cache");
		return -1;
	}

	return 0;
}

/**
 * @brief Look up a 9p owner
 *
 * @param[in] client_addr 9p client address
 * @param[in] proc_id     Process ID of owning process
 *
 * @return The found owner or NULL.
 */

state_owner_t *get_9p_owner(struct sockaddr_storage *client_addr,
			    uint32_t proc_id)
{
	state_owner_t key;

	memset(&key, 0, sizeof(key));

	key.so_type = STATE_LOCK_OWNER_9P;
	key.so_refcount = 1;
	key.so_owner.so_9p_owner.proc_id = proc_id;

	memcpy(&key.so_owner.so_9p_owner.client_addr, client_addr,
	       sizeof(*client_addr));

	return get_state_owner(CARE_ALWAYS, &key, NULL, NULL);
}

/** @} */
