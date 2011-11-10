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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    HashTable.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:36:19 $
 * \version $Revision: 1.35 $
 * \brief   Gestion des tables de hachage a base de Red/Black Trees.
 *
 * HashTable.h : gestion d'une table de hachage
 *
 *
 */

#ifndef _HASHTABLE_H
#define _HASHTABLE_H

#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashData.h"
#include "log_macros.h"
#include "lookup3.h"

/**
 * @defgroup HashTableStructs
 * 
 * @{
 */

typedef struct hashparameter__ *p_hash_parameter_t;

typedef int (*ref_func)(void *);

typedef struct hashparameter__
{
  unsigned int index_size;                                    /**< Number of rbtree managed, this MUST be a prime number. */
  unsigned int alphabet_length;                               /**< Number of characters used to write the buffer (polynomial approach). */
  unsigned int nb_node_prealloc;                              /**< Number of node to allocated when new nodes are necessary. */
  unsigned long (*hash_func_key) (p_hash_parameter_t, hash_buffer_t *);     /**< Hashing function, returns an integer from 0 to index_size - 1 . */
  unsigned long (*hash_func_rbt) (p_hash_parameter_t, hash_buffer_t *);     /**< Rbt value calculator (for rbt management). */
  unsigned int (*hash_func_both) (p_hash_parameter_t, hash_buffer_t *,
                                  uint32_t * phashval, uint32_t * prbtval ); /**< Rbt + hash value calculator (for rbt management). */
  int (*compare_key) (hash_buffer_t *, hash_buffer_t *);                     /**< Function used to compare two keys together. */
  int (*key_to_str) (hash_buffer_t *, char *);                                  /**< Function used to convert a key to a string. */
  int (*val_to_str) (hash_buffer_t *, char *);                                  /**< Function used to convert a value to a string. */
  char *name;                                                                   /**< Name of this hash table. */
} hash_parameter_t;

typedef unsigned long (*hash_function_t) (hash_parameter_t *, hash_buffer_t *);
typedef long (*hash_buff_comparator_t) (hash_buffer_t *, hash_buffer_t *);
typedef long (*hash_key_display_convert_func_t) (hash_buffer_t *, char *);
typedef long (*hash_val_display_convert_func_t) (hash_buffer_t *, char *);

typedef struct hashstat_op__
{
  unsigned int nb_set;   /**< Number of 'set' operations,  */
  unsigned int nb_test;  /**< Number of 'test' operations, */
  unsigned int nb_get;   /**< Number of 'get' operations,  */
  unsigned int nb_del;   /**< Number of 'del' operations,  */
} hash_stat_op_t;

typedef struct hashstat_dynamic__
{
  unsigned int nb_entries;   /**< Number of entries managed in the HashTable. */
  hash_stat_op_t ok;         /**< Statistics of the operation that completed successfully. */
  hash_stat_op_t err;        /**< Statistics of the operation that failed. */
  hash_stat_op_t notfound;   /**< Statistics of the operation that returned HASHTABLE_ERROR_NO_SUCH_KEY */
} hash_stat_dynamic_t;

typedef struct hashstat_computed__
{
  unsigned int min_rbt_num_node;      /**< Minimum size (in number of node) of the rbt used. */
  unsigned int max_rbt_num_node;      /**< Maximum size (in number of node) of the rbt used. */
  unsigned int average_rbt_num_node;  /**< Average size (in number of node) of the rbt used. */
} hash_stat_computed_t;

typedef struct hashstat__
{
  hash_stat_dynamic_t dynamic;    /**< Dynamic statistics (computed on the fly). */
  hash_stat_computed_t computed;  /**< Statistics computed when HashTable_GetStats is called. */
} hash_stat_t;

typedef struct hashtable__
{
  hash_parameter_t parameter;           /**< Definition parameter for the HashTable */
  hash_stat_dynamic_t *stat_dynamic;    /**< Dynamic statistics for the HashTable. */
  struct rbt_head *array_rbt;           /**< Array of reb-black tree (of size parameter.index_size) */
  rw_lock_t *array_lock;                /**< Array of rw-locks for MT-safe management */
  struct prealloc_pool *node_prealloc;  /**< Pre-allocated nodes, ready to use for new entries (array of size parameter.nb_node_prealloc) */
  struct prealloc_pool *pdata_prealloc; /**< Pre-allocated pdata buffers  ready to use for new entries */
} hash_table_t;

typedef enum hashtable_set_how__
{ HASHTABLE_SET_HOW_TEST_ONLY = 1,
  HASHTABLE_SET_HOW_SET_OVERWRITE = 2,
  HASHTABLE_SET_HOW_SET_NO_OVERWRITE = 3
} hashtable_set_how_t;

/* @} */

/* How many character used to display a key or value */
#define HASHTABLE_DISPLAY_STRLEN 8192

/* Possible errors */
#define HASHTABLE_SUCCESS                  0
#define HASHTABLE_UNKNOWN_HASH_TYPE        1
#define HASHTABLE_INSERT_MALLOC_ERROR      2
#define HASHTABLE_ERROR_NO_SUCH_KEY        3
#define HASHTABLE_ERROR_KEY_ALREADY_EXISTS 4
#define HASHTABLE_ERROR_INVALID_ARGUMENT   5
#define HASHTABLE_ERROR_DELALL_FAIL        6
#define HASHTABLE_NOT_DELETED              7

const char *hash_table_err_to_str(int err);
unsigned long double_hash_func(hash_parameter_t * hc, hash_buffer_t * buffclef);
hash_table_t *HashTable_Init(hash_parameter_t hc);
int HashTable_Test_And_Set(hash_table_t * ht, hash_buffer_t * buffkey,
                           hash_buffer_t * buffval, hashtable_set_how_t how);
int HashTable_Get(hash_table_t * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval);
int HashTable_Del(hash_table_t * ht, hash_buffer_t * buffkey,
                  hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata);
int HashTable_Delall(hash_table_t * ht,
		     int (*free_func)(hash_buffer_t, hash_buffer_t) );
#define HashTable_Set( ht, buffkey, buffval ) HashTable_Test_And_Set( ht, buffkey, buffval, HASHTABLE_SET_HOW_SET_OVERWRITE )
void HashTable_GetStats(hash_table_t * ht, hash_stat_t * hstat);
void HashTable_Log(log_components_t component, hash_table_t * ht);
void HashTable_Print(hash_table_t * ht);
unsigned int HashTable_GetSize(hash_table_t * ht);

/* The following function allows an atomic fetch hash only once,
 * If the entry is found, it is removed.
 */
int HashTable_Get_and_Del(hash_table_t  * ht,
                          hash_buffer_t * buffkey,
                          hash_buffer_t * buffval,
                          hash_buffer_t * buff_used_key);

/*
 * The following two functions provide reference counting management while the
 * hash list mutex is held. put_ref should return 0 if the ref count was
 * decremented to 0 (otherwise it can return whatever).
 */
int HashTable_GetRef(hash_table_t * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval,
                     void (*get_ref)(hash_buffer_t *) );
int HashTable_DelRef(hash_table_t * ht, hash_buffer_t * buffkey,
                     hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata,
                     int (*put_ref)(hash_buffer_t *) );

#endif                          /* _HASHTABLE_H */
