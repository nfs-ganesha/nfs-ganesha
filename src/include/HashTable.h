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

/**
 * @defgroup HTStructs Structures used by the hash table code
 * @{
 */

/* Forward declaration */

typedef struct hashparameter__ hash_parameter_t;

/**
 * This is the type of functions taking or releasing a reference on a
 * looked-up object.
 */

typedef int (*ref_func)(void *);

/**
 * This structure defines parameters determining the behaviour of a
 * given hash table.
 */

struct hashparameter__
{
     uint32_t index_size; /*< Number of partition trees, this MUST be a
                              prime number. */
     uint32_t alphabet_length; /*< Size of the input alphabet for
                                   the template (and other polynomial
                                   style) hash functions. */
     size_t nb_node_prealloc; /*< Number of nodes to allocate when new
                                  nodes are necessary. */
     uint32_t (*hash_func_key)(
          hash_parameter_t *param,
          hash_buffer_t *key); /*< Partition function, returns an
                                    integer from 0 to index_size - 1. */
     uint64_t (*hash_func_rbt)(
          hash_parameter_t *param,
          hash_buffer_t *key); /*< The actual hash value, dtermining
                                   location within the partition tree. */
     int (*hash_func_both)(
          hash_parameter_t *param,
          hash_buffer_t *key,
          uint32_t *hashval,
          uint64_t *rbtval); /*< Index and partition calcualtor.
                                 Returns false on failure. */
     int (*compare_key)(
          hash_buffer_t *key1,
          hash_buffer_t *key2); /*< Function to compare two keys. */
     int (*key_to_str)(
          hash_buffer_t *key,
          char *outstring); /*< Function to convert a key to a string. */
     int (*val_to_str)(
          hash_buffer_t *val,
          char *outstring); /*< Function to convert a value to a string. */
     char *name; /*< Name of this hash table. */
};

typedef uint32_t (*index_function_t) (hash_parameter_t *, hash_buffer_t *);
typedef uint64_t (*rbthash_function_t) (hash_parameter_t *, hash_buffer_t *);
typedef long (*hash_buff_comparator_t) (hash_buffer_t *, hash_buffer_t *);
typedef long (*hash_key_display_convert_func_t) (hash_buffer_t *, char *);
typedef long (*hash_val_display_convert_func_t) (hash_buffer_t *, char *);

typedef struct hashstat_op__
{
     unsigned int nb_set;   /*< Number of 'set' operations,  */
     unsigned int nb_test;  /*< Number of 'test' operations, */
     unsigned int nb_get;   /*< Number of 'get' operations,  */
     unsigned int nb_del;   /*< Number of 'del' operations,  */
} hash_stat_op_t;

typedef struct hashstat_dynamic__
{
     unsigned int nb_entries; /*< Number of entries managed in the
                                 HashTable. */
     hash_stat_op_t ok; /*< Statistics of the operation that completed
                            successfully. */
     hash_stat_op_t err; /*< Statistics of the operation that failed. */
     hash_stat_op_t notfound; /*< Statistics of the operation that
                                returned HASHTABLE_ERROR_NO_SUCH_KEY */
} hash_stat_dynamic_t;

typedef struct hashstat_computed__
{
     unsigned int min_rbt_num_node; /*< Minimum size (in number of
                                        nodes) of the rbt used. */
     unsigned int max_rbt_num_node; /*< Maximum size (in number of
                                        nodes) of the rbt used. */
     unsigned int average_rbt_num_node;  /*< Average size (in number
                                             of nodes) of the rbt
                                             used. */
} hash_stat_computed_t;

typedef struct hashstat__
{
     hash_stat_dynamic_t dynamic; /*< Dynamic statistics (computed on
                                      the fly). */
     hash_stat_computed_t computed; /*< Statistics computed when
                                        HashTable_GetStats is
                                        called. */
} hash_stat_t;

typedef struct hashtable__
{
     hash_parameter_t parameter; /*< Definition parameter for the HashTable */
     hash_stat_dynamic_t *stat_dynamic;  /*< Dynamic statistics for
                                             the HashTable. */
     struct rbt_head *array_rbt; /*< Array of red-black trees
                                     (dimensioned from index_size) */
     pthread_rwlock_t *array_lock; /*< Array locks, one per red-black
                                       tree*/
     struct prealloc_pool *node_prealloc;  /*< Pre-allocated nodes,
                                               ready to use for new
                                               entries (array of size
                                               parameter.nb_node_prealloc) */
     struct prealloc_pool *pdata_prealloc; /*< Pre-allocated pdata
                                               buffers ready to use
                                               for new entries */
} hash_table_t;

typedef enum hashtable_set_how__ {
     HASHTABLE_SET_HOW_TEST_ONLY = 1,
     HASHTABLE_SET_HOW_SET_OVERWRITE = 2,
     HASHTABLE_SET_HOW_SET_NO_OVERWRITE = 3
} hashtable_set_how_t;

/* @} */

/* How many character used to display a key or value */
#define HASHTABLE_DISPLAY_STRLEN 8192

/* Possible errors */
typedef enum hashtable_error__ {
     HASHTABLE_SUCCESS = 0,
     HASHTABLE_UNKNOWN_HASH_TYPE = 1,
     HASHTABLE_INSERT_MALLOC_ERROR = 2,
     HASHTABLE_ERROR_NO_SUCH_KEY = 3,
     HASHTABLE_ERROR_KEY_ALREADY_EXISTS = 4,
     HASHTABLE_ERROR_INVALID_ARGUMENT = 5,
     HASHTABLE_ERROR_DELALL_FAIL = 6,
     HASHTABLE_NOT_DELETED = 7
} hashtable_error_t;

const char *hash_table_err_to_str(hashtable_error_t err);

uint32_t simple_hash_func(hash_parameter_t *param,
                          hash_buffer_t *key);
uint32_t double_hash_func(hash_parameter_t *param,
                          hash_buffer_t *key);
uint64_t rbt_hash_func(hash_parameter_t *param, hash_buffer_t *key);

hash_table_t *HashTable_Init(hash_parameter_t hc);
hashtable_error_t HashTable_Test_And_Set(hash_table_t *ht,
                                         hash_buffer_t *key,
                                         hash_buffer_t *val,
                                         hashtable_set_how_t how);
static inline hashtable_error_t
HashTable_Set(hash_table_t *ht,
              hash_buffer_t *key,
              hash_buffer_t *val)
{
     return HashTable_Test_And_Set(ht, key, val,
                                   HASHTABLE_SET_HOW_SET_OVERWRITE);
}
hashtable_error_t HashTable_GetRef(hash_table_t *ht,
                                   hash_buffer_t *key,
                                   hash_buffer_t *val,
                                   void (*get_ref)(hash_buffer_t *));
hashtable_error_t HashTable_Get(hash_table_t *ht,
                                hash_buffer_t *key,
                                hash_buffer_t *val);
hashtable_error_t HashTable_Get_and_Del(hash_table_t  *ht,
                                        hash_buffer_t *key,
                                        hash_buffer_t *val,
                                        hash_buffer_t *stored_key);
hashtable_error_t HashTable_Delall(
     hash_table_t *ht,
     int (*free_func)(hash_buffer_t, hash_buffer_t));
hashtable_error_t HashTable_DelRef(hash_table_t *ht,
                                   hash_buffer_t *key,
                                   hash_buffer_t *stored_key,
                                   hash_buffer_t *stored_val,
                                   int (*put_ref)(hash_buffer_t *));
hashtable_error_t HashTable_Del(hash_table_t *ht,
                                hash_buffer_t *key,
                                hash_buffer_t *stored_key,
                                hash_buffer_t *stored_val);

void HashTable_GetStats(hash_table_t *ht,
                        hash_stat_t *hstat);
size_t HashTable_GetSize(hash_table_t *ht);
void HashTable_Log(log_components_t component, hash_table_t *ht);
void HashTable_Print(hash_table_t *ht);

/**
 * @todo ACE: HashTable.h seems a singularly inappropriate place for a
 * reference to the clientid hash table.  Come back later, remove
 * this, see what breaks, and find somewhere more appropriate to put
 * it.
 */
extern hash_table_t * ht_client_id;
#endif                          /* _HASHTABLE_H */
