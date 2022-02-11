// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * @return the bytes remaining in the buffer.
 */

int display_9p_owner(struct display_buffer *dspbuf, state_owner_t *owner)
{
	int b_left;

	if (owner == NULL)
		return display_printf(dspbuf, "<NULL>");

	b_left = display_printf(dspbuf, "STATE_LOCK_OWNER_9P %p", owner);

	if (b_left <= 0)
		return b_left;

	b_left = display_sockaddr(dspbuf,
				  &owner->so_owner.so_9p_owner.client_addr);

	if (b_left <= 0)
		return b_left;

	b_left = display_printf(dspbuf, " proc_id=%u",
				owner->so_owner.so_9p_owner.proc_id);

	if (b_left <= 0)
		return b_left;

	return display_printf(dspbuf, " refcount=%d",
			      atomic_fetch_int32_t(&owner->so_refcount));
}

/**
 * @brief Display owner from hash key
 *
 * @param[in]  dspbuf display buffer to display into
 * @param[in]  buff   Buffer pointing to owner
 */

int display_9p_owner_key_val(struct display_buffer *dspbuf,
			     struct gsh_buffdesc *buff)
{
	return display_9p_owner(dspbuf, buff->addr);
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
		char str1[LOG_BUFF_LEN / 2] = "\0";
		char str2[LOG_BUFF_LEN / 2] = "\0";
		struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
		struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

		display_9p_owner(&dspbuf1, owner1);
		display_9p_owner(&dspbuf2, owner2);
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
	     sizeof(sockaddr_t)))
		return 1;
#endif

	/* so_owner_len is always 0, don't compare so_owner_val */
	return 0;
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
	unsigned long res;
	state_owner_t *pkey = key->addr;

	struct sockaddr_in *paddr =
	    (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr;

	/* so_owner_len is always zero so don't bother with so_owner_val */

	/** @todo using sin_addr.s_addr as an int makes this only work for
	 *        IPv4.
	 */
	res = (unsigned long)(pkey->so_owner.so_9p_owner.proc_id) +
	      (unsigned long)paddr->sin_addr.s_addr;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "value = %lu",
			     res % hparam->index_size);

	return (uint32_t)(res % hparam->index_size);

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
	uint64_t res;
	state_owner_t *pkey = key->addr;

	struct sockaddr_in *paddr =
	    (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr;

	/* so_owner_len is always zero so don't bother with so_owner_val */

	/** @todo using sin_addr.s_addr as an int makes this only work for
	 *        IPv4.
	 */
	res = (uint64_t)(pkey->so_owner.so_9p_owner.proc_id) +
	      (uint64_t)paddr->sin_addr.s_addr;

	if (isDebug(COMPONENT_HASHTABLE))
		LogFullDebug(COMPONENT_STATE, "rbt = %" PRIu64, res);

	return res;
}

static hash_parameter_t _9p_owner_hash_param = {
	.index_size = PRIME_STATE,
	.hash_func_key = _9p_owner_value_hash_func,
	.hash_func_rbt = _9p_owner_rbt_hash_func,
	.compare_key = compare_9p_owner_key,
	.display_key = display_9p_owner_key_val,
	.display_val = display_9p_owner_key_val,
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

state_owner_t *get_9p_owner(sockaddr_t *client_addr,
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
