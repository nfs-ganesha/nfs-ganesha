/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @addtogroup HashTable
 * @{
 */

/**
 * @file ht_shutdown.h
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Remove items from a hash table without taking locks
 *
 * @note This file is intended for use with the shutdown procedure.
 * Functions declared here take no locks to avoid a potential hang in
 * the event that a thread was cancelled while holding one.  They
 * *must not* be called while any threads accessing SAL, Cache Inode,
 * or FSAL are running.  In general, you should not include this file.
 */

#ifndef HT_SHUTDOWN_H
#define HT_SHUTDOWN_H
#include "abstract_mem.h"
#include "hashtable.h"
#include "rbt_tree.h"

static inline int cache_offsetof(struct hash_table *ht, struct rbt_node *node)
{
	return RBT_VALUE(node) % ht->parameter.cache_entry_count;
}

static inline void ht_unsafe_zap(struct hash_table *ht,
				 struct hash_partition *partition,
				 struct rbt_node *node)
{
	/* The pair of buffer descriptors comprising the stored entry */
	struct hash_data *data = NULL;

	data = RBT_OPAQ(node);

	/* Clear cache */
	if (partition->cache) {
		uint32_t offset = cache_offsetof(ht, node);
		struct rbt_node *cnode = partition->cache[offset];
		if (cnode)
			partition->cache[offset] = NULL;
	}

	/* Now remove the entry */
	RBT_UNLINK(&partition->rbt, node);
	pool_free(ht->data_pool, data);
	pool_free(ht->node_pool, node);
	--(partition->count);
}

static inline void ht_unsafe_zap_by_key(struct hash_table *ht,
					struct gsh_buffdesc *key)
{
	/* Index of the subtree */
	uint32_t index = 0;
	/* RBT lookup key */
	uint64_t rbt_hash = 0;
	/* The current partition */
	struct hash_partition *partition = NULL;
	/* The root of the red black tree matching this index */
	struct rbt_head *root = NULL;
	/* A pair of buffer descriptors locating key and value for this
	   entry */
	struct hash_data *data = NULL;
	/* The node in the red-black tree currently being traversed */
	struct rbt_node *cursor = NULL;
	/* true if we have located the key */
	int found = false;

	if (ht->parameter.hash_func_both) {
		ht->parameter.hash_func_both(&ht->parameter, key, &index,
					     &rbt_hash);
	} else {
		index = ht->parameter.hash_func_key(&ht->parameter, key);
		rbt_hash = ht->parameter.hash_func_rbt(&ht->parameter, key);
	}

	partition = &(ht->partitions[index]);

	root = &(ht->partitions[index].rbt);

	RBT_FIND_LEFT(root, cursor, rbt_hash);
	if (cursor == NULL)
		return;

	while ((cursor != NULL) && (RBT_VALUE(cursor) == rbt_hash)) {
		data = RBT_OPAQ(cursor);
		if (ht->parameter.compare_key(key, &(data->key)) == 0) {
			if (partition->cache) {
				partition->cache[cache_offsetof(ht,
								cursor)] = NULL;
			}
			found = true;
			break;
		}
		RBT_INCREMENT(cursor);
	}

	if (!found)
		return;

	ht_unsafe_zap(ht, partition, cursor);
}

#endif				/* !HT_SHUTDOWN_H */

/** @} */
