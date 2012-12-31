/*
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
 * @defgroup Cache_inode Cache Inode
 * @{
 */

/**
 * @file  cache_inode_misc.c
 * @brief Miscellaneous functions, especially new_entry
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "abstract_atomic.h"
#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "nfs4_acls.h"
#include "sal_functions.h"
#include "nfs_core.h"
#include "nfs_tools.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

cache_inode_gc_policy_t cache_inode_gc_policy = {
        /* Cache inode parameters: Garbage collection policy */
        .entries_hwmark = 100000,
        .entries_lwmark = 50000,
        .use_fd_cache = true,
        .lru_run_interval = 600,
        .fd_limit_percent = 99,
        .fd_hwmark_percent = 90,
        .fd_lwmark_percent = 50,
        .reaper_work = 1000,
        .biggest_window = 40,
        .required_progress = 5,
        .futility_count = 8
};

cache_inode_parameter_t cache_inode_params = {
        /* Cache inode parameters : hash table */
        .hparam.index_size = PRIME_CACHE_INODE,
        .hparam.alphabet_length = 10,
        .hparam.hash_func_both = cache_inode_fsal_rbt_both,
        .hparam.compare_key = cache_inode_compare_key_fsal,
        .hparam.key_to_str = display_cache,
        .hparam.val_to_str = display_cache,
        .hparam.ht_name = "Cache Inode",
        .hparam.flags = HT_FLAG_CACHE,
        .hparam.ht_log_component = COMPONENT_CACHE_INODE,

        /* Cache inode parameters : cookie hash table */
        .cookie_param.index_size = PRIME_STATE_ID,
        .cookie_param.alphabet_length = 10,
        .cookie_param.hash_func_key = lock_cookie_value_hash_func,
        .cookie_param.hash_func_rbt = lock_cookie_rbt_hash_func ,
        .cookie_param.compare_key = compare_lock_cookie_key,
        .cookie_param.key_to_str = display_lock_cookie_key,
        .cookie_param.val_to_str = display_lock_cookie_val,
        .cookie_param.ht_name = "Lock Cookie",
        .cookie_param.flags = HT_FLAG_NONE,
        .cookie_param.ht_log_component = COMPONENT_STATE,

        .expire_type_attr    = CACHE_INODE_EXPIRE_NEVER,
        .expire_type_link    = CACHE_INODE_EXPIRE_NEVER,
        .expire_type_dirent  = CACHE_INODE_EXPIRE_NEVER,
        .use_fsal_hash = 1
};

pool_t *cache_inode_entry_pool;

const char *cache_inode_err_str(cache_inode_status_t err)
{
  switch(err)
    {
      case CACHE_INODE_SUCCESS:
        return "CACHE_INODE_SUCCESS";
      case CACHE_INODE_MALLOC_ERROR:
        return "CACHE_INODE_MALLOC_ERROR";
      case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
        return "CACHE_INODE_POOL_MUTEX_INIT_ERROR";
      case CACHE_INODE_GET_NEW_LRU_ENTRY:
        return "CACHE_INODE_GET_NEW_LRU_ENTRY";
      case CACHE_INODE_UNAPPROPRIATED_KEY:
        return "CACHE_INODE_UNAPPROPRIATED_KEY";
      case CACHE_INODE_INIT_ENTRY_FAILED:
        return "CACHE_INODE_INIT_ENTRY_FAILED";
      case CACHE_INODE_FSAL_ERROR:
        return "CACHE_INODE_FSAL_ERROR";
      case CACHE_INODE_LRU_ERROR:
        return "CACHE_INODE_LRU_ERROR";
      case CACHE_INODE_HASH_SET_ERROR:
        return "CACHE_INODE_HASH_SET_ERROR";
      case CACHE_INODE_NOT_A_DIRECTORY:
        return "CACHE_INODE_NOT_A_DIRECTORY";
      case CACHE_INODE_INCONSISTENT_ENTRY:
        return "CACHE_INODE_INCONSISTENT_ENTRY";
      case CACHE_INODE_BAD_TYPE:
        return "CACHE_INODE_BAD_TYPE";
      case CACHE_INODE_ENTRY_EXISTS:
        return "CACHE_INODE_ENTRY_EXISTS";
      case CACHE_INODE_DIR_NOT_EMPTY:
        return "CACHE_INODE_DIR_NOT_EMPTY";
      case CACHE_INODE_NOT_FOUND:
        return "CACHE_INODE_NOT_FOUND";
      case CACHE_INODE_INVALID_ARGUMENT:
        return "CACHE_INODE_INVALID_ARGUMENT";
      case CACHE_INODE_INSERT_ERROR:
        return "CACHE_INODE_INSERT_ERROR";
      case CACHE_INODE_HASH_TABLE_ERROR:
        return "CACHE_INODE_HASH_TABLE_ERROR";
      case CACHE_INODE_FSAL_EACCESS:
        return "CACHE_INODE_FSAL_EACCESS";
      case CACHE_INODE_IS_A_DIRECTORY:
        return "CACHE_INODE_IS_A_DIRECTORY";
      case CACHE_INODE_FSAL_EPERM:
        return "CACHE_INODE_FSAL_EPERM";
      case CACHE_INODE_NO_SPACE_LEFT:
        return "CACHE_INODE_NO_SPACE_LEFT";
      case CACHE_INODE_READ_ONLY_FS:
        return "CACHE_INODE_READ_ONLY_FS";
      case CACHE_INODE_IO_ERROR:
        return "CACHE_INODE_IO_ERROR";
      case CACHE_INODE_FSAL_ESTALE:
        return "CACHE_INODE_FSAL_ESTALE";
      case CACHE_INODE_FSAL_ERR_SEC:
        return "CACHE_INODE_FSAL_ERR_SEC";
      case CACHE_INODE_STATE_CONFLICT:
        return "CACHE_INODE_STATE_CONFLICT";
      case CACHE_INODE_QUOTA_EXCEEDED:
        return "CACHE_INODE_QUOTA_EXCEEDED";
      case CACHE_INODE_DEAD_ENTRY:
        return "CACHE_INODE_DEAD_ENTRY";
      case CACHE_INODE_ASYNC_POST_ERROR:
        return "CACHE_INODE_ASYNC_POST_ERROR";
      case CACHE_INODE_NOT_SUPPORTED:
        return "CACHE_INODE_NOT_SUPPORTED";
      case CACHE_INODE_STATE_ERROR:
        return "CACHE_INODE_STATE_ERROR";
      case CACHE_INODE_DELAY:
        return "CACHE_INODE_FSAL_DELAY";
      case CACHE_INODE_NAME_TOO_LONG:
        return "CACHE_INODE_NAME_TOO_LONG";
      case CACHE_INODE_BAD_COOKIE:
        return "CACHE_INODE_BAD_COOKIE";
      case CACHE_INODE_FILE_BIG:
        return "CACHE_INODE_FILE_BIG";
      case CACHE_INODE_KILLED:
        return "CACHE_INODE_KILLED";
      case CACHE_INODE_FILE_OPEN:
        return "CACHE_INODE_FILE_OPEN";
      case CACHE_INODE_FSAL_XDEV:
	return "CACHE_INOE_FSAL_XDEV";
      case CACHE_INODE_FSAL_MLINK:
	return "CACHE_INOE_FSAL_MLINK";
    }
  return "unknown";
}

/**
 *
 * @brief Compares two keys used in cache inode
 *
 * Compare two keys used in cache inode. These keys are basically made from FSAL
 * related information.
 *
 * @param[in] buff1 First key
 * @param[in] buff2 Second key
 * @return 0 if keys are the same,
 *        -1 if first is greater/larger
 *         1 if second is greater/larger
 *
 * @see FSAL_handlecmp
 *
 */
int cache_inode_compare_key_fsal(struct gsh_buffdesc *buff1,
                                 struct gsh_buffdesc *buff2)
{
  /* Test if one of the entries is NULL */
  if(buff1->addr == NULL)
    return (buff2->addr == NULL) ? 0 : 1;
  else
    {
      if(buff2->addr == NULL) {
        return -1;              /* left member is the greater one */
      }
      if(buff1->len == buff2->len)
	      return memcmp(buff1->addr, buff2->addr, buff1->len);
      else
	      return (buff1->len > buff2->len) ? -1 : 1;
    }
  /* This line should never be reached */
} /* cache_inode_compare_key_fsal */


/**
 *
 * @brief Set the fsal_time in a pentry struct to the current time.
 *
 * Sets the fsal_time in a pentry struct to the current time. This
 * function is using gettimeofday.
 *
 * @param[out] time Pointer to time to be set
 *
 * @return 0 if keys if successfully build, -1 otherwise
 *
 */
int cache_inode_set_time_current(gsh_time_t *time)
{
  struct timeval t;

  if (time == NULL)
    return -1;

  if (gettimeofday(&t, NULL) != 0)
    return -1;

  time->seconds  = t.tv_sec;
  time->nseconds = 1000*t.tv_usec;

  return 0;
} /* cache_inode_set_time_current */


/**
 * @brief Adds a new entry to the cache
 *
 * This funcion adds a new entry to the cache.  It will allocate
 * entries of any kind.
 *
 * @param[in]  new_obj Object handle to be added to the cache
 * @param[in]  flags   Vary the function's operation
 * @param[out] entry   Newly instantiated cache entry
 *
 * @return CACHE_INODE_SUCCESS or errors.
 */
cache_inode_status_t
cache_inode_new_entry(struct fsal_obj_handle *new_obj,
                      uint32_t flags,
                      cache_entry_t **entry)
{
     cache_inode_status_t status;
     cache_entry_t *new_entry = NULL;
     struct gsh_buffdesc key, value;
     int rc = 0;
     bool lrurefed = false;
     bool weakrefed = false;
     bool locksinited = false;
     bool latched = false;
     struct hash_latch latch;
     hash_error_t hrc = 0;
     struct gsh_buffdesc fh_desc;
     fsal_status_t fsal_status;

     new_obj->ops->handle_to_key(new_obj, &fh_desc);
     key.addr = fh_desc.addr;
     key.len = fh_desc.len;

     /* Check if the entry doesn't already exists */
     /* This is slightly ugly, since we make two tries in the event
        that the lru reference fails. */
     hrc = HashTable_GetLatch(fh_to_cache_entry_ht, &key, &value,
                              true, &latch);
     if ((hrc != HASHTABLE_SUCCESS) && (hrc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          status = CACHE_INODE_HASH_TABLE_ERROR;
          LogCrit(COMPONENT_CACHE_INODE, "Hash access failed with code %d"
                  " - this should not have happened", hrc);
          goto out;
     }
     if (hrc == HASHTABLE_SUCCESS) {
          /* Entry is already in the cache, do not add it */
          *entry = value.addr;
          status = CACHE_INODE_ENTRY_EXISTS;
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Trying to add an already existing "
                   "entry 1. Found entry %p type: %d, New type: %d",
                   *entry, (*entry)->type, new_obj->type);
          if (cache_inode_lru_ref(*entry, LRU_FLAG_NONE) ==
              CACHE_INODE_SUCCESS) {
               /* Release the subtree hash table mutex acquired in
                  HashTable_GetEx */
               HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
               goto out;
          } else {
               /* Entry is being deconstructed, so just replace it. */
               (*entry) = NULL;
               status = CACHE_INODE_SUCCESS;
          }
     }
     /* We did not find the object; we need to get a new one.
        Let us drop the latch and reacquire before inserting it. */

     HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);

     /* Pull an entry off the LRU */
     status = cache_inode_lru_get(&new_entry, 0);
     if (new_entry == NULL) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: cache_inode_lru_get failed");
          status = CACHE_INODE_MALLOC_ERROR;
          goto out;
     }
     assert(new_entry->lru.refcount > 1);
     /* Now we got the entry; get the latch and see if someone raced us. */
     hrc = HashTable_GetLatch(fh_to_cache_entry_ht, &key, &value,
                              true, &latch);
     if ((hrc != HASHTABLE_SUCCESS) && (hrc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          status = CACHE_INODE_HASH_TABLE_ERROR;
          LogCrit(COMPONENT_CACHE_INODE, "Hash access failed with code %d"
                  " - this should not have happened", hrc);
          /* Release our reference and the sentinel on the entry we
             acquired. */
          cache_inode_lru_kill(new_entry);
          cache_inode_lru_unref(new_entry, LRU_FLAG_NONE);
          goto out;
     }
     if (hrc == HASHTABLE_SUCCESS) {
          /* Entry is already in the cache, do not add it */
          *entry = value.addr;
          status = CACHE_INODE_ENTRY_EXISTS;
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Trying to add an already existing "
                   "entry 2. Found entry %p type: %d, New type: %d",
                   *entry, (*entry)->obj_handle->type, new_obj->type);
          if (cache_inode_lru_ref(*entry, LRU_FLAG_NONE) ==
              CACHE_INODE_SUCCESS) {
               /* Release the subtree hash table mutex acquired in
                  HashTable_GetEx */
               HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
               /* Release the new entry we acquired. */
               cache_inode_lru_kill(new_entry);
               cache_inode_lru_unref(new_entry, LRU_FLAG_NONE);
               goto out;
          }
     }
     *entry = new_entry;
     latched = true;
     /* This should be the sentinel, plus one to use the entry we
        just returned. */
     lrurefed = true;

     /* Enroll the object in the weakref table */

     (*entry)->weakref =
          cache_inode_weakref_insert(*entry);
     assert((*entry)->weakref.ptr != 0); /* A NULL pointer here would
					    indicate a programming
					    error, such as an old entry
					    not being unenrolled from
					    the table. */
     weakrefed = true;

     /* Initialize the entry locks */
     if (((rc = pthread_rwlock_init(&(*entry)->attr_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&(*entry)->content_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&(*entry)->state_lock, NULL)) != 0)) {
          /* Recycle */
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: pthread_rwlock_init "
                  "returned %d (%s)", rc, strerror(rc));
          status = CACHE_INODE_INIT_ENTRY_FAILED;
          goto out;
     }
     locksinited = true;

     /* Initialize common fields */

     (*entry)->type = new_obj->type;
     (*entry)->flags = 0;
     init_glist(&(*entry)->state_list);
     init_glist(&(*entry)->layoutrecall_list);

     switch ((*entry)->type) {
     case REGULAR_FILE:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a REGULAR_FILE, entry=%p",
                   entry);

          /* No locks, yet. */
          init_glist(&(*entry)->object.file.lock_list);
          init_glist(&(*entry)->object.file.nlm_share_list);   /* No associated NLM shares yet */

          memset(&((*entry)->object.file.unstable_data), 0,
                 sizeof(cache_inode_unstable_data_t));
          memset(&(*entry)->object.file.share_state, 0,
		 sizeof(cache_inode_share_t));
     break;
     
     case DIRECTORY:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a DIRECTORY, entry=%p",
                   entry);

          /* If the directory is newly created, it is empty.  Because
             we know its content, we consider it read. */
          if (flags & CACHE_INODE_FLAG_CREATE) {
               atomic_set_uint32_t_bits(&(*entry)->flags,
				       CACHE_INODE_TRUST_CONTENT |
				       CACHE_INODE_DIR_POPULATED);
          } else {
               atomic_clear_uint32_t_bits(&(*entry)->flags,
                                          CACHE_INODE_TRUST_CONTENT |
                                          CACHE_INODE_DIR_POPULATED);
          }
	  
          (*entry)->object.dir.avl.collisions = 0;
          (*entry)->object.dir.nbactive = 0;
          (*entry)->object.dir.referral = NULL;
          (*entry)->object.dir.parent.ptr = NULL;
          (*entry)->object.dir.parent.gen = 0;
          (*entry)->object.dir.root = false;
          /* init avl tree */
          cache_inode_avl_init(*entry);
          break;

     case SYMBOLIC_LINK:
     case SOCKET_FILE:
     case FIFO_FILE:
     case BLOCK_FILE:
     case CHARACTER_FILE:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a special file of type %d "
                   "entry=%p", (*entry)->type, *entry);
          break;

     case FS_JUNCTION:
          /* I don't think this ever actually gets called */
          abort();
          break;

     default:
          /* Should never happen */
          status = CACHE_INODE_INCONSISTENT_ENTRY;
          LogMajor(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: unknown type %u provided",
                   (*entry)->type);
          goto out;
     }

     (*entry)->obj_handle = new_obj;
     new_obj = NULL; /* mark it as having a home */
     cache_inode_fixup_md(*entry);

     /* Adding the entry in the hash table using the key we started with */

     value.addr = *entry;
     value.len = sizeof(cache_entry_t);

     rc = HashTable_SetLatched(fh_to_cache_entry_ht, &key, &value,
                               &latch, true, NULL, NULL);
     /* HashTable_SetLatched release the latch irrespective
      * of success/failure. */
     latched = false;
     if ((rc != HASHTABLE_SUCCESS) && (rc != HASHTABLE_OVERWRITTEN)) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: entry could not be added to hash, "
                  "rc=%d", rc);
          new_obj = (*entry)->obj_handle;
          (*entry)->obj_handle = NULL;  /* give it back and poison the entry */
          status = CACHE_INODE_HASH_SET_ERROR;
          goto out;
     }

     LogDebug(COMPONENT_CACHE_INODE,
              "cache_inode_new_entry: New entry %p added", entry);
     status = CACHE_INODE_SUCCESS;

out:
     if (status != CACHE_INODE_SUCCESS) {
          /* Deconstruct the object */
          if (locksinited) {
               pthread_rwlock_destroy(&(*entry)->attr_lock);
               pthread_rwlock_destroy(&(*entry)->content_lock);
               pthread_rwlock_destroy(&(*entry)->state_lock);
          }
          if (weakrefed) {
               cache_inode_weakref_delete(&(*entry)->weakref);
          }
          if (latched) {
               HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
          }
          if (lrurefed && status != CACHE_INODE_ENTRY_EXISTS) {
               cache_inode_lru_unref(*entry, LRU_FLAG_NONE);
          }
          if (status != CACHE_INODE_ENTRY_EXISTS) {
               *entry = NULL;
          }
     }

/* must free new_obj if no new entry was created to reference it. */
     if(new_obj != NULL) {
	  fsal_status = new_obj->ops->release(new_obj);
	  if (FSAL_IS_ERROR(fsal_status))
	    {
	      status = cache_inode_error_convert(fsal_status);
	      LogDebug(COMPONENT_CACHE_INODE,
		       "failed to release unused new_obj %p", new_obj);
	      /* further recovery ?? */
	    }
     }

     return status;
}                               /* cache_inode_new_entry */

/**
 * @brief Final cleaning of an entry
 *
 * This function performs final cleanup of an entry before recycling or free.
 *
 * @param[in] entry The entry to be cleaned
 */
void cache_inode_clean_entry(cache_entry_t *entry)
{
  pthread_rwlock_destroy(&entry->content_lock);
  pthread_rwlock_destroy(&entry->state_lock);
  pthread_rwlock_destroy(&entry->attr_lock);
}

/**
 * @brief Converts an FSAL error to the corresponding cache_inode error
 *
 * This function converts an FSAL error to the corresponding
 * cache_inode error.
 *
 * @param[in] fsal_status FSAL error to be converted
 *
 * @return the result of the conversion.
 *
 */
cache_inode_status_t
cache_inode_error_convert(fsal_status_t fsal_status)
{
  switch (fsal_status.major)
    {
    case ERR_FSAL_NO_ERROR:
      return CACHE_INODE_SUCCESS;

    case ERR_FSAL_NOENT:
      return CACHE_INODE_NOT_FOUND;

    case ERR_FSAL_EXIST:
      return CACHE_INODE_ENTRY_EXISTS;

    case ERR_FSAL_ACCESS:
      return CACHE_INODE_FSAL_EACCESS;

    case ERR_FSAL_PERM:
      return CACHE_INODE_FSAL_EPERM;

    case ERR_FSAL_NOSPC:
      return CACHE_INODE_NO_SPACE_LEFT;

    case ERR_FSAL_NOTEMPTY:
      return CACHE_INODE_DIR_NOT_EMPTY;

    case ERR_FSAL_ROFS:
      return CACHE_INODE_READ_ONLY_FS;

    case ERR_FSAL_NOTDIR:
      return CACHE_INODE_NOT_A_DIRECTORY;

    case ERR_FSAL_IO:
    case ERR_FSAL_NXIO:
      return CACHE_INODE_IO_ERROR;

    case ERR_FSAL_STALE:
    case ERR_FSAL_BADHANDLE:
    case ERR_FSAL_FHEXPIRED:
      return CACHE_INODE_FSAL_ESTALE;

    case ERR_FSAL_INVAL:
    case ERR_FSAL_OVERFLOW:
      return CACHE_INODE_INVALID_ARGUMENT;

    case ERR_FSAL_DQUOT:
      return CACHE_INODE_QUOTA_EXCEEDED;

    case ERR_FSAL_SEC:
      return CACHE_INODE_FSAL_ERR_SEC;

    case ERR_FSAL_NOTSUPP:
    case ERR_FSAL_ATTRNOTSUPP:
      return CACHE_INODE_NOT_SUPPORTED;

    case ERR_FSAL_DELAY:
      return CACHE_INODE_DELAY;

    case ERR_FSAL_NAMETOOLONG:
      return CACHE_INODE_NAME_TOO_LONG;

    case ERR_FSAL_NOMEM:
      return CACHE_INODE_MALLOC_ERROR;

    case ERR_FSAL_BADCOOKIE:
      return CACHE_INODE_BAD_COOKIE;

    case ERR_FSAL_FILE_OPEN:
      return CACHE_INODE_FILE_OPEN;

    case ERR_FSAL_NOT_OPENED:
      LogDebug(COMPONENT_CACHE_INODE,
               "Conversion of ERR_FSAL_NOT_OPENED to CACHE_INODE_FSAL_ERROR");
      return CACHE_INODE_FSAL_ERROR;

    case ERR_FSAL_SYMLINK:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_BADTYPE:
      return CACHE_INODE_BAD_TYPE;

    case ERR_FSAL_FBIG:
      return CACHE_INODE_FILE_BIG;

    case ERR_FSAL_XDEV:
      return CACHE_INODE_FSAL_XDEV ;

    case ERR_FSAL_MLINK:
      return CACHE_INODE_FSAL_MLINK ;

    case ERR_FSAL_DEADLOCK:
    case ERR_FSAL_BLOCKED:
    case ERR_FSAL_INTERRUPT:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_NOT_INIT:
    case ERR_FSAL_ALREADY_INIT:
    case ERR_FSAL_BAD_INIT:
    case ERR_FSAL_NO_QUOTA:
    case ERR_FSAL_TOOSMALL:
    case ERR_FSAL_TIMEOUT:
    case ERR_FSAL_SERVERFAULT:
      /* These errors should be handled inside Cache Inode (or should never be seen by Cache Inode) */
      LogDebug(COMPONENT_CACHE_INODE,
               "Conversion of FSAL error %d,%d to CACHE_INODE_FSAL_ERROR",
               fsal_status.major, fsal_status.minor);
      return CACHE_INODE_FSAL_ERROR;
    }

  /* We should never reach this line, this may produce a warning with certain compiler */
  LogCrit(COMPONENT_CACHE_INODE,
          "cache_inode_error_convert: default conversion to "
          "CACHE_INODE_FSAL_ERROR for error %d, line %u should never be reached",
          fsal_status.major, __LINE__);
  return CACHE_INODE_FSAL_ERROR;
}

/**
 *
 * @brief Prints the content of a directory
 *
 * This debugging function prints the contents of a directory.
 *
 * @param[in] entry the input pentry.
 *
 */
void cache_inode_print_dir(cache_entry_t *entry)
{
  struct avltree_node *dirent_node;
  cache_inode_dir_entry_t *dirent;
  int i = 0;

  if(entry->type != DIRECTORY)
    {
      LogDebug(COMPONENT_CACHE_INODE,
               "This entry is not a directory");
      return;
    }

  dirent_node = avltree_first(&entry->object.dir.avl.t);
  do {
      dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
                                    node_hk);
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "Name = %s, DIRECTORY entry = (%p, %"PRIu64") i=%d",
                   dirent->name,
                   dirent->entry.ptr,
                   dirent->entry.gen,
                   i);
      i++;
  } while ((dirent_node = avltree_next(dirent_node)));

  LogFullDebug(COMPONENT_CACHE_INODE, "------------------");
} /* cache_inode_print_dir */

/**
 * cache_inode_release_dirents: release cached dirents associated
 * with an entry.
 *
 * releases dirents associated with pentry.  this is simple, but maybe
 * should be abstracted.
 *
 * @param[in] entry Directory to have entries be released
 * @param[in] which Caches to clear (dense, sparse, or both)
 *
 */
void cache_inode_release_dirents(cache_entry_t *entry,
                                 cache_inode_avl_which_t which)
{
    struct avltree_node *dirent_node = NULL;
    struct avltree_node *next_dirent_node = NULL;
    struct avltree *tree = NULL;
    cache_inode_dir_entry_t *dirent = NULL;

    /* Won't see this */
    if (entry->type != DIRECTORY)
        return;

    switch (which)
    {
    case CACHE_INODE_AVL_NAMES:
        tree = &entry->object.dir.avl.t;
        break;

    case CACHE_INODE_AVL_COOKIES:
        tree = &entry->object.dir.avl.c;
        break;

    case CACHE_INODE_AVL_BOTH:
        cache_inode_release_dirents(entry, CACHE_INODE_AVL_NAMES);
        cache_inode_release_dirents(entry, CACHE_INODE_AVL_COOKIES);
        /* tree == NULL */
        break;

    default:
        /* tree == NULL */
        break;
    }

    if (tree) {
          dirent_node = avltree_first(tree);

          while( dirent_node )
           {
             next_dirent_node = avltree_next(dirent_node);
             dirent = avltree_container_of(dirent_node,
                                           cache_inode_dir_entry_t,
                                           node_hk);
             avltree_remove(dirent_node, tree);
             gsh_free(dirent);
             dirent_node = next_dirent_node;
           }

          if (tree == &entry->object.dir.avl.t) {
              entry->object.dir.nbactive = 0;
              atomic_clear_uint32_t_bits(&entry->flags,
                                         (CACHE_INODE_TRUST_CONTENT |
                                          CACHE_INODE_DIR_POPULATED));
          }
    }
}

/**
 * @brief Conditionally refresh attributes
 *
 * This function tests whether we should still trust the current
 * attributes and, if not, refresh them.
 *
 * @param[in] entry   The entry to refresh
 * @param[in] req_ctx Request context
 *
 * @return CACHE_INODE_SUCCESS or other status codes.
 */

cache_inode_status_t
cache_inode_check_trust(cache_entry_t *entry,
                        const struct req_op_context *req_ctx)
{
     time_t current_time = 0;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     time_t oldmtime = 0;

     if (entry->type == FS_JUNCTION) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_check_attrs called on file %p of bad type %d",
                  entry, entry->type);

          status = CACHE_INODE_BAD_TYPE;
          goto out;
        }

     PTHREAD_RWLOCK_rdlock(&entry->attr_lock);
     current_time = time(NULL);

     oldmtime = entry->obj_handle->attributes.mtime.seconds;

     /* Do we need a refresh? */
     if (((cache_inode_params.expire_type_attr == CACHE_INODE_EXPIRE_NEVER) ||
          (current_time - entry->attr_time <
           cache_inode_params.grace_period_attr)) &&
         (entry->flags & CACHE_INODE_TRUST_ATTRS) &&
         !((cache_inode_params.getattr_dir_invalidation)&&
           (entry->type == DIRECTORY))) {
          goto unlock;
     }

     PTHREAD_RWLOCK_unlock(&entry->attr_lock);

     /* Update the atributes */
     PTHREAD_RWLOCK_wrlock(&entry->attr_lock);
     current_time = time(NULL);

     /* Make sure no one else has first */
     if (((cache_inode_params.expire_type_attr == CACHE_INODE_EXPIRE_NEVER) ||
          (current_time - entry->attr_time <
           cache_inode_params.grace_period_attr)) &&
         (entry->flags & CACHE_INODE_TRUST_ATTRS) &&
         !((cache_inode_params.getattr_dir_invalidation) &&
           (entry->type == DIRECTORY))) {
          goto unlock;
     }

     if ((status = cache_inode_refresh_attrs(entry, req_ctx))
         != CACHE_INODE_SUCCESS) {
          goto unlock;
     }

     if ((entry->type == DIRECTORY) &&
                (oldmtime < entry->obj_handle->attributes.mtime.seconds)) {
          PTHREAD_RWLOCK_wrlock(&entry->content_lock);
          PTHREAD_RWLOCK_unlock(&entry->attr_lock);

          atomic_clear_uint32_t_bits(&entry->flags, CACHE_INODE_TRUST_CONTENT |
                                     CACHE_INODE_DIR_POPULATED);

          status = cache_inode_invalidate_all_cached_dirent(entry);
          if (status != CACHE_INODE_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE,
                       "cache_inode_invalidate_all_cached_dirent "
                       "returned %d (%s)", status,
                       cache_inode_err_str(status));
          }

          PTHREAD_RWLOCK_unlock(&entry->content_lock);
          goto out;
     }

unlock:

     PTHREAD_RWLOCK_unlock(&entry->attr_lock);

out:
     return status;
}
/** @} */
