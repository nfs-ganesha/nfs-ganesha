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
 * @file HashTable.h
 * @brief Header for hash functionality
 *
 * This file defines the functions and data structures for use with
 * the Ganesha red-black tree based, concurrent hash store.
 */

#ifndef _HASHTABLE_H
#define _HASHTABLE_H

#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "HashData.h"
#include "log.h"
#include "lookup3.h"
#include "stuff_alloc.h"

/**
 * @defgroup HTStructs Structures used by the hash table code
 * @{
 */

/* Forward declaration */

typedef struct hash_param hash_parameter_t;

typedef uint32_t (*index_function_t)(struct hash_param *,
                                     struct hash_buff *);
typedef uint64_t (*rbthash_function_t)(struct hash_param *,
                                       struct hash_buff *);
typedef int (*both_function_t)(struct hash_param *,
                               struct hash_buff *,
                               uint32_t *,
                               uint64_t *);
typedef int (*hash_buff_comparator_t)(struct hash_buff *,
                                       struct hash_buff *);
typedef int (*key_display_function_t)(struct hash_buff *,
                                       char *);
typedef int (*val_display_function_t)(struct hash_buff*,
                                       char *);


/**
 * This structure defines parameters determining the behaviour of a
 * given hash table.
 */


struct hash_param
{
     uint32_t index_size; /*< Number of partition trees, this MUST be a
                              prime number. */
     uint32_t alphabet_length; /*< Size of the input alphabet for
                                   the template (and other polynomial
                                   style) hash functions. */
     size_t nb_node_prealloc; /*< Number of nodes to allocate when new
                                  nodes are necessary. */
     index_function_t hash_func_key; /*< Partition function, returns an
                                         integer from 0 to (index_size
                                         - 1).  This should be
                                         something fairly simple and
                                         fast with a uniform
                                         distribution. */
     rbthash_function_t hash_func_rbt; /*< The actual hash value,
                                           termining location within
                                           the partition tree. This
                                           should be a high quality
                                           hash function such as
                                           64 bit Lookup3 or Murmur. */
     both_function_t hash_func_both; /*< Index and partition calcualtor.
                                         Returns false on
                                         failure. A single function
                                         may replace the partition
                                         and hash funcions. */
     hash_buff_comparator_t compare_key; /*< Function to compare two
                                             keys.  This function
                                             returns 0 on equality. */
     key_display_function_t key_to_str; /*< Function to convert a key
                                            to a string. */
     val_display_function_t val_to_str; /*< Function to convert a
                                            value to a string. */
     char *name; /*< Name of this hash table. */
};

typedef struct hash_stat
{
     size_t entries; /*< Number of entries in the hash table */
     size_t min_rbt_num_node; /*< Minimum size (in number of
                                  nodes) of the rbt used. */
     size_t max_rbt_num_node; /*< Maximum size (in number of
                                  nodes) of the rbt used. */
     size_t average_rbt_num_node;  /*< Average size (in number
                                       of nodes) of the rbt used. */
} hash_stat_t;

/**
 * @brief Represents an individual partition
 *
 * This structure holds the per-subtree data making up the partition i
 * a hash table.
 */

struct hash_partition
{
     size_t count; /*< Numer of entries in this partition */
     struct rbt_head rbt; /*< The red-black tree */
     pthread_rwlock_t lock; /*< Lock for this partition */
     struct prealloc_pool node_pool; /*< Pre-allocated nodes,
                                        ready to use for new
                                        entries */
     struct prealloc_pool data_pool ; /*< Pre-allocated pdata buffers
                                          ready to use for new
                                          entries */
};

typedef struct hash_table
{
     struct hash_param parameter; /*< Definitive parameter for the
                                      HashTable */
     struct hash_partition partitions[]; /*< Parameter.index_size partitions of
                                             the hash table. */
} hash_table_t;

struct hash_latch {
     uint32_t index; /*< Saved partition index */
     uint64_t rbt_hash; /*< Saved red-black hash */
     struct rbt_node *locator; /*< Saved location in the tree */
};

typedef enum hash_set_how {
     HASHTABLE_SET_HOW_TEST_ONLY = 1,
     HASHTABLE_SET_HOW_SET_OVERWRITE = 2,
     HASHTABLE_SET_HOW_SET_NO_OVERWRITE = 3
} hash_set_how_t;

/* @} */

/* How many character used to display a key or value */
#define HASHTABLE_DISPLAY_STRLEN 8192

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

struct hash_table *HashTable_Init(struct hash_param *hparam);
hash_error_t HashTable_GetLatch(struct hash_table *ht,
                                struct hash_buff *key,
                                struct hash_buff *val,
                                int may_write,
                                struct hash_latch *latch);
hash_error_t HashTable_ReleaseLatched(struct hash_table *ht,
                                      struct hash_latch *latch);
hash_error_t HashTable_SetLatched(struct hash_table *ht,
                                  struct hash_buff *key,
                                  struct hash_buff *val,
                                  struct hash_latch *latch,
                                  int overwrite,
                                  struct hash_buff *stored_key,
                                  struct hash_buff *stored_val);
hash_error_t HashTable_DeleteLatched(struct hash_table *ht,
                                     struct hash_buff *key,
                                     struct hash_latch *latch,
                                     struct hash_buff *stored_key,
                                     struct hash_buff *stored_val);
hash_error_t HashTable_Delall(struct hash_table *ht,
                              int (*free_func)(struct hash_buff,
                                               struct hash_buff));

void HashTable_GetStats(struct hash_table *ht,
                        struct hash_stat *hstat);
size_t HashTable_GetSize(struct hash_table *ht);
void HashTable_Log(log_components_t component, struct hash_table *ht);

/* These are very simple wrappers around the primitives */

/**
 *
 * @brief Look up a value
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It is implemented as a wrapper around
 * the HashTable_GetLatched function.
 *
 * @param ht [in] The hash store to be searched
 * @param key [in] A buffer descriptore locating the key to find
 * @param val [out] A buffer descriptor locating the value found
 *
 * @return HASHTABLE_SUCCESS or errors
 */

static inline hash_error_t
HashTable_Get(struct hash_table *ht,
              struct hash_buff *key,
              struct hash_buff *val)
{
     return HashTable_GetLatch(ht, key, val, FALSE, NULL);
} /* HashTable_Get */

/**
 *
 * @brief Set a pair (key,value) into the Hash Table
 *
 * This function sets a value into the hash table with no overwrite.
 *
 * The previous version of this function would overwrite, but having
 * overwrite as the only value for a function that doesn't return the
 * original buffers is a bad idea and can lead to leaks.
 *
 * @param ht [in] The hashtable to test or alter
 * @param key [in] The key to be set
 * @param val [in] The value to be stored
 *
 * @retval HASHTABLE_SUCCESS if successfull
 * @retval HASHTABLE_KEY_ALREADY_EXISTS if the key already exists
 */

static inline hash_error_t
HashTable_Set(struct hash_table *ht,
              struct hash_buff *key,
              struct hash_buff *val)
{
     /* structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;

     rc = HashTable_GetLatch(ht, key, NULL,
                             TRUE,
                             &latch);

     if ((rc != HASHTABLE_SUCCESS) &&
         (rc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          return rc;
     }

     rc = HashTable_SetLatched(ht,
                               key,
                               val,
                               &latch,
                               FALSE,
                               NULL,
                               NULL);

     return rc;
} /* HashTable_Set */


/**
 *
 * @brief Remove an entry from the hash table
 *
 * This function deletes an entry from the hash table.
 *
 * @param ht [in] The hashtable to be modified
 * @param key [in] The key corresponding to the entry to delete
 * @param stored_key [out] If non-NULL, a buffer descriptor specifying
 *                         the key as stored in the hash table
 * @param stored_val [out] If non-NULL, a buffer descriptor specifying
 *                         the key as stored in the hash table
 *
 * @retval HASHTABLE_SUCCESS on deletion
 */

static inline hash_error_t
HashTable_Del(struct hash_table *ht,
              struct hash_buff *key,
              struct hash_buff *stored_key,
              struct hash_buff *stored_val)
{
     /* Structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;

     rc = HashTable_GetLatch(ht, key, NULL,
                             TRUE, &latch);

     switch (rc) {
     case HASHTABLE_SUCCESS:
          return HashTable_DeleteLatched(ht,
                                         key,
                                         &latch,
                                         stored_key,
                                         stored_val);

     case HASHTABLE_ERROR_NO_SUCH_KEY:
          HashTable_ReleaseLatched(ht, &latch);
     default:
          return rc;
     }
}

/* These are the prototypes for large wrappers implementing more
   complex semantics on top of the primitives. */

hash_error_t HashTable_Test_And_Set(struct hash_table *ht,
                                    struct hash_buff *key,
                                    struct hash_buff *val,
                                    enum hash_set_how how);
hash_error_t HashTable_GetRef(struct hash_table *ht,
                              struct hash_buff *key,
                              struct hash_buff *val,
                              void (*get_ref)(struct hash_buff *));

hash_error_t HashTable_Get_and_Del(struct hash_table  *ht,
                                   struct hash_buff *key,
                                   struct hash_buff *val,
                                   struct hash_buff *stored_key);
hash_error_t HashTable_DelRef(struct hash_table *ht,
                              struct hash_buff *key,
                              struct hash_buff *stored_key,
                              struct hash_buff *stored_val,
                              int (*put_ref)(struct hash_buff *));
hash_error_t HashTable_DelSafe(hash_table_t *ht,
                               hash_buffer_t *key,
                               hash_buffer_t *val);

/**
 * @todo ACE: HashTable.h seems a singularly inappropriate place for a
 * reference to the clientid hash table.  Come back later, remove
 * this, see what breaks, and find somewhere more appropriate to put
 * it.
 */
extern hash_table_t * ht_client_id;
#endif                          /* _HASHTABLE_H */
