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
 * @addtogroup hashtable
 * @{
 */

/**
 * @file hashtable.c
 * @brief Implement an RBTree-based partitioend hash lookup
 *
 * This file implements a partitioned, tree-based, concurrent
 * hash-lookup structure.  For every key, two values are derived that
 * determine its location within the structure: an index, which
 * determines which of the partitions (each containing a tree and each
 * separately locked), and a hash which acts as the key within an
 * individual Red-Black Tree.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "hashtable.h"
#include "log.h"
#include "abstract_atomic.h"
#include "common_utils.h"
#include <assert.h>

/**
 * @brief Total size of the cache page configured for a table
 *
 * This function returns the size of the cache page for the given hash
 * table, based on the configured entry count.
 *
 * @param[in] ht The hash table to query
 *
 * @return The cache page size
 */
static inline size_t
cache_page_size(const hash_table_t *ht)
{
	return (ht->parameter.cache_entry_count) * sizeof(struct rbt_node *);
}

/**
 * @brief Offset of a pointer in the cache
 *
 * This function returns the offset into a cache array of the given
 * hash value.
 *
 * @param[in] ht      The hash table to query
 * @param[in] rbthash The hash value to look up
 *
 * @return the offset into the cache at which the hash value might be
 *         found
 */
static inline int
cache_offsetof(struct hash_table *ht, uint64_t rbthash)
{
	return rbthash % ht->parameter.cache_entry_count;
}

/**
 * @brief Return an error string for an error code
 *
 * This function returns an error string corresponding to the supplied
 * error code.
 *
 * @param[in] err The error code to look up
 *
 * @return An error string or "UNKNOWN HASH TABLE ERROR"
 */
const char *
hash_table_err_to_str(hash_error_t err)
{
	switch (err) {
	case HASHTABLE_SUCCESS:
		return "HASHTABLE_SUCCESS";
	case HASHTABLE_UNKNOWN_HASH_TYPE:
		return "HASHTABLE_UNKNOWN_HASH_TYPE";
	case HASHTABLE_ERROR_NO_SUCH_KEY:
		return "HASHTABLE_ERROR_NO_SUCH_KEY";
	case HASHTABLE_ERROR_KEY_ALREADY_EXISTS:
		return "HASHTABLE_ERROR_KEY_ALREADY_EXISTS";
	case HASHTABLE_ERROR_INVALID_ARGUMENT:
		return "HASHTABLE_ERROR_INVALID_ARGUMENT";
	case HASHTABLE_ERROR_DELALL_FAIL:
		return "HASHTABLE_ERROR_DELALL_FAIL";
	case HASHTABLE_NOT_DELETED:
		return "HASHTABLE_NOT_DELETED";
	case HASHTABLE_OVERWRITTEN:
		return "HASHTABLE_OVERWRITTEN";
	}

	return "UNKNOWN HASH TABLE ERROR";
}

/**
 * @brief Locate a key within a partition
 *
 * This function traverses the red-black tree within a hash table
 * partition and returns, if one exists, a pointer to a node matching
 * the supplied key.
 *
 * @param[in]  ht      The hashtable to be used
 * @param[in]  key     The key to look up
 * @param[in]  index   Index into RBT array
 * @param[in]  rbthash Hash in red-black tree
 * @param[out] node    On success, the found node, NULL otherwise
 *
 * @retval HASHTABLE_SUCCESS if successfull
 * @retval HASHTABLE_NO_SUCH_KEY if key was not found
 */
static hash_error_t
key_locate(struct hash_table *ht, const struct gsh_buffdesc *key,
	   uint32_t index, uint64_t rbthash, struct rbt_node **node)
{
	/* The current partition */
	struct hash_partition *partition = &(ht->partitions[index]);

	/* The root of the red black tree matching this index */
	struct rbt_head *root = NULL;

	/* A pair of buffer descriptors locating key and value for this
	   entry */
	struct hash_data *data = NULL;

	/* The node in the red-black tree currently being traversed */
	struct rbt_node *cursor = NULL;

	/* true if we have located the key */
	int found = false;

	*node = NULL;

	if (partition->cache) {
		void **cache_slot = (void **)
		    &(partition->cache[cache_offsetof(ht, rbthash)]);
		cursor = atomic_fetch_voidptr(cache_slot);
		LogFullDebug(COMPONENT_HASHTABLE_CACHE,
			     "hash %s index %" PRIu32 " slot %d",
			     (cursor) ? "hit" : "miss", index,
			     cache_offsetof(ht, rbthash));
		if (cursor) {
			data = RBT_OPAQ(cursor);
			if (ht->parameter.compare_key(
						(struct gsh_buffdesc *)key,
						&(data->key)) == 0) {
				goto out;
			}
		}
	}

	root = &(ht->partitions[index].rbt);

	/* The lefmost occurrence of the value is the one from which we
	   may start iteration to visit all nodes containing a value. */
	RBT_FIND_LEFT(root, cursor, rbthash);

	if (cursor == NULL) {
		if (isFullDebug(COMPONENT_HASHTABLE)
		    && isFullDebug(ht->parameter.ht_log_component))
			LogFullDebug(ht->parameter.ht_log_component,
				     "Key not found: rbthash = %" PRIu64,
				     rbthash);
		return HASHTABLE_ERROR_NO_SUCH_KEY;
	}

	while ((cursor != NULL) && (RBT_VALUE(cursor) == rbthash)) {
		data = RBT_OPAQ(cursor);
		if (ht->parameter.compare_key((struct gsh_buffdesc *)key,
					      &(data->key)) == 0) {
			if (partition->cache) {
				void **cache_slot = (void **)
				    &partition->cache[cache_offsetof(ht,
								     rbthash)];
				atomic_store_voidptr(cache_slot, cursor);
			}
			found = true;
			break;
		}
		RBT_INCREMENT(cursor);
	}

	if (!found) {
		if (isFullDebug(COMPONENT_HASHTABLE)
		    && isFullDebug(ht->parameter.ht_log_component))
			LogFullDebug(ht->parameter.ht_log_component,
				     "Matching hash found, but no matching key.");
		return HASHTABLE_ERROR_NO_SUCH_KEY;
	}

 out:
	*node = cursor;

	return HASHTABLE_SUCCESS;
}

/**
 * @brief Compute the values to search a hash store
 *
 * This function computes the index and RBT hash values for the
 * specified key.
 *
 * @param[in]  ht       The hash table whose parameters determine computation
 * @param[in]  key      The key from which to compute the values
 * @param[out] index    The partition index
 * @param[out] rbt_hash The hash in the Red-Black tree
 *
 * @retval HASHTABLE_SUCCESS if values computed
 * @retval HASHTABLE_ERROR_INVALID_ARGUMENT if the supplied function
 *         fails
 */

static inline hash_error_t
compute(struct hash_table *ht, const struct gsh_buffdesc *key,
	uint32_t *index, uint64_t *rbt_hash)
{
	/* Compute the partition index and red-black tree hash */
	if (ht->parameter.hash_func_both) {
		if (!(*(ht->parameter.hash_func_both))
		    (&ht->parameter, (struct gsh_buffdesc *)key, index,
		     rbt_hash))
			return HASHTABLE_ERROR_INVALID_ARGUMENT;
	} else {
		*index =
		    (*(ht->parameter.hash_func_key)) (&ht->parameter,
						      (struct gsh_buffdesc *)
						      key);
		*rbt_hash =
		    (*(ht->parameter.hash_func_rbt)) (&ht->parameter,
						      (struct gsh_buffdesc *)
						      key);
	}

	/* At the suggestion of Jim Lieb, die if a hash function sends
	   us off the end of the array. */

	assert(*index < ht->parameter.index_size);

	return HASHTABLE_SUCCESS;
}

/* The following are the hash table primitives implementing the
   actual functionality. */

/**
 * @brief Initialize a new hash table
 *
 * This function initializes and allocates storage for a hash table.
 *
 * @param[in] hparam Parameters to determine the hash table's
 *                    behaviour
 *
 * @return Pointer to the new hash table, NULL on failure
 *
 */

struct hash_table *
hashtable_init(struct hash_param *hparam)
{
	/* The hash table being constructed */
	struct hash_table *ht = NULL;
	/* The index for initializing each partition */
	uint32_t index = 0;
	/* Read-Write Lock attributes, to prevent write starvation under
	   GLIBC */
	pthread_rwlockattr_t rwlockattr;
	/* Hash partition */
	struct hash_partition *partition = NULL;
	/* The number of fully initialized partitions */
	uint32_t completed = 0;

	if (pthread_rwlockattr_init(&rwlockattr) != 0)
		return NULL;

	/* At some point factor this out into the OS directory.  it is
	   necessary to prevent writer starvation under GLIBC. */
#ifdef GLIBC
	if ((pthread_rwlockattr_setkind_np
	     (&rwlockattrs,
	      PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP)) != 0) {
		LogCrit(COMPONENT_HASHTABLE,
			"Unable to set writer-preference on lock attribute.");
		goto deconstruct;
	}
#endif				/* GLIBC */

	ht = gsh_calloc(1, sizeof(struct hash_table) +
			(sizeof(struct hash_partition) *
			 hparam->index_size));

	/* Fixup entry size */
	if (hparam->flags & HT_FLAG_CACHE) {
		if (!hparam->cache_entry_count)
			/* works fine with a good hash algo */
			hparam->cache_entry_count = 32767;
	}

	/* We need to save copy of the parameters in the table. */
	ht->parameter = *hparam;
	for (index = 0; index < hparam->index_size; ++index) {
		partition = (&ht->partitions[index]);
		RBT_HEAD_INIT(&(partition->rbt));

		if (pthread_rwlock_init(&partition->lock, &rwlockattr) != 0) {
			LogCrit(COMPONENT_HASHTABLE,
				"Unable to initialize lock in hash table.");
			goto deconstruct;
		}

		/* Allocate a cache if requested */
		if (hparam->flags & HT_FLAG_CACHE)
			partition->cache = gsh_calloc(1, cache_page_size(ht));

		completed++;
	}

	ht->node_pool = pool_basic_init(NULL, sizeof(rbt_node_t));
	ht->data_pool = pool_basic_init(NULL, sizeof(struct hash_data));

	pthread_rwlockattr_destroy(&rwlockattr);
	return ht;

 deconstruct:

	while (completed != 0) {
		if (hparam->flags & HT_FLAG_CACHE)
			gsh_free(ht->partitions[completed - 1].cache);

		PTHREAD_RWLOCK_destroy(&(ht->partitions[completed - 1].lock));
		completed--;
	}
	if (ht->node_pool)
		pool_destroy(ht->node_pool);
	if (ht->data_pool)
		pool_destroy(ht->data_pool);

	gsh_free(ht);
	return ht = NULL;
}

/**
 * @brief Dispose of a hash table
 *
 * This function deletes all the entries from the given hash table and
 * then destroys the hash table.
 *
 * @param[in,out] ht        Pointer to the hash table.  After calling
 *                          this function, the memory pointed to by ht
 *                          must not be accessed in any way.
 * @param[in]     free_func Function to free entries as they are
 *                          deleted
 *
 * @return HASHTABLE_SUCCESS on success, other things on failure
 */
hash_error_t
hashtable_destroy(struct hash_table *ht,
		  int (*free_func)(struct gsh_buffdesc,
				   struct gsh_buffdesc))
{
	size_t index = 0;
	hash_error_t hrc = HASHTABLE_SUCCESS;

	hrc = hashtable_delall(ht, free_func);
	if (hrc != HASHTABLE_SUCCESS)
		goto out;

	for (index = 0; index < ht->parameter.index_size; ++index) {
		if (ht->partitions[index].cache) {
			gsh_free(ht->partitions[index].cache);
			ht->partitions[index].cache = NULL;
		}

		PTHREAD_RWLOCK_destroy(&(ht->partitions[index].lock));
	}
	pool_destroy(ht->node_pool);
	pool_destroy(ht->data_pool);
	gsh_free(ht);

 out:
	return hrc;
}

/**
 * @brief acquire the partition lock for the given key
 *
 * This function acquires the partition write mode lock corresponding to
 * the given key.
 *
 * @brief[in]  ht        The hash table to search
 * @brief[in]  key       The key for which to acquire the partition lock
 * @brief[out] latch     Opaque structure holding partition information
 *
 * @retval HASHTABLE_SUCCESS, the partition lock is acquired.
 * @retval Others, failure, the partition lock is not acquired
 *
 * NOTES: fast path if the caller just needs to lock the partition and
 * doesn't need to look for the entry. The lock needs to be released
 * with hashtable_releaselatched()
 */
hash_error_t
hashtable_acquire_latch(struct hash_table *ht,
			const struct gsh_buffdesc *key,
			struct hash_latch *latch)
{
	uint32_t index;
	uint64_t rbt_hash;
	hash_error_t rc = HASHTABLE_SUCCESS;

	memset(latch, 0, sizeof(struct hash_latch));
	rc = compute(ht, key, &index, &rbt_hash);
	if (rc != HASHTABLE_SUCCESS)
		return rc;

	latch->index = index;
	PTHREAD_RWLOCK_wrlock(&(ht->partitions[index].lock));

	return HASHTABLE_SUCCESS;
}

/**
 * @brief Look up an entry, latching the table
 *
 * This function looks up an entry in the hash table and latches the
 * partition in which that entry would belong in preparation for other
 * activities.  This function is a primitive and is intended more for
 * use building other access functions than for client code itself.
 *
 * @brief[in]  ht        The hash table to search
 * @brief[in]  key       The key for which to search
 * @brief[out] val       The value found
 * @brief[in]  may_write This must be true if the followup call might
 *                       mutate the hash table (set or delete)
 * @brief[out] latch     Opaque structure holding information on the
 *                       table.
 *
 * @retval HASHTABLE_SUCCESS The entry was found, the table is
 *         latched.
 * @retval HASHTABLE_ERROR_NOT_FOUND The entry was not found, the
 *         table is latched.
 * @retval Others, failure, the table is not latched.
 */
hash_error_t
hashtable_getlatch(struct hash_table *ht,
		   const struct gsh_buffdesc *key,
		   struct gsh_buffdesc *val, bool may_write,
		   struct hash_latch *latch)
{
	/* The index specifying the partition to search */
	uint32_t index = 0;
	/* The node found for the key */
	struct rbt_node *locator = NULL;
	/* The buffer descritpros for the key and value for the found entry */
	struct hash_data *data = NULL;
	/* The hash value to be searched for within the Red-Black tree */
	uint64_t rbt_hash = 0;
	/* Stored error return */
	hash_error_t rc = HASHTABLE_SUCCESS;

	/* This combination of options makes no sense ever */
	assert(!(may_write && !latch));

	rc = compute(ht, key, &index, &rbt_hash);
	if (rc != HASHTABLE_SUCCESS)
		return rc;

	/* Acquire mutex */
	if (may_write)
		PTHREAD_RWLOCK_wrlock(&(ht->partitions[index].lock));
	else
		PTHREAD_RWLOCK_rdlock(&(ht->partitions[index].lock));

	rc = key_locate(ht, key, index, rbt_hash, &locator);

	if (rc == HASHTABLE_SUCCESS) {
		/* Key was found */
		data = RBT_OPAQ(locator);
		if (val) {
			val->addr = data->val.addr;
			val->len = data->val.len;
		}

		if (isDebug(COMPONENT_HASHTABLE)
		    && isFullDebug(ht->parameter.ht_log_component)) {
			char dispval[HASHTABLE_DISPLAY_STRLEN];
			struct display_buffer valbuf = {
				sizeof(dispval), dispval, dispval};

			if (ht->parameter.display_val != NULL)
				ht->parameter.display_val(&valbuf, &data->val);
			else
				dispval[0] = '\0';

			LogFullDebug(ht->parameter.ht_log_component,
				     "Get %s returning Value=%p {%s}",
				     ht->parameter.ht_name, data->val.addr,
				     dispval);
		}
	}

	if (((rc == HASHTABLE_SUCCESS) || (rc == HASHTABLE_ERROR_NO_SUCH_KEY))
	    && (latch != NULL)) {
		latch->index = index;
		latch->rbt_hash = rbt_hash;
		latch->locator = locator;
	} else {
		PTHREAD_RWLOCK_unlock(&ht->partitions[index].lock);
	}

	if (rc != HASHTABLE_SUCCESS && isDebug(COMPONENT_HASHTABLE)
	    && isFullDebug(ht->parameter.ht_log_component))
		LogFullDebug(ht->parameter.ht_log_component,
			     "Get %s returning failure %s",
			     ht->parameter.ht_name, hash_table_err_to_str(rc));

	return rc;
}

/**
 *
 * @brief Release lock held on hash table
 *
 * This function releases the lock on the hash partition acquired and
 * retained by a call to hashtable_getlatch.  This function must be
 * used to free any acquired lock but ONLY if the lock was not already
 * freed by some other means (hashtable_setlatched or
 * HashTable_DelLatched).
 *
 * @param[in] ht    The hash table with the lock to be released
 * @param[in] latch The latch structure holding retained state
 */

void
hashtable_releaselatched(struct hash_table *ht, struct hash_latch *latch)
{
	if (latch) {
		PTHREAD_RWLOCK_unlock(&ht->partitions[latch->index].lock);
		memset(latch, 0, sizeof(struct hash_latch));
	}
}

/**
 * @brief Set a value in a table following a previous GetLatch
 *
 * This function sets a value in a hash table following a previous
 * call to the hashtable_getlatch function.  It must only be used
 * after such a call made with the may_write parameter set to true.
 * In all cases, the lock on the hash table is released.
 *
 * @param[in,out] ht          The hash store to be modified
 * @param[in]     key         A buffer descriptor locating the key to set
 * @param[in]     val         A buffer descriptor locating the value to insert
 * @param[in]     latch       A pointer to a structure filled by a previous
 *                            call to hashtable_getlatched.
 * @param[in]     overwrite   If true, overwrite a prexisting key,
 *                            otherwise return error on collision.
 * @param[out]    stored_key If non-NULL, a buffer descriptor for an
 *                           overwritten key as stored.
 * @param[out]    stored_val If non-NULL, a buffer descriptor for an
 *                           overwritten value as stored.
 *
 * @retval HASHTABLE_SUCCESS on non-colliding insert
 * @retval HASHTABLE_ERROR_KEY_ALREADY_EXISTS if overwrite disabled
 * @retval HASHTABLE_OVERWRITTEN on successful overwrite
 * @retval Other errors on failure
 */

hash_error_t
hashtable_setlatched(struct hash_table *ht,
		     struct gsh_buffdesc *key,
		     struct gsh_buffdesc *val,
		     struct hash_latch *latch, int overwrite,
		     struct gsh_buffdesc *stored_key,
		     struct gsh_buffdesc *stored_val)
{
	/* Stored error return */
	hash_error_t rc = HASHTABLE_SUCCESS;
	/* The pair of buffer descriptors locating both key and value
	   for this object, what actually gets stored. */
	struct hash_data *descriptors = NULL;
	/* Node giving the location to insert new node */
	struct rbt_node *locator = NULL;
	/* New node for the case of non-overwrite */
	struct rbt_node *mutator = NULL;

	if (isDebug(COMPONENT_HASHTABLE)
	    && isFullDebug(ht->parameter.ht_log_component)) {
		char dispkey[HASHTABLE_DISPLAY_STRLEN];
		char dispval[HASHTABLE_DISPLAY_STRLEN];
		struct display_buffer keybuf = {
			sizeof(dispkey), dispkey, dispkey};
		struct display_buffer valbuf = {
			sizeof(dispval), dispval, dispval};

		if (ht->parameter.display_key != NULL)
			ht->parameter.display_key(&keybuf, key);
		else
			dispkey[0] = '\0';

		if (ht->parameter.display_val != NULL)
			ht->parameter.display_val(&valbuf, val);
		else
			dispval[0] = '\0';

		LogFullDebug(ht->parameter.ht_log_component,
			     "Set %s Key=%p {%s} Value=%p {%s} index=%" PRIu32
			     " rbt_hash=%" PRIu64, ht->parameter.ht_name,
			     key->addr, dispkey, val->addr, dispval,
			     latch->index, latch->rbt_hash);
	}

	/* In the case of collision */
	if (latch->locator) {
		if (!overwrite) {
			rc = HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
			goto out;
		}

		descriptors = RBT_OPAQ(latch->locator);

		if (isDebug(COMPONENT_HASHTABLE)
		    && isFullDebug(ht->parameter.ht_log_component)) {
			char dispkey[HASHTABLE_DISPLAY_STRLEN];
			char dispval[HASHTABLE_DISPLAY_STRLEN];
			struct display_buffer keybuf = {
				sizeof(dispkey), dispkey, dispkey};
			struct display_buffer valbuf = {
				sizeof(dispval), dispval, dispval};

			if (ht->parameter.display_key != NULL)
				ht->parameter.display_key(&keybuf,
							  &descriptors->key);
			else
				dispkey[0] = '\0';

			if (ht->parameter.display_val != NULL)
				ht->parameter.display_val(&valbuf,
							  &descriptors->val);
			else
				dispval[0] = '\0';

			LogFullDebug(ht->parameter.ht_log_component,
				     "Set %s Key=%p {%s} Value=%p {%s} index=%"
				     PRIu32" rbt_hash=%"PRIu64" was replaced",
				     ht->parameter.ht_name,
				     descriptors->key.addr, dispkey,
				     descriptors->val.addr, dispval,
				     latch->index, latch->rbt_hash);
		}

		if (stored_key)
			*stored_key = descriptors->key;

		if (stored_val)
			*stored_val = descriptors->val;

		descriptors->key = *key;
		descriptors->val = *val;
		rc = HASHTABLE_OVERWRITTEN;
		goto out;
	}

	/* We have no collision, so go about creating and inserting a new
	   node. */

	RBT_FIND(&ht->partitions[latch->index].rbt, locator, latch->rbt_hash);

	mutator = pool_alloc(ht->node_pool);

	descriptors = pool_alloc(ht->data_pool);

	RBT_OPAQ(mutator) = descriptors;
	RBT_VALUE(mutator) = latch->rbt_hash;
	RBT_INSERT(&ht->partitions[latch->index].rbt, mutator, locator);

	descriptors->key.addr = key->addr;
	descriptors->key.len = key->len;

	descriptors->val.addr = val->addr;
	descriptors->val.len = val->len;

	/* Only in the non-overwrite case */
	++ht->partitions[latch->index].count;

	rc = HASHTABLE_SUCCESS;

 out:
	hashtable_releaselatched(ht, latch);

	if (rc != HASHTABLE_SUCCESS && isDebug(COMPONENT_HASHTABLE)
	    && isFullDebug(ht->parameter.ht_log_component))
		LogFullDebug(ht->parameter.ht_log_component,
			     "Set %s returning failure %s",
			     ht->parameter.ht_name, hash_table_err_to_str(rc));

	return rc;
}

/**
 * @brief Delete a value from the store following a previous GetLatch
 *
 * This function removes a value from the a hash store, the value
 * already having been looked up with GetLatched. In all cases, the
 * lock is retained. hashtable_getlatch must have been called with
 * may_write true.
 *
 * @param[in,out] ht      The hash store to be modified
 * @param[in]     key     A buffer descriptore locating the key to remove
 * @param[in]     latch   A pointer to a structure filled by a previous
 *                        call to hashtable_getlatched.
 * @param[out] stored_key If non-NULL, a buffer descriptor the
 *                        removed key as stored.
 * @param[out] stored_val If non-NULL, a buffer descriptor for the
 *                        removed value as stored.
 *
 */

void hashtable_deletelatched(struct hash_table *ht,
			     const struct gsh_buffdesc *key,
			     struct hash_latch *latch,
			     struct gsh_buffdesc *stored_key,
			     struct gsh_buffdesc *stored_val)
{
	/* The pair of buffer descriptors comprising the stored entry */
	struct hash_data *data;
	/* Its partition */
	struct hash_partition *partition = &ht->partitions[latch->index];

	data = RBT_OPAQ(latch->locator);

	if (isDebug(COMPONENT_HASHTABLE)
	    && isFullDebug(ht->parameter.ht_log_component)) {
		char dispkey[HASHTABLE_DISPLAY_STRLEN];
		char dispval[HASHTABLE_DISPLAY_STRLEN];
		struct display_buffer keybuf = {
			sizeof(dispkey), dispkey, dispkey};
		struct display_buffer valbuf = {
			sizeof(dispval), dispval, dispval};

		if (ht->parameter.display_key != NULL)
			ht->parameter.display_key(&keybuf, &data->key);
		else
			dispkey[0] = '\0';

		if (ht->parameter.display_val != NULL)
			ht->parameter.display_val(&valbuf, &data->val);
		else
			dispval[0] = '\0';

		LogFullDebug(ht->parameter.ht_log_component,
			     "Delete %s Key=%p {%s} Value=%p {%s} index=%"
			     PRIu32 " rbt_hash=%" PRIu64 " was removed",
			     ht->parameter.ht_name, data->key.addr, dispkey,
			     data->val.addr, dispval, latch->index,
			     latch->rbt_hash);
	}

	if (stored_key)
		*stored_key = data->key;

	if (stored_val)
		*stored_val = data->val;

	/* Clear cache */
	if (partition->cache) {
		uint32_t offset = cache_offsetof(ht, latch->rbt_hash);
		struct rbt_node *cnode = partition->cache[offset];

		if (cnode) {
#if COMPARE_BEFORE_CLEAR_CACHE
			struct hash_data *data1 = RBT_OPAQ(cnode);
			struct hash_data *data2 = RBT_OPAQ(latch->locator);

			if (ht->parameter.compare_key(&data1->key, &data2->key)
			    == 0) {
				LogFullDebug(COMPONENT_HASHTABLE_CACHE,
					     "hash clear index %d slot %" PRIu64
					     latch->index, offset);
				partition->cache[offset] = NULL;
			}
#else
			LogFullDebug(COMPONENT_HASHTABLE_CACHE,
				     "hash clear slot %d", offset);
			partition->cache[offset] = NULL;
#endif
		}
	}

	/* Now remove the entry */
	RBT_UNLINK(&partition->rbt, latch->locator);
	pool_free(ht->data_pool, data);
	pool_free(ht->node_pool, latch->locator);
	--ht->partitions[latch->index].count;

	/* Some callers re-use the latch to insert a record after this call,
	 * so reset latch locator to avoid hashtable_setlatched() using the
	 * invalid latch->locator
	 */
	latch->locator = NULL;
}

/**
 * @brief Remove and free all (key,val) couples from the hash store
 *
 * This function removes all (key,val) couples from the hashtable and
 * frees the stored data using the supplied function
 *
 * @param[in,out] ht        The hashtable to be cleared of all entries
 * @param[in]     free_func The function with which to free the contents
 *                          of each entry
 *
 * @return HASHTABLE_SUCCESS or errors
 */
hash_error_t
hashtable_delall(struct hash_table *ht,
		 int (*free_func)(struct gsh_buffdesc,
				  struct gsh_buffdesc))
{
	/* Successive partition numbers */
	uint32_t index = 0;

	for (index = 0; index < ht->parameter.index_size; index++) {
		/* The root of each successive partition */
		struct rbt_head *root = &ht->partitions[index].rbt;
		/* Pointer to node in tree for removal */
		struct rbt_node *cursor = NULL;

		PTHREAD_RWLOCK_wrlock(&ht->partitions[index].lock);

		/* Continue until there are no more entries in the red-black
		   tree */
		while ((cursor = RBT_LEFTMOST(root)) != NULL) {
			/* Pointer to the key and value descriptors
			   for each successive entry */
			struct hash_data *data = NULL;
			/* Aliased poitner to node, for freeing
			   buffers after removal from tree */
			struct rbt_node *holder = cursor;
			/* Buffer descriptor for key, as stored */
			struct gsh_buffdesc key;
			/* Buffer descriptor for value, as stored */
			struct gsh_buffdesc val;
			/* Return code from the free function.  Zero
			   on failure */
			int rc = 0;

			RBT_UNLINK(root, cursor);
			data = RBT_OPAQ(holder);

			key = data->key;
			val = data->val;

			pool_free(ht->data_pool, data);
			pool_free(ht->node_pool, holder);
			--ht->partitions[index].count;
			rc = free_func(key, val);

			if (rc == 0) {
				PTHREAD_RWLOCK_unlock(
						&ht->partitions[index].lock);
				return HASHTABLE_ERROR_DELALL_FAIL;
			}
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[index].lock);
	}

	return HASHTABLE_SUCCESS;
}

/**
 * @brief Log information about the hashtable
 *
 * This debugging function prints information about the hash table to
 * the log.
 *
 * @param[in] component The component debugging config to use.
 * @param[in] ht        The hashtable to be used.
 */

void
hashtable_log(log_components_t component, struct hash_table *ht)
{
	/* The current position in the hash table */
	struct rbt_node *it = NULL;
	/* The root of the tree currently being inspected */
	struct rbt_head *root;
	/* Buffer descriptors for the key and value */
	struct hash_data *data = NULL;
	/* String representation of the key */
	char dispkey[HASHTABLE_DISPLAY_STRLEN];
	/* String representation of the stored value */
	char dispval[HASHTABLE_DISPLAY_STRLEN];
	struct display_buffer keybuf = {sizeof(dispkey), dispkey, dispkey};
	struct display_buffer valbuf = {sizeof(dispval), dispval, dispval};
	/* Index for traversing the partitions */
	uint32_t i = 0;
	/* Running count of entries  */
	size_t nb_entries = 0;
	/* Recomputed partitionindex */
	uint32_t index = 0;
	/* Recomputed hash for Red-Black tree */
	uint64_t rbt_hash = 0;

	LogFullDebug(component, "The hash is partitioned into %d trees",
		     ht->parameter.index_size);

	for (i = 0; i < ht->parameter.index_size; i++)
		nb_entries += ht->partitions[i].count;

	LogFullDebug(component, "The hash contains %zd entries", nb_entries);

	for (i = 0; i < ht->parameter.index_size; i++) {
		root = &ht->partitions[i].rbt;
		LogFullDebug(component,
			     "The partition in position %" PRIu32
			     "contains: %u entries", i, root->rbt_num_node);
		PTHREAD_RWLOCK_rdlock(&ht->partitions[i].lock);
		RBT_LOOP(root, it) {
			data = it->rbt_opaq;

			if (ht->parameter.display_key != NULL)
				ht->parameter.display_key(&keybuf, &data->key);
			else
				dispkey[0] = '\0';

			if (ht->parameter.display_val != NULL)
				ht->parameter.display_val(&valbuf, &data->val);
			else
				dispval[0] = '\0';

			if (compute(ht, &data->key, &index, &rbt_hash)
			    != HASHTABLE_SUCCESS) {
				LogCrit(component,
					"Possible implementation error in hash_func_both");
				index = 0;
				rbt_hash = 0;
			}

			LogFullDebug(component,
				     "%s => %s; index=%" PRIu32 " rbt_hash=%"
				     PRIu64, dispkey, dispval, index, rbt_hash);
			RBT_INCREMENT(it);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
}

/**
 * @brief Set a pair (key,value) into the Hash Table
 *
 * Depending on the value of 'how', this function sets a value into
 * the hash table or tests that the hash table contains that value.
 *
 * This function is deprecated.
 *
 * @param[in,out] ht  The hashtable to test or alter
 * @param[in]     key The key to be set
 * @param[in]     val The value to be stored
 * @param[in]     how A value determining whether this is a test, a set
 *                    with overwrite, or a set without overwrite.
 *
 * @retval HASHTABLE_SUCCESS if successfull.
 */

hash_error_t
hashtable_test_and_set(struct hash_table *ht,
		       struct gsh_buffdesc *key,
		       struct gsh_buffdesc *val,
		       hash_set_how_t how)
{
	/* structure to hold retained state */
	struct hash_latch latch;
	/* Stored return code */
	hash_error_t rc = 0;

	rc = hashtable_getlatch(ht, key, NULL,
				(how != HASHTABLE_SET_HOW_TEST_ONLY), &latch);

	if ((rc != HASHTABLE_SUCCESS) &&
	    (rc != HASHTABLE_ERROR_NO_SUCH_KEY))
		return rc;

	if (how == HASHTABLE_SET_HOW_TEST_ONLY) {
		hashtable_releaselatched(ht, &latch);
		return rc;
	}

	/* No point in calling hashtable_setlatched when we know it
	   will error. */

	if ((how == HASHTABLE_SET_HOW_SET_NO_OVERWRITE)
	    && (rc == HASHTABLE_SUCCESS)) {
		hashtable_releaselatched(ht, &latch);
		return HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
	}

	rc = hashtable_setlatched(ht, key, val, &latch,
				  (how == HASHTABLE_SET_HOW_SET_OVERWRITE),
				  NULL, NULL);

	if (rc == HASHTABLE_OVERWRITTEN)
		rc = HASHTABLE_SUCCESS;

	return rc;
}

/**
 * @brief Look up a value and take a reference
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It also calls the supplied function to take
 * a reference before releasing the partition lock.  It is implemented
 * as a wrapper around hashtable_getlatched.
 *
 * @param[in]  ht      The hash store to be searched
 * @param[in]  key     A buffer descriptore locating the key to find
 * @param[out] val     A buffer descriptor locating the value found
 * @param[in]  get_ref A function to take a reference on the supplied
 *                     value
 *
 * @return HASHTABLE_SUCCESS or errors
 */
hash_error_t
hashtable_getref(hash_table_t *ht, struct gsh_buffdesc *key,
		 struct gsh_buffdesc *val,
		 void (*get_ref)(struct gsh_buffdesc *))
{
	/* structure to hold retained state */
	struct hash_latch latch;
	/* Stored return code */
	hash_error_t rc = 0;

	rc = hashtable_getlatch(ht, key, val, false, &latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		if (get_ref != NULL)
			get_ref(val);
		/* FALLTHROUGH */
	case HASHTABLE_ERROR_NO_SUCH_KEY:
		hashtable_releaselatched(ht, &latch);
		break;

	default:
		break;
	}

	return rc;
}

void hashtable_for_each(hash_table_t *ht, ht_for_each_cb_t callback, void *arg)
{
	uint32_t i;
	struct rbt_head *head_rbt;
	struct rbt_node *pn;

	/* For each bucket of the requested hashtable */
	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &ht->partitions[i].rbt;
		PTHREAD_RWLOCK_rdlock(&ht->partitions[i].lock);
		RBT_LOOP(head_rbt, pn) {
			callback(pn, arg);
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&ht->partitions[i].lock);
	}
}
/** @} */
