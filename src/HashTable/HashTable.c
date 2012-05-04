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
#include <assert.h>

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
hash_table_err_to_str(hash_error_t err)
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
 * @param ht [in] The hashtable to be used
 * @param key [in] The key to look up
 * @param index [in] Index into RBT array
 * @param rbthash [in] Hash in red-black tree
 * @param node [out] On success, the found node, NULL otherwise
 *
 * @retval HASHTABLE_SUCCESS if successfull
 * @retval HASHTABLE_NO_SUCH_KEY if key was not found
 */
static hash_error_t
Key_Locate(struct hash_table *ht,
           struct hash_buff *key,
           uint32_t index,
           uint64_t rbthash,
           struct rbt_node **node)
{
     /* The root of the red black tree matching this index */
     struct rbt_head *root = NULL;
     /* A pair of buffer descriptors locating key and value for this
        entry*/
     struct hash_data *data = NULL;
     /* The node in the red-black tree currently being traversed */
     struct rbt_node *cursor = NULL;
     /* TRUE if we have located the key */
     int found = FALSE;

     *node = NULL;

     root = &(ht->partitions[index].rbt);

     /* The lefmost occurrence of the value is the one from which we
        may start iteration to visit all nodes containing a value. */
     RBT_FIND_LEFT(root, cursor, rbthash);

     if (cursor == NULL) {
          LogFullDebug(COMPONENT_HASHTABLE,
                       "Key not found: rbthash = %"PRIu64,
                       rbthash);
          return HASHTABLE_ERROR_NO_SUCH_KEY;
     }

     while ((cursor != NULL) && (RBT_VALUE(cursor) == rbthash)) {
          data = RBT_OPAQ(cursor);
          if (ht->parameter.compare_key(key,
                                        &(data->buffkey)) == 0) {
               found = TRUE;
               break;
          }
          RBT_INCREMENT(cursor);
     }

     if (!found) {
          LogFullDebug(COMPONENT_HASHTABLE,
                       "Matching hash found, but no matching key.");
          return HASHTABLE_ERROR_NO_SUCH_KEY;
     }

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

static inline hash_error_t
compute(struct hash_table *ht, struct hash_buff *key,
        uint32_t *index, uint64_t *rbt_hash)
{
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

     /* At the suggestion of Jim Lieb, die if a hash function sends
        us off the end of the array. */

     assert(*index < ht->parameter.index_size);

     return HASHTABLE_SUCCESS;
}

/*}@ */

/**
 * @defgroup HTExported Functions in the public Hash Table interface
 *@{
 */

/* The following are the hash table primitives implementing the
   actual functionality. */

/**
 *
 * @brief Initialize a new hash table
 *
 * This function initializes and allocates storage for a hash table.
 *
 * @param hparam [in] Parameters to determine the hash table's
 *                    behaviour
 *
 * @return Pointer to the new hash table, NULL on failure
 *
 */

struct hash_table *
HashTable_Init(struct hash_param *hparam)
{
     /* The hash table being constructed */
     struct hash_table *ht = NULL;
     /* The index for initializing each partition */
     uint32_t index = 0;
     /* Read-Write Lock attributes, to prevent write starvation under
        GLIBC */
     pthread_rwlockattr_t rwlockattr;
     /* The number of fully initialized partitions */
     uint32_t completed = 0;

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
          LogCrit(COMPONENT_HASHTABLE,
                  "Unable to set writer-preference on lock attribute.");
          goto deconstruct;
     }
#endif /* GLIBC */

     if ((ht = Mem_Alloc(sizeof(struct hash_table) +
                         sizeof(struct hash_partition) *
                         hparam->index_size)) == NULL) {
          goto deconstruct;
     }

     memset(ht, 0,
            sizeof(struct hash_table) +
            sizeof(struct hash_partition) * hparam->index_size);

     /* We need to save copy of the parameters in the table. */
     ht->parameter = *hparam;

     for (index = 0; index < hparam->index_size; ++index) {

          MakePool(&ht->partitions[index].node_pool,
                                   hparam->nb_node_prealloc,
                                   rbt_node_t, NULL, NULL);
          if (!IsPoolPreallocated(&ht->partitions[index].node_pool)) {
               goto deconstruct;
          }
          MakePool(&ht->partitions[index].data_pool,
                   hparam->nb_node_prealloc, hash_data_t, NULL, NULL);
          if (!IsPoolPreallocated(&ht->partitions[index].data_pool))
               goto deconstruct;
          RBT_HEAD_INIT(&(ht->partitions[index].rbt));

          /**
           * @todo: ACE: When a function to destroy a memory pool is
           * created, then the memory pools allocated on THIS
           * ITERATION should be freed before jumping to deconstruct.
           */

          if (pthread_rwlock_init(&(ht->partitions[index].lock),
                                  &rwlockattr) != 0) {
               LogCrit(COMPONENT_HASHTABLE,
                       "Unable to initialize lock in hash table.");
               goto deconstruct;
          }
          completed++;
     }

     pthread_rwlockattr_destroy(&rwlockattr);

     return ht;

deconstruct:

     while (completed != 0) {
          /**
           * @todo ACE: This does nothing about the memory pools
           * because there is no function in stuff_alloc.h that I
           * could see to destroy a memory pool.  Such a function
           * should be added and called here.
           */
          pthread_rwlock_destroy(
               &(ht->partitions[completed - 1].lock));
          completed--;
     }

     Mem_Free(ht);
     return (ht = NULL);

} /* HashTable_Init */

hash_error_t
HashTable_GetLatch(struct hash_table *ht,
                   struct hash_buff *key,
                   struct hash_buff *val,
                   int may_write,
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

     if ((rc = compute(ht, key, &index, &rbt_hash))
         != HASHTABLE_SUCCESS) {
          return rc;
     }

     /* Acquire mutex */
     if (may_write) {
          pthread_rwlock_wrlock(&(ht->partitions[index].lock));
     } else {
          pthread_rwlock_rdlock(&(ht->partitions[index].lock));
     }

     rc = Key_Locate(ht, key, index, rbt_hash, &locator);

     if (rc == HASHTABLE_SUCCESS) {
          /* Key was found */
          data = RBT_OPAQ(locator);
          if (val) {
               val->pdata = data->buffval.pdata;
               val->len = data->buffval.len;
          }
     }

     if (((rc == HASHTABLE_SUCCESS) ||
          (rc == HASHTABLE_ERROR_NO_SUCH_KEY)) &&
         (latch != NULL)) {
          latch->index = index;
          latch->rbt_hash = rbt_hash;
          latch->locator = locator;
     } else {
          pthread_rwlock_unlock(&ht->partitions[index].lock);
     }

     return rc;
} /* HashTable_GetLatch */

/**
 *
 * @brief Release lock held on hash table
 *
 * This function releases the lock on the hash partition acquired and
 * retained by a call to HashTable_GetLatch.  This function must be
 * used to free any acquired lock but ONLY if the lock was not already
 * freed by some other means (HashTable_SetLatched or
 * HashTable_DelLatched).
 *
 * @param ht [in] The hash table with the lock to be released
 * @param latch [in] The latch structure holding retained state
 *
 * @returns HASHTABLE_SUCCESS.
 */

hash_error_t
HashTable_ReleaseLatched(struct hash_table *ht,
                         struct hash_latch *latch)
{
     if (latch) {
          pthread_rwlock_unlock(&ht->partitions[latch->index].lock);
          memset(latch, 0, sizeof(struct hash_latch));
     }

     return HASHTABLE_SUCCESS;
} /* HashTable_ReleaseLatched */

/**
 *
 * @brief Set a value in a table following a previous GetLatch
 *
 * This function sets a value in a hash table following a previous
 * call to the HashTable_GetLatch function.  It must only be used
 * after such a call made with the may_write parameter set to true.
 * In all cases, the lock on the hash table is released.
 *
 * @param ht [in] The hash store to be modified
 * @param key [in] A buffer descriptor locating the key to set
 * @param val [in] A buffer descriptor locating the value to insert
 * @param latch [in] A pointer to a structure filled by a previous
 *                   call to HashTable_GetLatched.
 * @param overwrite [in] If true, overwrite a prexisting key,
 *                       otherwise return error on collision.
 * @param stored_key [out] If non-NULL, a buffer descriptor for an
 *                         overwritten key as stored.
 * @param stored_val [out] If non-NULL, a buffer descriptor for an
 *                         overwritten value as stored.
 *
 * @retval HASHTABLE_SUCCESS on non-colliding insert
 * @retval HASHTABLE_ERROR_KEY_ALREADY_EXISTS if overwrite disabled
 * @retval HASHTABLE_OVERWRITTEN on successful overwrite
 * @retval Other errors on failure
 */

hash_error_t
HashTable_SetLatched(struct hash_table *ht,
                     struct hash_buff *key,
                     struct hash_buff *val,
                     struct hash_latch *latch,
                     int overwrite,
                     struct hash_buff *stored_key,
                     struct hash_buff *stored_val)
{
     /* Stored error return */
     hash_error_t rc = HASHTABLE_SUCCESS;
     /* The pair of buffer descriptors locating both key and value
        for this object, what actually gets stored. */
     hash_data_t *descriptors = NULL;
     /* Node giving the location to insert new node */
     struct rbt_node *locator = NULL;
     /* New node for the case of non-overwrite */
     struct rbt_node *mutator = NULL;

     /* In the case of collision */
     if (latch->locator) {
          if (!overwrite) {
               rc = HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
               goto out;
          }

          descriptors = RBT_OPAQ(latch->locator);
          if (stored_key) {
               *stored_key = descriptors->buffkey;
          }
          if (stored_val) {
               *stored_val = descriptors->buffval;
          }
          descriptors->buffkey = *key;
          descriptors->buffval = *val;
          rc = HASHTABLE_OVERWRITTEN;
          goto out;
     }

     /* We have no collision, so go about creating and inserting a new
        node. */

     RBT_FIND(&ht->partitions[latch->index].rbt,
              locator, latch->rbt_hash);

     GetFromPool(mutator, &ht->partitions[latch->index].node_pool,
                 rbt_node_t);
     if (mutator == NULL) {
          rc = HASHTABLE_INSERT_MALLOC_ERROR;
          goto out;
     }

     GetFromPool(descriptors,
                 &ht->partitions[latch->index].data_pool, hash_data_t);
     if (descriptors == NULL) {
          ReleaseToPool(mutator,
                        &ht->partitions[latch->index].node_pool);
          rc = HASHTABLE_INSERT_MALLOC_ERROR;
          goto out;
     }

     RBT_OPAQ(mutator) = descriptors;
     RBT_VALUE(mutator) = latch->rbt_hash;
     RBT_INSERT(&ht->partitions[latch->index].rbt, mutator, locator);

     descriptors->buffkey.pdata = key->pdata;
     descriptors->buffkey.len = key->len;

     descriptors->buffval.pdata = val->pdata;
     descriptors->buffval.len = val->len;

     /* Only in the non-overwrite case */
     ++ht->partitions[latch->index].count;

     rc = HASHTABLE_SUCCESS;

out:
     HashTable_ReleaseLatched(ht, latch);
     return rc;
} /* HashTable_SetLatched */

/**
 *
 * @brief Delete a value from the store following a previous GetLatch
 *
 * This function removes a value from the a hash store, the value
 * already having been looked up with GetLatched.  In all cases, the
 * lock is released.  HashTable_GetLatch must have been called with
 * may_read true.
 *
 * @param ht [in] The hash store to be modified
 * @param key [in] A buffer descriptore locating the key to remove
 * @param latch [in] A pointer to a structure filled by a previous
 *                   call to HashTable_GetLatched.
 * @param stored_key [out] If non-NULL, a buffer descriptor the
 *                         removed key as stored.
 * @param stored_val [out] If non-NULL, a buffer descriptor for the
 *                         removed value as stored.
 *
 * @retval HASHTABLE_SUCCESS on non-colliding insert
 * @retval Other errors on failure
 */

hash_error_t
HashTable_DeleteLatched(struct hash_table *ht,
                        struct hash_buff *key,
                        struct hash_latch *latch,
                        struct hash_buff *stored_key,
                        struct hash_buff *stored_val)
{
     /* The pair of buffer descriptors comprising the stored entry */
     struct hash_data *data = NULL;

     if (!latch->locator) {
          HashTable_ReleaseLatched(ht,
                                   latch);
     }

     data = RBT_OPAQ(latch->locator);
     if (stored_key) {
          *stored_key = data->buffkey;
     }

     if (stored_val) {
          *stored_val = data->buffval;
     }

     /* Now remove the entry */
     RBT_UNLINK(&ht->partitions[latch->index].rbt, latch->locator);
     ReleaseToPool(data, &ht->partitions[latch->index].data_pool);
     ReleaseToPool(latch->locator, &ht->partitions[latch->index].node_pool);

     HashTable_ReleaseLatched(ht, latch);
     return HASHTABLE_SUCCESS;
} /* HashTable_DeleteLatched */

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

hash_error_t
HashTable_Delall(struct hash_table *ht,
                 int (*free_func)(struct hash_buff,
                                  struct hash_buff))
{
     /* Successive partition numbers */
     uint32_t index = 0;

     for (index = 0; index < ht->parameter.index_size; index++) {
          /* The root of each successive partition */
          struct rbt_head *root = &ht->partitions[index].rbt;
          /* Pointer to node in tree for removal */
          struct rbt_node *cursor = NULL;

          pthread_rwlock_wrlock(&ht->partitions[index].lock);

          /* Continue until there are no more entries in the red-black
             tree */
          while ((cursor = RBT_LEFTMOST(root)) != NULL) {
               /* Pointer to the key and value descriptors for each successive
                  entry */
               hash_data_t *data = NULL;
               /* Aliased poitner to node, for freeing buffers after
                  removal from tree */
               struct rbt_node *holder = cursor;
               /* Buffer descriptor for key, as stored */
               hash_buffer_t key;
               /* Buffer descriptor for value, as stored */
               hash_buffer_t val;
               /* Return code from the free function.  Zero on failure */
               int rc = 0;

               RBT_UNLINK(root, cursor);
               data = RBT_OPAQ(holder);

               key = data->buffkey;
               val = data->buffval;

               ReleaseToPool(data, &ht->partitions[index].data_pool);
               ReleaseToPool(holder, &ht->partitions[index].node_pool);
               --ht->partitions[index].count;
               rc = free_func(key, val);

               if (rc == 0) {
                    pthread_rwlock_unlock(&ht->partitions[index].lock);
                    return HASHTABLE_ERROR_DELALL_FAIL;
               }
          }
          pthread_rwlock_unlock(&ht->partitions[index].lock);
     }

     return HASHTABLE_SUCCESS;
} /* HashTable_Delall */

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
HashTable_GetStats(struct hash_table *ht,
                   struct hash_stat *hstat)
{
     size_t i = 0;

     /* Then compute the other values */

     /* A min value hash to be initialized with a huge value */
     hstat->min_rbt_num_node = 1 << 31;

     /* A max value is initialized with 0 */
     hstat->max_rbt_num_node = 0;
     /* And so does the average value */
     hstat->average_rbt_num_node = 0;

     hstat->entries = 0;

     for (i = 0; i < ht->parameter.index_size; i++) {
          if (ht->partitions[i].rbt.rbt_num_node > hstat->max_rbt_num_node)
               hstat->max_rbt_num_node
                    = ht->partitions[i].rbt.rbt_num_node;

          if (ht->partitions[i].rbt.rbt_num_node < hstat->min_rbt_num_node)
               hstat->min_rbt_num_node
                    = ht->partitions[i].rbt.rbt_num_node;

          hstat->average_rbt_num_node
               += ht->partitions[i].rbt.rbt_num_node;

          hstat->entries += ht->partitions[i].count;
     }

     hstat->average_rbt_num_node /= ht->parameter.index_size;
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
HashTable_GetSize(struct hash_table *ht)
{
     uint32_t i = 0;
     size_t nb_entries = 0;

     for (i = 0; i < ht->parameter.index_size; i++)
          nb_entries += ht->partitions[i].count;

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
              struct hash_table *ht)
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
     uint32_t i = 0;
     /* Running count of entries  */
     size_t nb_entries = 0;
     /* Recomputed partitionindex */
     uint32_t index = 0;
     /* Recomputed hash for Red-Black tree*/
     uint64_t rbt_hash = 0;

     LogFullDebug(component,
                  "The hash is partitioned into %d trees",
                  ht->parameter.index_size);

     for (i = 0; i < ht->parameter.index_size; i++) {
          nb_entries += ht->partitions[i].count;
     }

     LogFullDebug(component, "The hash contains %zd entries",
                  nb_entries);

     for (i = 0; i < ht->parameter.index_size; i++) {
          root = &ht->partitions[i].rbt;
          LogFullDebug(component,
                       "The partition in position %"PRIu32
                       "contains: %u entries",
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
 * @brief Set a pair (key,value) into the Hash Table
 *
 * Depending on the value of 'how', this function sets a value into
 * the hash table or tests that the hash table contains that value.
 *
 * This function is deprecated.
 *
 * @param ht [in] The hashtable to test or alter
 * @param key [in] The key to be set
 * @param val [in] The value to be stored
 * @param how [in] A value determining whether this is a test, a set
 *                 with overwrite, or a set without overwrite.
 *
 * @retval HASHTABLE_SUCCESS if successfull.
 */
hash_error_t
HashTable_Test_And_Set(struct hash_table *ht,
                       struct hash_buff *key,
                       struct hash_buff *val,
                       hash_set_how_t how)
{
     /* structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;

     rc = HashTable_GetLatch(ht, key, NULL,
                             (how != HASHTABLE_SET_HOW_TEST_ONLY),
                             &latch);

     if ((rc != HASHTABLE_SUCCESS) &&
         (rc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          return rc;
     }

     if (how == HASHTABLE_SET_HOW_TEST_ONLY) {
          HashTable_ReleaseLatched(ht, &latch);
          return rc;
     }

     /* No point in calling HashTable_SetLatched when we know it will
        error. */

     if ((how == HASHTABLE_SET_HOW_SET_NO_OVERWRITE) &&
         (rc == HASHTABLE_SUCCESS)) {
          HashTable_ReleaseLatched(ht, &latch);
          return HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
     }

     rc = HashTable_SetLatched(ht,
                               key,
                               val,
                               &latch,
                               (how == HASHTABLE_SET_HOW_SET_OVERWRITE),
                               NULL,
                               NULL);

     if (rc == HASHTABLE_OVERWRITTEN) {
          rc = HASHTABLE_SUCCESS;
     }

     return rc;
} /* HashTable_Test_And_Set */

/**
 *
 * @brief Look up a value and take a reference
 *
 * This function attempts to locate a key in the hash store and return
 * the associated value.  It also calls the supplied function to take
 * a reference before releasing the partition lock.  It is implemented
 * as a wrapper around HashTable_GetLatched.
 *
 * @param ht [in] The hash store to be searched
 * @param key [in] A buffer descriptore locating the key to find
 * @param val [out] A buffer descriptor locating the value found
 * @param get_ref [in] A function to take a reference on the supplied
 *                     value
 *
 * @return HASHTABLE_SUCCESS or errors
 */

hash_error_t
HashTable_GetRef(hash_table_t *ht,
                 hash_buffer_t *key,
                 hash_buffer_t *val,
                 void (*get_ref)(hash_buffer_t *))
{
     /* structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;

     rc = HashTable_GetLatch(ht, key, val, FALSE, &latch);

     switch (rc) {
     case HASHTABLE_SUCCESS:
          if (get_ref != NULL) {
               get_ref(val);
          }
     case HASHTABLE_ERROR_NO_SUCH_KEY:
          HashTable_ReleaseLatched(ht, &latch);
          break;

     default:
          break;
     }

     return rc;
} /* HashTable_GetRef */

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

hash_error_t
HashTable_Get_and_Del(hash_table_t  *ht,
                      hash_buffer_t *key,
                      hash_buffer_t *val,
                      hash_buffer_t *stored_key)
{
     /* structure to hold retained state */
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
                                         val);


     case HASHTABLE_ERROR_NO_SUCH_KEY:
          HashTable_ReleaseLatched(ht, &latch);
     default:
          return rc;
     }

} /* HashTable_Get_and_Del */

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

hash_error_t
HashTable_DelRef(hash_table_t *ht,
                 hash_buffer_t *key,
                 hash_buffer_t *stored_key,
                 hash_buffer_t *stored_val,
                 int (*put_ref)(hash_buffer_t *))
{
     /* structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;
     /* Temporary buffer descriptor.  We need the value to call the
        decrement function, even if the caller didn't request the
        value. */
     struct hash_buff temp_val;

     rc = HashTable_GetLatch(ht, key, &temp_val,
                             TRUE, &latch);

     switch (rc) {
     case HASHTABLE_ERROR_NO_SUCH_KEY:
          HashTable_ReleaseLatched(ht, &latch);
          break;

     case HASHTABLE_SUCCESS:
          if (put_ref != NULL) {
               if (put_ref(&temp_val) != 0) {
                    HashTable_ReleaseLatched(ht, &latch);
                    rc = HASHTABLE_NOT_DELETED;
                    goto out;
               }
          }
          rc = HashTable_DeleteLatched(ht,
                                       key,
                                       &latch,
                                       stored_key,
                                       stored_val);
          break;

     default:
          break;
     }

out:

     return rc;
} /* HashTable_DelRef */

/**
 *
 * @brief Remove an entry if key and value both match
 *
 * This function looks up an entry and removes it if both the key and
 * the supplied pointer matches the stored value pointer.
 *
 * @param ht [in] The hashtable to be modified
 * @param key [in] The key corresponding to the entry to delete
 * @param val [in] A pointer, which should match the stored entry's
 *                 value pointer.
 *
 * @retval HASHTABLE_SUCCESS on deletion
 * @retval HASHTABLE_NO_SUCH_KEY if the key was not found or the
 *         values did not match.
 */

hash_error_t
HashTable_DelSafe(hash_table_t *ht,
                  hash_buffer_t *key,
                  hash_buffer_t *val)
{
     /* structure to hold retained state */
     struct hash_latch latch;
     /* Stored return code */
     hash_error_t rc = 0;
     /* Temporary buffer descriptor.  We need the value to call the
        decrement function, even if the caller didn't request the
        value. */
     struct hash_buff found_val;

     rc = HashTable_GetLatch(ht, key, &found_val,
                             TRUE, &latch);

     switch (rc) {
     case HASHTABLE_ERROR_NO_SUCH_KEY:
          HashTable_ReleaseLatched(ht, &latch);
          break;

     case HASHTABLE_SUCCESS:
          if (found_val.pdata == val->pdata) {
               rc = HashTable_DeleteLatched(ht,
                                            key,
                                            &latch,
                                            NULL,
                                            NULL);
          } else {
               rc = HASHTABLE_ERROR_NO_SUCH_KEY;
          }
          break;

     default:
          break;
     }

     return rc;
} /* HashTable_DelSafe */

/* @} */

