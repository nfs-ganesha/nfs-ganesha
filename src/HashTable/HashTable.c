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
 * @file  HashTable.c
 * @brief Implement an RBTree-based partitioend hash lookup
 *
 * This file implements a partitioned, tree-based, concurrent
 * hash-lookup structure.  For every key, two values are derived that
 * determine its location within the structure: an index, which
 * determines which of the partitions (each containing a tree and each
 * separately locked), and a hash which acts as the key within an
 * individual Red-Black Tree.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashTable.h"
#include "stuff_alloc.h"
#include "log.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/**
 * @defgroup HTInternals Internal implementation details of the hash table
 *@{
 */

const char *
hash_table_err_to_str(hashtable_error_t err)
{
     switch(err) {
     case HASHTABLE_SUCCESS:
          return "HASHTABLE_SUCCESS";
     case HASHTABLE_UNKNOWN_HASH_TYPE:
          return "HASHTABLE_UNKNOWN_HASH_TYPE";
     case HASHTABLE_INSERT_MALLOC_ERROR:
          return "HASHTABLE_INSERT_MALLOC_ERROR";
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
     default:
          return "UNKNOWN HASH TABLE ERROR";
     }
}

/**
 *
 * @brief A template hash function that treats the key as a polynomial
 *
 * We wish to hash a string written with ht->alphabet_length
 * different characters. We turn the string into a hash by computing
 *
 * \f[
 *     str_0 +
 *     str_1 \cdot ht\rightarrow alphabet_length +
 *     str_2 \cdot ht\rightarrow alphabet_length^2 +
 *     \cdots +
 *     str_n \cdot ht\rightarrow alphabet_length^n
 * \f]
 *
 * Then we take the modulus by index_size. This size has to be a prime
 * integer for performance reason The polynomial is computed with
 * Horner's method.
 *
 * @param param [in] The parameters for thish hash table
 * @param key [in] The key whose hash we wish to take
 *
 * @return The computed value
 *
 * @see double_hash_func
 */

uint32_t
simple_hash_func(hash_parameter_t *param, hash_buffer_t *key)
{
     /* The index ranging over the bytes in the key */
     uint32_t i = 0;
     /* The accumulator into which the hash is constructed */
     uint32_t h = 0;
     /* The current byte in the key (as an integer, to match h) */
     uint32_t c = 0;
     /* An ipointer aliasing the key */
     char *sobj = key->pdata;

     for (i = 0; i < key->len; i++) {
          c = (uint32_t) sobj[i];
          h = (param->alphabet_length * h + c) % param->index_size;
     }

     return h;
} /* simple_hash_func */

/**
 *
 * @brief Doubly hash, based on another hash function
 *
 * This functions uses the hash function contained in hparam to
 * compute a first hash value, then uses it to compute a second value
 * like

 * \f[
 *    h = (firsthash + (8 - (firsthash \mod 8))) \mod index_size
 * \f]

 * This operation just changes the last 3 bits, but it can be
 * demonstrated that this produced a more efficient and better
 * balanced hash function (See 'Algorithm in C', Robert Sedjewick for
 * more detail on this).
 *
 * @param param [in] The parameters defining the hash table
 * @param key [in] The key to be hashed
 *
 * @return the hash value
 *
 * @see double_hash_func
 */

uint32_t
double_hash_func(hash_parameter_t *param,
                 hash_buffer_t *key)
{
     /* The hash created returned by the first function */
     uint32_t firsthash = 0;
     /* Our modified hash */
     uint32_t h = 0;
     /* The first hashing function */
     index_function_t hashfunc = simple_hash_func;

     /* first, we find the intial value for simple hashing */
     firsthash = hashfunc(param, key);

     /* A second value is computed a second a value is added to the
        first one, then the modulo is kept For the second hash, we
        choose to change the last 3 bit, which is usually a good
        compromise */

     h = (firsthash + (8 - (firsthash % 8)))
          % param->index_size;

     return h;
}                               /* double_hash_func */

/**
 * @brief A hash function for inside the Red-black trees
 *
 * This library uses red-black trees to store data. RB trees use key
 * data too. The hash function has to be different than the one used
 * to find the RB Tree; if not, all the entries in the tree would have
 * the same hash value which would lead to a very unbalanced tree.
 *
 * This is not a very good hash and should not be used.  It is
 * currently required for some testing code, however.
 *
 * @param param [in] The parameters for this hashtable
 * @param key [in] The key to be hashed
 *
 * @return the hash value
 *
 */

uint64_t
rbt_hash_func(hash_parameter_t *param, hash_buffer_t *key)
{
     return atoi(key->pdata) + 3;
} /* rbt_hash_func */

/**
 * @brief Locate a key in the hash table as an RBT node
 *
 * This function funds a key within the appropriate Red-Black tree
 * as part of other operations.  The hash values are computed in the
 * caller and passed in, to avoid duplicated work.
 *
 * @param ht [in] The hashtable to be used
 * @param key [in] The key to look up
 * @param index [in] Index into RBT array
 * @param rbthash [in] Hash in red-black tree
 * @param node [out] On success, the found node, NULL otherwise
 *
 * @retval HASHTABLE_SUCCESS if successfull
 * @retval HASHTABLE_NO_SUCH_KEY if key was not found
 */
static hashtable_error_t
Key_Locate(hash_table_t *ht,
           hash_buffer_t *key,
           uint32_t index,
           uint64_t rbthash,
           struct rbt_node **node)
{
     /* The root of the red black tree matching this index */
     struct rbt_head *root = NULL;
     /* A pair of buffer descriptors locating key and value for this
        entry*/
     hash_data_t *data = NULL;
     /* The node in the red-black tree currently being traversed */
     struct rbt_node *cursor = NULL;
     /* TRUE if we have located the key */
     int found = FALSE;

     /* Sanity check */
     if (ht == NULL || key == NULL || node == NULL) {
          LogFullDebug(COMPONENT_HASHTABLE,
                       "Returning HASHTABLE_ERROR_INVALID_ARGUMENT");
          return HASHTABLE_ERROR_INVALID_ARGUMENT;
     }

     *node = NULL;

     /* Find the root of the Red-Black Tree in the array of tree
        partitions. */
     root = &(ht->array_rbt[index]);

     /* I get the node with this value that is located on the left
        (first with this value in the rbtree) */
     RBT_FIND_LEFT(root, cursor, rbthash);

     /* Find was successfull ? */
     if (cursor == NULL) {
          LogFullDebug(COMPONENT_HASHTABLE,
                       "Key not found: rbthash = %"PRIu64,
                       rbthash);
          return HASHTABLE_ERROR_NO_SUCH_KEY;
     }

     /* For each entry with this value, compare the key value */
     while ((cursor != NULL) && (RBT_VALUE(cursor) == rbthash)) {
          data = RBT_OPAQ(cursor);
          /* Verify the key value; this function returns 0 if keys are
             indentical */
          if (!ht->parameter.compare_key(key, &(data->buffkey))) {
               found = TRUE;
               break; /* exit the while loop */
          }
          RBT_INCREMENT(cursor);
     }

     /* We didn't find anything */
     if (!found) {
          LogFullDebug(COMPONENT_HASHTABLE,
                       "Returning HASHTABLE_ERROR_NO_SUCH_KEY.");
          return HASHTABLE_ERROR_NO_SUCH_KEY;
     }

     /* Key was found */
     *node = cursor;

     return HASHTABLE_SUCCESS;
} /* Key_Locate */

/**
 * @brief Compute the values to search a hash store
 *
 * This function computes the index and RBT hash values for the
 * specified key.
 *
 * @param ht [in] The hash table whose parameters determine computation
 * @param key [in] The key from which to compute the values
 * @param index [out] The partition index
 * @param rbt_hash [out] The hash in the Red-Black tree
 */

static inline hashtable_error_t
compute(hash_table_t *ht, hash_buffer_t *key, uint32_t *index,
        uint64_t *rbt_hash) {

     /* Compute the partition index and red-black tree hash */
     if (ht->parameter.hash_func_both) {
          if (!(*(ht->parameter.hash_func_both))(&ht->parameter,
                                                 key, index,
                                                 rbt_hash))
               return HASHTABLE_ERROR_INVALID_ARGUMENT;
     } else {
          *index = (*(ht->parameter.hash_func_key))(&ht->parameter,
                                                   key);
          *rbt_hash = (*(ht->parameter.hash_func_rbt))(&ht->parameter,
                                                      key);
     }

     return HASHTABLE_SUCCESS;
}

/*}@ */

/**
 * @defgroup HTxported Functions in the public Hash Table interface
 *@{
 */

/**
 *
 * @brief Initialize a new hash table
 *
 * This function initializes and allocates storage for a hash table.
 *
 * @param hparam A structure of type hash_parameter_t which contains the values used to init the hash table.
 *
 * @return Pointer to the new hash table, NULL on failure
 *
 */

hash_table_t *
HashTable_Init(hash_parameter_t hparam)
{
     /* The hash table being constructed */
     hash_table_t *ht = NULL;
     /* The index for initializing each partition */
     unsigned int i = 0;
     /* A name to be used in creating the memory pools */
     char *name __attribute__((unused)) = "Un-named";
     /* Read-Write Lock attributes, to prevent write starvation under
        GLIBC*/
     pthread_rwlockattr_t rwlockattr;

     if (hparam.name != NULL)
          name = hparam.name;

     /* Sanity check */
     if((ht = Mem_Alloc_Label(sizeof(hash_table_t),
                              "hash_table_t")) == NULL)
          return NULL;

     /* we have to keep the discriminant values */
     ht->parameter = hparam;

     if (pthread_rwlockattr_init(&rwlockattr) != 0) {
          Mem_Free(ht);
          return NULL;
     }

     /* At some point factor this out into the OS directory.  it is
        necessary to prevent writer starvation under GLIBC. */
#ifdef GLIBC
     if ((pthread_rwlockattr_setkind_np(
               &rwlockattrs,
               PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP)) != 0) {
          Mem_Free(ht);
          LogCrit(COMPONENT_HASHTABLE,
                  "Unable to set writer-preference on lock attribute.");
          return NULL;
     }
#endif /* GLIBC */

     /* Initialization of the node array */
     if ((ht->array_rbt
          = Mem_Alloc_Label(sizeof(struct rbt_head) *
                            hparam.index_size,
                            "rbt_head")) == NULL) {
          Mem_Free(ht);
          return NULL;
     }

     memset(ht->array_rbt, 0, sizeof(struct rbt_head) * hparam.index_size);

     /* Initialization of the stat array */
     if ((ht->stat_dynamic
          = Mem_Alloc_Label(sizeof(hash_stat_dynamic_t) *
                            hparam.index_size,
                            "hash_stat_dynamic_t")) == NULL) {
          Mem_Free(ht->array_rbt);
          Mem_Free(ht) ;
          return NULL;
     }

     /* Init the stats */
     memset(ht->stat_dynamic, 0,
            sizeof(hash_stat_dynamic_t) * hparam.index_size);

     /* Initialization of the semaphores array */
     if ((ht->array_lock =
          Mem_Alloc_Label(sizeof(pthread_rwlock_t) * hparam.index_size,
                          "pthread_rwlock_t")) == NULL) {
          Mem_Free(ht->array_rbt);
          Mem_Free(ht->stat_dynamic);
          Mem_Free(ht);
          return NULL;
     }

     memset(ht->array_lock, 0, sizeof(pthread_rwlock_t) * hparam.index_size);

     /* Initialize the array of pre-allocated node */
     if((ht->node_prealloc =
         Mem_Calloc_Label(hparam.index_size,
                          sizeof(struct prealloc_pool),
                          "rbt_node_pool")) == NULL) {
          Mem_Free(ht->array_rbt);
          Mem_Free(ht->stat_dynamic);
          Mem_Free(ht->array_lock);
          Mem_Free(ht);
          return NULL;
     }

     memset(ht->node_prealloc, 0,
            sizeof(prealloc_pool) * hparam.index_size);

     if ((ht->pdata_prealloc
          = Mem_Calloc_Label(hparam.index_size,
                             sizeof(struct prealloc_pool),
                             "hash_data_pool")) == NULL) {
          Mem_Free(ht->array_rbt);
          Mem_Free(ht->stat_dynamic);
          Mem_Free(ht->array_lock);
          Mem_Free(ht->node_prealloc);
          Mem_Free(ht);
          return NULL;
     }

     memset(ht->pdata_prealloc, 0, sizeof(prealloc_pool) * hparam.index_size);

     for(i = 0; i < hparam.index_size; i++) {
          LogFullDebug(COMPONENT_MEMALLOC,
                       "Allocating %zd new nodes",
                       hparam.nb_node_prealloc);

          /* Allocate a group of nodes to be managed by the RB Tree. */
          MakePool(&ht->node_prealloc[i],
                   hparam.nb_node_prealloc,
                   rbt_node_t, NULL, NULL);
          NamePool(&ht->node_prealloc[i],
                   "%s Hash RBT Nodes index %d",
                   name, i);
          if(!IsPoolPreallocated(&ht->node_prealloc[i]))
               return NULL;

          /* Allocate a group of hash_data_t to be managed as RBT_OPAQ
             values. */
          MakePool(&ht->pdata_prealloc[i],
                   hparam.nb_node_prealloc, hash_data_t, NULL, NULL);
          NamePool(&ht->pdata_prealloc[i], "%s Hash Data Nodes index %d",
                   name, i);
          if (!IsPoolPreallocated(&ht->pdata_prealloc[i]))
               return NULL;
     }

     /* Initialize each of the RB-Tree, mutexes and stats */
     for (i = 0; i < hparam.index_size; i++) {
          /* RBT Init */
          RBT_HEAD_INIT(&(ht->array_rbt[i]));

          /* Mutex Init */
          if (pthread_rwlock_init(&(ht->array_lock[i]),
                                  &rwlockattr) != 0) {
               LogCrit(COMPONENT_HASHTABLE,
                       "Unable to initialize lock array.");
               /* XXX This should clean up, but how do you destroy a
                  memory pool? */
               return NULL;
          }

          /* Initialization of the stats structure */
          ht->stat_dynamic[i].nb_entries = 0;

          ht->stat_dynamic[i].ok.nb_set = 0;
          ht->stat_dynamic[i].ok.nb_get = 0;
          ht->stat_dynamic[i].ok.nb_del = 0;
          ht->stat_dynamic[i].ok.nb_test = 0;

          ht->stat_dynamic[i].err.nb_set = 0;
          ht->stat_dynamic[i].err.nb_get = 0;
          ht->stat_dynamic[i].err.nb_del = 0;
          ht->stat_dynamic[i].err.nb_test = 0;

          ht->stat_dynamic[i].notfound.nb_set = 0;
          ht->stat_dynamic[i].notfound.nb_get = 0;
          ht->stat_dynamic[i].notfound.nb_del = 0;
          ht->stat_dynamic[i].notfound.nb_test = 0;
     }

     pthread_rwlockattr_destroy(&rwlockattr);

     /* final return, if we arrive here, then everything is alright */
     return ht;
} /* HashTable_Init */

/**
 *
 * @brief Set a pair (key,value) into the Hash Table
 *
 * Depending on the value of 'how', this function sets a value into
 * the hash table or tests that the hash table contains that value.
 *
 * @param ht [in] The hashtable to test or alter
 * @param key [in] The key to be set
 * @param val [in] The value to be stored
 * @param how [in] A value determining whether this is a test, a set
 *                 with overwrite, or a set without overwrite.
 *
 * @retval HASHTABLE_SUCCESS if successfull.
 */
hashtable_error_t
HashTable_Test_And_Set(hash_table_t *ht,
                       hash_buffer_t *key,
                       hash_buffer_t *val,
                       hashtable_set_how_t how)
{
     /* The index, calculated for this key, specifying the partition
        where it should be set within the hash table. */
     uint32_t index = 0;
     /* The hash value to be used as a key within the Red-Black tree
        comprising the partition */
     uint64_t rbt_hash = 0;
     /* The root of the red-black tree comprising the partition */
     struct rbt_head *root = NULL;
     /* The node returned if an entry is found, or used to indicate
        where a new node should be inserted. */
     struct rbt_node *locator = NULL;
     /* The node which is inserted into the table.  Alternatively,
        the alias for the found node that is set for overwrite. */
     struct rbt_node *mutator = NULL;
     /* The pair of buffer descriptors locating both key and value
        for this object, what actually gets stored. */
     hash_data_t *data = NULL;
     /* Stored error return */
     hashtable_error_t rc = HASHTABLE_SUCCESS;

     /* Sanity check */
     if ((ht == NULL) || (key == NULL) || (val == NULL)) {
          return HASHTABLE_ERROR_INVALID_ARGUMENT;
     }

     if ((rc = compute(ht, key, &index, &rbt_hash))
         != HASHTABLE_SUCCESS) {
          return rc;
     }

     root = &(ht->array_rbt[index]);

     LogFullDebug(COMPONENT_HASHTABLE,
                  "Key = %p, Value = %p, index = %"PRIu32
                  ", rbt_value = %"PRIu64,
                  key->pdata, val->pdata, index, rbt_hash);

     /* acquire mutex for protection */
     pthread_rwlock_wrlock(&(ht->array_lock[index]));

     if (Key_Locate(ht, key, index, rbt_hash, &locator)
         == HASHTABLE_SUCCESS) {
          /* An entry of that key already exists */
          if (how == HASHTABLE_SET_HOW_TEST_ONLY) {
               ht->stat_dynamic[index].ok.nb_test += 1;
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_SUCCESS;
          }

          if (how == HASHTABLE_SET_HOW_SET_NO_OVERWRITE) {
               ht->stat_dynamic[index].err.nb_test += 1;
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
          }

          mutator = locator;
          data = RBT_OPAQ(mutator);

          LogFullDebug(COMPONENT_HASHTABLE,
                       "Entry already exists (k=%p,v=%p)",
                       key->pdata, val->pdata);
     } else {
          if (how == HASHTABLE_SET_HOW_TEST_ONLY) {
               ht->stat_dynamic[index].notfound.nb_test += 1;
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_ERROR_NO_SUCH_KEY;
          }

          /* Insert a new node in the table */
          RBT_FIND(root, locator, rbt_hash);

          /* This entry does not exist, create it */
          /* First get a new entry in the preallocated node array */
          GetFromPool(mutator, &ht->node_prealloc[index], rbt_node_t);
          if(mutator == NULL) {
               ht->stat_dynamic[index].err.nb_set += 1;
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_INSERT_MALLOC_ERROR;
          }

          GetFromPool(data, &ht->pdata_prealloc[index], hash_data_t);
          if (data == NULL) {
               ht->stat_dynamic[index].err.nb_set += 1;
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_INSERT_MALLOC_ERROR;
          }

          RBT_OPAQ(mutator) = data;
          RBT_VALUE(mutator) = rbt_hash;
          RBT_INSERT(root, mutator, locator);

          LogFullDebug(COMPONENT_HASHTABLE,
                       "Create new entry (k=%p,v=%p), mutator=%p, data=%p",
                       key->pdata, val->pdata, mutator,
                       RBT_OPAQ(mutator));
     }

     data->buffval.pdata = val->pdata;
     data->buffval.len = val->len;

     data->buffkey.pdata = key->pdata;
     data->buffkey.len = key->len;

     ht->stat_dynamic[index].nb_entries += 1;
     ht->stat_dynamic[index].ok.nb_set += 1;

     /* Release mutex */
     pthread_rwlock_unlock(&(ht->array_lock[index]));

     return HASHTABLE_SUCCESS;
} /* HashTable_Test_And_Set */

/**
 *
 * @brief Look up a value and take a reference
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It also calls the supplied function to take
 * a reference before releasing the partition lock.
 *
 * @param ht [in] The hash store to be searched
 * @param key [in] A buffer descriptore locating the key to find
 * @param val [out] A buffer descriptor locating the value found
 * @param get_ref [in] A function to take a reference on the supplied
 *                     value
 *
 * @return HASHTABLE_SUCCESS or errors
 */

hashtable_error_t
HashTable_GetRef(hash_table_t *ht,
                 hash_buffer_t *key,
                 hash_buffer_t *val,
                 void (*get_ref)(hash_buffer_t *))
{
     /* The index specifying the partition to search */
     uint32_t index = 0;
     /* The node found for the key */
     struct rbt_node *locator = NULL;
     /* The buffer descritpros for the key and value for the found entry */
     hash_data_t *data = NULL;
     /* The hash value to be searched for within the Red-Black tree */
     unsigned long rbt_hash = 0;
     /* Stored error return */
     hashtable_error_t rc = HASHTABLE_SUCCESS;

     /* Sanity check */
     if (ht == NULL || key == NULL || val == NULL)
          return HASHTABLE_ERROR_INVALID_ARGUMENT;

     if ((rc = compute(ht, key, &index, &rbt_hash))
         != HASHTABLE_SUCCESS) {
          return rc;
     }

     /* Acquire mutex */
     pthread_rwlock_rdlock(&(ht->array_lock[index]));

     if ((rc = Key_Locate(ht, key, index, rbt_hash, &locator))
         != HASHTABLE_SUCCESS) {
          ht->stat_dynamic[index].notfound.nb_get += 1;
          pthread_rwlock_unlock(&(ht->array_lock[index]));
          return rc;
     }

     /* Key was found */
     data = RBT_OPAQ(locator);
     val->pdata = data->buffval.pdata;
     val->len = data->buffval.len;

     ht->stat_dynamic[index].ok.nb_get += 1;

     if (get_ref != NULL) {
          get_ref(val);
     }

     /* Release mutex */
     pthread_rwlock_unlock(&(ht->array_lock[index]));

     return HASHTABLE_SUCCESS;
} /* HashTable_GetRef */

/**
 *
 * @brief Look up a value
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It is implemented as a wrapper around
 * the HashTable_GetRef function.
 *
 * @param ht [in] The hash store to be searched
 * @param key [in] A buffer descriptore locating the key to find
 * @param val [out] A buffer descriptor locating the value found
 *
 * @return HASHTABLE_SUCCESS or errors
 */

hashtable_error_t
HashTable_Get(hash_table_t *ht,
              hash_buffer_t *key,
              hash_buffer_t *val)
{
     return HashTable_GetRef(ht, key, val, NULL);
} /* HashTable_Get */

/**
 *
 * @brief Look up, return, and remove an entry
 *
 * If the object specified by key can be found, it will be removed
 * from the hash table and returned to the caller.
 *
 * @param ht [in] The hash table to be altered
 * @param key [in] The key to search for and remove
 * @param val [out] The value associated with the found object
 * @param stored_key [out] Buffer descriptor for key actually stored
 *
 * @return HASHTABLE_SUCCESS or errors
 */

hashtable_error_t
HashTable_Get_and_Del(hash_table_t  *ht,
                      hash_buffer_t *key,
                      hash_buffer_t *val,
                      hash_buffer_t *stored_key)
{
     /* The partition to search for this key */
     uint32_t index = 0;
     /* The node corresponding to this key */
     struct rbt_node *locator = NULL;
     /* The root of the Red-Black tree for this partition */
     struct rbt_head *root = NULL;
     /* Buffer descriptors for the stored key and value */
     hash_data_t *data = NULL;
     /* Hashed value to be used as the key within the tree */
     unsigned long rbt_hash = 0;
     /* Error return code */
     hashtable_error_t rc = HASHTABLE_SUCCESS;

     /* Sanity check */
     if (ht == NULL || key == NULL || val == NULL) {
          return HASHTABLE_ERROR_INVALID_ARGUMENT;
     }

     if ((rc = compute(ht, key, &index, &rbt_hash))
         != HASHTABLE_SUCCESS) {
          return rc;
     }

     root = &(ht->array_rbt[index]);

     pthread_rwlock_wrlock(&(ht->array_lock[index]));

     if ((rc = Key_Locate(ht, key, index, rbt_hash, &locator))
         != HASHTABLE_SUCCESS) {
          ht->stat_dynamic[index].notfound.nb_get += 1;
          pthread_rwlock_unlock(&(ht->array_lock[index]));
          return rc;
     }

     data = RBT_OPAQ(locator);
     *val = data->buffval;

     if (stored_key != NULL) {
          *stored_key = data->buffkey;
     }

     ht->stat_dynamic[index].ok.nb_get += 1;

     /* Now remove the entry */
     RBT_UNLINK(root, locator);
     ht->stat_dynamic[index].nb_entries -= 1;
     ReleaseToPool(data, &ht->pdata_prealloc[index]);
     ReleaseToPool(locator, &ht->node_prealloc[index]);

     ht->stat_dynamic[index].ok.nb_del += 1;

     pthread_rwlock_unlock(&(ht->array_lock[index]));

     return HASHTABLE_SUCCESS;
} /* HashTable_Get_and_Del */

/**
 *
 * @brief Remove and free all (key,val) couples from the hash store
 *
 * This function removes all (key,val) couples from the hashtable and
 * frees the stored data using the supplied function
 *
 * @param ht [in] The hashtable to be cleared of all entries
 * @param free_func [in] The function with which to free the contents
 *                       of each entry
 *
 * @return HASHTABLE_SUCCESS or errors
 *
 */

hashtable_error_t
HashTable_Delall(hash_table_t *ht,
                 int (*free_func)(hash_buffer_t, hash_buffer_t))
{
     /* Successive partition numbers */
     uint32_t partition = 0;

     /* Sanity check */
     if (ht == NULL || free_func == NULL) {
          return HASHTABLE_ERROR_INVALID_ARGUMENT;
     }

     LogFullDebug(COMPONENT_HASHTABLE,
                  "Deleting all entries in hashtable.");

     for (partition = 0; partition < ht->parameter.index_size; partition++) {
          /* The root of each successive partition */
          struct rbt_head *root = &(ht->array_rbt[partition]);

          pthread_rwlock_wrlock(&(ht->array_lock[partition]));

          /* Continue until there are no more entries in the red-black
             tree */
          while (ht->stat_dynamic[partition].nb_entries != 0) {
               /* Pointer to the key and value descriptors for each successive
                  entry */
               hash_data_t *data = NULL;
               /* Pointer to node in tree for removal */
               struct rbt_node *cursor = RBT_LEFTMOST(root);
               /* Aliased poitner to node, for freeing buffers after
                  removal from tree */
               struct rbt_node *holder = cursor;
               /* Buffer descriptor for key, as stored */
               hash_buffer_t key;
               /* Buffer descriptor for value, as stored */
               hash_buffer_t val;
               /* Return code from the free function.  Zero on failure */
               int rc = 0;

               if (cursor == NULL) {
                    break;
               }

               RBT_UNLINK(root, cursor);
               data = RBT_OPAQ(holder);

               key = data->buffkey;
               val = data->buffval;

               ReleaseToPool(data, &ht->pdata_prealloc[partition]);
               ReleaseToPool(holder, &ht->node_prealloc[partition]);
               ht->stat_dynamic[partition].nb_entries -= 1;
               ht->stat_dynamic[partition].ok.nb_del += 1;
               rc = free_func(key, val);

               if (rc == 0) {
                    pthread_rwlock_unlock(&(ht->array_lock[partition]));
                    return HASHTABLE_ERROR_DELALL_FAIL;
               }
          }
          pthread_rwlock_unlock(&(ht->array_lock[partition]));
     }

     return HASHTABLE_SUCCESS;
} /* HashTable_Delall */

/**
 *
 * @brief Decrement the refcount of and possibly remove an entry
 *
 * This function decrements the reference count and deletes the entry
 * if it goes to zero.
 *
 * @param ht [in] The hashtable to be modified
 * @param key [in] The key corresponding to the entry to delete
 * @param stored_key [out] If non-NULL, a buffer descriptor specifying
 *                         the key as stored in the hash table
 * @param stored_val [out] If non-NULL, a buffer descriptor specifying
 *                         the key as stored in the hash table
 * @param put_ref [in] Function to decrement the reference count of
 *                     the located object.  If the function returns 0,
 *                     the entry is deleted.  If put_ref is NULL, the
 *                     entry is deleted unconditionally.
 *
 * @retval HASHTABLE_SUCCESS on deletion
 * @retval HASHTABLE_NOT_DELETED put_ref returned a non-zero value
 */

hashtable_error_t
HashTable_DelRef(hash_table_t *ht,
                 hash_buffer_t *key,
                 hash_buffer_t *stored_key,
                 hash_buffer_t *stored_val,
                 int (*put_ref)(hash_buffer_t *))
{
     /* The value indicating the partition from which to delete the
        key */
     uint32_t index = 0;
     /* The hash value acting as a key within a given Red-Black tree */
     uint64_t rbt_hash = 0;
     /* The found node to be acted upon */
     struct rbt_node *locator = NULL;
     /* The root of the tree making up the partition */
     struct rbt_head *root = NULL;
     /* Pointer to the buffer descriptors locating the stored key and
        value. */
     hash_data_t *data = NULL;
     /* Stored error return code */
     hashtable_error_t rc = 0;

     /* Sanity check */
     if (ht == NULL || key == NULL) {
          return HASHTABLE_ERROR_INVALID_ARGUMENT;
     }

     if ((rc = compute(ht, key, &index, &rbt_hash))
         != HASHTABLE_SUCCESS) {
          return rc;
     }


     pthread_rwlock_wrlock(&(ht->array_lock[index]));

     if ((rc = Key_Locate(ht, key, index, rbt_hash, &locator))
         != HASHTABLE_SUCCESS) {
          ht->stat_dynamic[index].notfound.nb_del += 1;
          pthread_rwlock_unlock(&(ht->array_lock[index]));
          return rc;
     }

     data = RBT_OPAQ(locator);

     /* Return the stored buffers, if requested */
     if (stored_key != NULL) {
          *stored_key = data->buffkey;
     }

     if (stored_val != NULL) {
          *stored_val = data->buffval;
     }

     if (put_ref != NULL)
          if (put_ref(&data->buffval) != 0) {
               pthread_rwlock_unlock(&(ht->array_lock[index]));
               return HASHTABLE_NOT_DELETED;
          }

     /* There are no outstanding references or the caller isn't using
        references. */

     root = &(ht->array_rbt[index]);
     RBT_UNLINK(root, locator);
     ht->stat_dynamic[index].nb_entries -= 1;
     ReleaseToPool(data, &ht->pdata_prealloc[index]);
     ReleaseToPool(locator, &ht->node_prealloc[index]);
     ht->stat_dynamic[index].ok.nb_del += 1;

     pthread_rwlock_unlock(&(ht->array_lock[index]));

     return HASHTABLE_SUCCESS;
} /* HashTable_DelRef */

/**
 *
 * @brief Remove an entry from the hash table
 *
 * This function deletes anentry from the hash table.
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

hashtable_error_t
HashTable_Del(hash_table_t *ht,
              hash_buffer_t *key,
              hash_buffer_t *stored_key,
              hash_buffer_t *stored_val)
{
     return HashTable_DelRef(ht, key, stored_key,
                             stored_val, NULL);
}

/**
 *
 * @brief Get information on the hash table
 *
 * This function provides statistical information (mostly for
 * debugging purposes) on the hash table.  Some of this information
 * must be computed at call-time
 *
 * @param ht [in] The hashtable to be interrogate
 * @param hstat [out] The result structure to be filled in
 *
 */

void
HashTable_GetStats(hash_table_t *ht,
                   hash_stat_t *hstat)
{
     size_t i = 0;

     /* Sanity check */
     if (ht == NULL || hstat == NULL) {
          return;
     }

     /* Firt, copy the dynamic values */
     memcpy(&(hstat->dynamic), ht->stat_dynamic, sizeof(hash_stat_dynamic_t));

     /* Then compute the other values */

     /* A min value hash to be initialized with a huge value */
     hstat->computed.min_rbt_num_node = 1 << 31;

     /* A max value is initialized with 0 */
     hstat->computed.max_rbt_num_node = 0;
     /* And so does the average value */
     hstat->computed.average_rbt_num_node = 0;

     hstat->dynamic.nb_entries = 0;

     hstat->dynamic.ok.nb_set = 0;
     hstat->dynamic.ok.nb_test = 0;
     hstat->dynamic.ok.nb_get = 0;
     hstat->dynamic.ok.nb_del = 0;

     hstat->dynamic.err.nb_set = 0;
     hstat->dynamic.err.nb_test = 0;
     hstat->dynamic.err.nb_get = 0;
     hstat->dynamic.err.nb_del = 0;

     hstat->dynamic.notfound.nb_set = 0;
     hstat->dynamic.notfound.nb_test = 0;
     hstat->dynamic.notfound.nb_get = 0;
     hstat->dynamic.notfound.nb_del = 0;

     for (i = 0; i < ht->parameter.index_size; i++) {
          if (ht->array_rbt[i].rbt_num_node > hstat->computed.max_rbt_num_node)
               hstat->computed.max_rbt_num_node
                    = ht->array_rbt[i].rbt_num_node;

          if (ht->array_rbt[i].rbt_num_node < hstat->computed.min_rbt_num_node)
               hstat->computed.min_rbt_num_node
                    = ht->array_rbt[i].rbt_num_node;

          hstat->computed.average_rbt_num_node
               += ht->array_rbt[i].rbt_num_node;

          hstat->dynamic.nb_entries += ht->stat_dynamic[i].nb_entries;

          hstat->dynamic.ok.nb_set += ht->stat_dynamic[i].ok.nb_set;
          hstat->dynamic.ok.nb_test += ht->stat_dynamic[i].ok.nb_test;
          hstat->dynamic.ok.nb_get += ht->stat_dynamic[i].ok.nb_get;
          hstat->dynamic.ok.nb_del += ht->stat_dynamic[i].ok.nb_del;

          hstat->dynamic.err.nb_set += ht->stat_dynamic[i].err.nb_set;
          hstat->dynamic.err.nb_test += ht->stat_dynamic[i].err.nb_test;
          hstat->dynamic.err.nb_get += ht->stat_dynamic[i].err.nb_get;
          hstat->dynamic.err.nb_del += ht->stat_dynamic[i].err.nb_del;

          hstat->dynamic.notfound.nb_set
               += ht->stat_dynamic[i].notfound.nb_set;
          hstat->dynamic.notfound.nb_test
               += ht->stat_dynamic[i].notfound.nb_test;
          hstat->dynamic.notfound.nb_get
               += ht->stat_dynamic[i].notfound.nb_get;
          hstat->dynamic.notfound.nb_del
               += ht->stat_dynamic[i].notfound.nb_del;
     }

     hstat->computed.average_rbt_num_node /= ht->parameter.index_size;
} /* Hashtable_GetStats */

/**
 *
 * @brief Gets the number of entries in the hashtable.
 *
 * This function gets the number of entries in the hashtable.
 *
 * @param ht [in] The hashtable to be interrogated
 *
 * @return the number of found entries
 */

size_t
HashTable_GetSize(hash_table_t *ht)
{
     size_t i = 0, nb_entries = 0;

     /* Sanity check */
     if (ht == NULL)
          return HASHTABLE_ERROR_INVALID_ARGUMENT;

     for (i = 0; i < ht->parameter.index_size; i++)
          nb_entries += ht->stat_dynamic[i].nb_entries;

     return nb_entries;
} /* HashTable_GetSize */

/**
 *
 * @brief Log information about the hashtable
 *
 * This debugging function prints information about the hash table to
 * the log.
 *
 * @param component the component debugging config to use.
 * @param ht the hashtable to be used.
 *
 */
void
HashTable_Log(log_components_t component,
              hash_table_t *ht)
{
     /* The current position in the hash table */
     struct rbt_node *it = NULL;
     /* The root of the tree currently being inspected */
     struct rbt_head *root;
     /* Buffer descriptors for the key and value */
     hash_data_t *data = NULL;
     /* String representation of the key */
     char dispkey[HASHTABLE_DISPLAY_STRLEN];
     /* String representation of the stored value */
     char dispval[HASHTABLE_DISPLAY_STRLEN];
     /* Index for traversing the partitions */
     size_t i = 0;
     /* Running count of entries  */
     size_t nb_entries = 0;
     /* Recomputed partitionindex */
     uint32_t index = 0;
     /* Recomputed hash for Red-Black tree*/
     uint64_t rbt_hash = 0;

     /* Sanity check */
     if (ht == NULL)
          return;

     LogFullDebug(component,
                  "The hash is partitioned into %d trees",
                  ht->parameter.index_size);

     for (i = 0; i < ht->parameter.index_size; i++) {
          nb_entries += ht->stat_dynamic[i].nb_entries;
     }

     LogFullDebug(component, "The hash contains %zd entries",
                  nb_entries);

     for (i = 0; i < ht->parameter.index_size; i++) {
          root = &((ht->array_rbt)[i]);
          LogFullDebug(component,
                       "The partition in position %zd "
                       "contains: %d entries",
                       i, root->rbt_num_node);
          RBT_LOOP(root, it) {
               data = it->rbt_opaq;

               ht->parameter.key_to_str(&(data->buffkey), dispkey);
               ht->parameter.val_to_str(&(data->buffval), dispval);

               if (compute(ht, &data->buffkey, &index, &rbt_hash)
                   != HASHTABLE_SUCCESS) {
                    LogCrit(component,
                            "Possible implementation error in hash_func_both");
                    index = 0;
                    rbt_hash = 0;
               }

               LogFullDebug(component,
                            "%s => %s; index=%"PRIu32" rbt_hash=%"PRIu64,
                            dispkey, dispval, index, rbt_hash);
               RBT_INCREMENT(it);
          }
     }
} /* HashTable_Log */

/**
 *
 * @brief Print information about the hashtable
 *
 * This debugging function prints information about the hash table to
 * stderr.
 *
 * @param ht the hashtable to be used.
 *
 */
void
HashTable_Print(hash_table_t *ht)
{
     /* The current position in the hash table */
     struct rbt_node *it = NULL;
     /* The root of the tree currently being inspected */
     struct rbt_head *root;
     /* Buffer descriptors for the key and value */
     hash_data_t *data = NULL;
     /* String representation of the key */
     char dispkey[HASHTABLE_DISPLAY_STRLEN];
     /* String representation of the stored value */
     char dispval[HASHTABLE_DISPLAY_STRLEN];
     /* Index for traversing the partitions */
     size_t i = 0;
     /* Running count of entries  */
     size_t nb_entries = 0;
     /* Recomputed partition index */
     uint32_t index = 0;
     /* Recomputed hash in Red-Black Tree */
     uint64_t rbt_hash = 0;

     /* Sanity check */
     if (ht == NULL)
          return;

     fprintf(stderr,
             "The hash is partitioned into %d trees",
             ht->parameter.index_size);

     for (i = 0; i < ht->parameter.index_size; i++) {
          nb_entries += ht->stat_dynamic[i].nb_entries;
     }

     fprintf(stderr, "The hash contains %zd entries", nb_entries);

     for (i = 0; i < ht->parameter.index_size; i++) {
          root = &((ht->array_rbt)[i]);
          fprintf(stderr,
                  "The partition in position %zd contains: %d entries",
                  i, root->rbt_num_node);
          RBT_LOOP(root, it) {
               data = it->rbt_opaq;

               ht->parameter.key_to_str(&(data->buffkey), dispkey);
               ht->parameter.val_to_str(&(data->buffval), dispval);

               if (compute(ht, &data->buffkey, &index, &rbt_hash)
                   != HASHTABLE_SUCCESS) {
                    LogCrit(COMPONENT_HASHTABLE,
                            "Possible implementation error in hash_func_both");
                    index = 0;
                    rbt_hash = 0;
               }

               fprintf(stderr,
                       "%s => %s; index=%"PRIu32" rbt_hash=%"PRIu64,
                       dispkey, dispval, index, rbt_hash);
               RBT_INCREMENT(it);
          }
     }
} /* HashTable_Print */

/* @} */

