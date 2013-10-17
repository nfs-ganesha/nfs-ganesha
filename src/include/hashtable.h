/*
 *
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
 * @defgroup hashtable A non-intrusive, partitioned hash-keyed tree
 * @{
 */

/**
 * @file HashTable.h
 * @brief Header for hash functionality
 *
 * This file declares the functions and data structures for use with
 * the Ganesha red-black tree based, concurrent hash store.
 */

#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stdbool.h>
#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "log.h"
#include "abstract_mem.h"
#include "ganesha_types.h"

/**
 * @brief A pair of buffer descriptors
 *
 * This is used internally to represent a single hash datum within the
 * table.
 */

struct hash_data {
	struct gsh_buffdesc key; /*< The lookup key */
	struct gsh_buffdesc val; /*< The stored value */
};

/* Forward declaration */

typedef struct hash_param hash_parameter_t;

typedef uint32_t(*index_function_t)(struct hash_param *,
				    struct gsh_buffdesc *);
typedef uint64_t(*rbthash_function_t)(struct hash_param *,
				      struct gsh_buffdesc *);
typedef int (*both_function_t)(struct hash_param *, struct gsh_buffdesc *,
			       uint32_t *, uint64_t *);
typedef int (*hash_comparator_t)(struct gsh_buffdesc *, struct gsh_buffdesc *);
typedef int (*key_display_function_t)(struct gsh_buffdesc *, char *);
typedef int (*val_display_function_t)(struct gsh_buffdesc *, char *);

#define HT_FLAG_NONE 0x0000	/*< Null hash table flags */
#define HT_FLAG_CACHE 0x0001	/*< Indicates that caching should be
				   enabled */

/**
 * @brief Hash parameters
 *
 * This structure defines parameters determining the behaviour of a
 * given hash table.
 */

struct hash_param {
	uint32_t flags; /*< Create flags */
	uint32_t cache_entry_count; /*< 2^10 <= Power of 2 <= 2^15 */
	uint32_t index_size;	/*< Number of partition trees, this MUST
				   be a prime number. */
	index_function_t hash_func_key;	/*< Partition function,
					   returns an integer from 0
					   to (index_size - 1).  This
					   should be something fairly
					   simple and fast with a
					   uniform distribution. */
	rbthash_function_t hash_func_rbt; /*< The actual hash value,
					      termining location
					      within the partition
					      tree. This should be a
					      high quality hash
					      function such as 64 bit
					      Lookup3 or Murmur. */
	both_function_t hash_func_both;	/*< Index and partition
					   calcualtor.  Returns false
					   on failure. A single
					   function may replace the
					   partition and hash
					   funcions. */
	hash_comparator_t compare_key;/*< Function to compare two
					  keys.  This function
					  returns 0 on equality. */
	key_display_function_t key_to_str; /*< Function to convert a key
					       to a string. */
	val_display_function_t val_to_str; /*< Function to convert a
					       value to a string. */
	char *ht_name; /*< Name of this hash table. */
	log_components_t ht_log_component; /*< Log component to use for this
					       hash table */
};

/**
 * @brief Hash stats
 *
 * This structure defines various statistics for a hash table.
 */

typedef struct hash_stat {
	size_t entries; /*< Number of entries in the hash table */
	size_t min_rbt_num_node; /*< Minimum size (in number of nodes) of the
				     rbt used. */
	size_t max_rbt_num_node; /*< Maximum size (in number of nodes) of the
				     rbt used. */
	size_t average_rbt_num_node; /*< Average size (in number of nodes) of
				       the rbt used. */
} hash_stat_t;

/**
 * @brief Represents an individual partition
 *
 * This structure holds the per-subtree data making up each partition in
 * a hash table.
 */

struct hash_partition {
	size_t count; /*< Numer of entries in this partition */
	struct rbt_head rbt; /*< The red-black tree */
	pthread_rwlock_t lock; /*< Lock for this partition */
	struct rbt_node **cache; /*< Expected entry cache */
};

/**
 * @brief A hash table
 *
 * This structure defines an entire hash table.
 */

typedef struct hash_table {
	struct hash_param parameter; /*< Definitive parameter for the
					 HashTable */
	pool_t *node_pool; /*< Pool of RBT nodes */
	pool_t *data_pool; /*< Pool of buffer pairs */
	struct hash_partition partitions[]; /*< Parameter.index_size
						partitions of the hash
						table. */
} hash_table_t;

/**
 * @brief A 'latching' lock
 *
 * This structure defines a 'latching' lock for subsequent operations
 * on a hash table after an initial lookup.
 */

struct hash_latch {
	uint32_t index;	/*< Saved partition index */
	uint64_t rbt_hash; /*< Saved red-black hash */
	struct rbt_node *locator; /*< Saved location in the tree */
};

typedef enum hash_set_how {
	HASHTABLE_SET_HOW_TEST_ONLY = 1,
	HASHTABLE_SET_HOW_SET_OVERWRITE = 2,
	HASHTABLE_SET_HOW_SET_NO_OVERWRITE = 3
} hash_set_how_t;

/* How many character used to display a key or value */
static const size_t HASHTABLE_DISPLAY_STRLEN = 8192;

/* Possible errors */
typedef enum hash_error {
	HASHTABLE_SUCCESS = 0,
	HASHTABLE_UNKNOWN_HASH_TYPE = 1,
	HASHTABLE_INSERT_MALLOC_ERROR = 2,
	HASHTABLE_ERROR_NO_SUCH_KEY = 3,
	HASHTABLE_ERROR_KEY_ALREADY_EXISTS = 4,
	HASHTABLE_ERROR_INVALID_ARGUMENT = 5,
	HASHTABLE_ERROR_DELALL_FAIL = 6,
	HASHTABLE_NOT_DELETED = 7,
	HASHTABLE_OVERWRITTEN = 8
} hash_error_t;

const char *hash_table_err_to_str(hash_error_t err);

/* These are the primitives of the hash table */

struct hash_table *hashtable_init(struct hash_param *);
hash_error_t hashtable_destroy(struct hash_table *,
			       int (*)(struct gsh_buffdesc,
				       struct gsh_buffdesc));
hash_error_t hashtable_getlatch(struct hash_table *,
				const struct gsh_buffdesc *,
				struct gsh_buffdesc *, bool ,
				struct hash_latch *);
void hashtable_releaselatched(struct hash_table *, struct hash_latch *);
hash_error_t hashtable_setlatched(struct hash_table *, struct gsh_buffdesc *,
				  struct gsh_buffdesc *, struct hash_latch *,
				  int, struct gsh_buffdesc *,
				  struct gsh_buffdesc *);
hash_error_t hashtable_deletelatched(struct hash_table *,
				     const struct gsh_buffdesc *,
				     struct hash_latch *,
				     struct gsh_buffdesc *,
				     struct gsh_buffdesc *);
hash_error_t hashtable_delall(struct hash_table *,
			      int (*)(struct gsh_buffdesc,
				      struct gsh_buffdesc));

void hashtable_log(log_components_t, struct hash_table *);

/* These are very simple wrappers around the primitives */

/**
 * @brief Look up a value
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It is implemented as a wrapper around
 * the hashtable_getlatched function.
 *
 * @param[in]  ht  The hash store to be searched
 * @param[in]  key A buffer descriptor locating the key to find
 * @param[out] val A buffer descriptor locating the value found
 *
 * @return Same possibilities as HahTable_GetLatch
 */

static inline hash_error_t HashTable_Get(struct hash_table *ht,
					 const struct gsh_buffdesc *key,
					 struct gsh_buffdesc *val)
{
	return hashtable_getlatch(ht, key, val, false, NULL);
}

/**
 * @brief Set a pair (key,value) into the Hash Table
 *
 * This function sets a value into the hash table with no overwrite.
 *
 * The previous version of this function would overwrite, but having
 * overwrite as the only value for a function that doesn't return the
 * original buffers is a bad idea and can lead to leaks.
 *
 * @param[in,out] ht  The hashtable to test or alter
 * @param[in]     key The key to be set
 * @param[in]     val The value to be stored
 *
 * @retval HASHTABLE_SUCCESS if successfull
 * @retval HASHTABLE_KEY_ALREADY_EXISTS if the key already exists
 */

static inline hash_error_t HashTable_Set(struct hash_table *ht,
					 struct gsh_buffdesc *key,
					 struct gsh_buffdesc *val)
{
	/* structure to hold retained state */
	struct hash_latch latch;
	/* Stored return code */
	hash_error_t rc = HASHTABLE_SUCCESS;

	rc = hashtable_getlatch(ht, key, NULL, true, &latch);

	if ((rc != HASHTABLE_SUCCESS) &&
	    (rc != HASHTABLE_ERROR_NO_SUCH_KEY))
		return rc;

	rc = hashtable_setlatched(ht, key, val, &latch, false, NULL, NULL);

	return rc;
}				/* HashTable_Set */

/**
 * @brief Remove an entry from the hash table
 *
 * This function deletes an entry from the hash table.
 *
 * @param[in,out] ht         The hashtable to be modified
 * @param[in]     key        The key corresponding to the entry to delete
 * @param[out]    stored_key If non-NULL, a buffer descriptor
 *                           specifying the key as stored in the hash table
 * @param[out]    stored_val If non-NULL, a buffer descriptor
 *                           specifying the key as stored in the hash table
 *
 * @retval HASHTABLE_SUCCESS on deletion
 */
static inline hash_error_t HashTable_Del(struct hash_table *ht,
					 const struct gsh_buffdesc *key,
					 struct gsh_buffdesc *stored_key,
					 struct gsh_buffdesc *stored_val)
{
	/* Structure to hold retained state */
	struct hash_latch latch;
	/* Stored return code */
	hash_error_t rc = HASHTABLE_SUCCESS;

	rc = hashtable_getlatch(ht, key, NULL, true, &latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		return hashtable_deletelatched(ht, key, &latch, stored_key,
					       stored_val);

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		hashtable_releaselatched(ht, &latch);
	default:
		return rc;
	}
}

/* These are the prototypes for large wrappers implementing more
   complex semantics on top of the primitives. */

hash_error_t hashtable_test_and_set(struct hash_table *,
				    struct gsh_buffdesc *,
				    struct gsh_buffdesc *,
				    enum hash_set_how);
hash_error_t hashtable_getref(struct hash_table *, struct gsh_buffdesc *,
			      struct gsh_buffdesc *,
			      void (*)(struct gsh_buffdesc *));
hash_error_t hashtable_delref(struct hash_table *, struct gsh_buffdesc *,
			      struct gsh_buffdesc *,
			      struct gsh_buffdesc *,
			      int (*)(struct gsh_buffdesc *));
hash_error_t hashtable_delsafe(hash_table_t *, struct gsh_buffdesc *,
			       struct gsh_buffdesc *);

/** @} */

#endif /* HASHTABLE_H */
