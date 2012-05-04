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
 * @file    cache_inode_misc.c
 * @brief   routines for management of the cache_inode layer,
 *
 * Some routines for management of the cache_inode layer, shared by
 * other calls.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "stuff_alloc.h"
#include "nfs4_acls.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

cache_inode_gc_policy_t cache_inode_gc_policy;

const char *cache_inode_err_str(cache_inode_status_t err)
{
  switch(err)
    {
      case CACHE_INODE_SUCCESS:               return "CACHE_INODE_SUCCESS";
      case CACHE_INODE_MALLOC_ERROR:          return "CACHE_INODE_MALLOC_ERROR";
      case CACHE_INODE_POOL_MUTEX_INIT_ERROR: return "CACHE_INODE_POOL_MUTEX_INIT_ERROR";
      case CACHE_INODE_GET_NEW_LRU_ENTRY:     return "CACHE_INODE_GET_NEW_LRU_ENTRY";
      case CACHE_INODE_UNAPPROPRIATED_KEY:    return "CACHE_INODE_UNAPPROPRIATED_KEY";
      case CACHE_INODE_INIT_ENTRY_FAILED:     return "CACHE_INODE_INIT_ENTRY_FAILED";
      case CACHE_INODE_FSAL_ERROR:            return "CACHE_INODE_FSAL_ERROR";
      case CACHE_INODE_LRU_ERROR:             return "CACHE_INODE_LRU_ERROR";
      case CACHE_INODE_HASH_SET_ERROR:        return "CACHE_INODE_HASH_SET_ERROR";
      case CACHE_INODE_NOT_A_DIRECTORY:       return "CACHE_INODE_NOT_A_DIRECTORY";
      case CACHE_INODE_INCONSISTENT_ENTRY:    return "CACHE_INODE_INCONSISTENT_ENTRY";
      case CACHE_INODE_BAD_TYPE:              return "CACHE_INODE_BAD_TYPE";
      case CACHE_INODE_ENTRY_EXISTS:          return "CACHE_INODE_ENTRY_EXISTS";
      case CACHE_INODE_DIR_NOT_EMPTY:         return "CACHE_INODE_DIR_NOT_EMPTY";
      case CACHE_INODE_NOT_FOUND:             return "CACHE_INODE_NOT_FOUND";
      case CACHE_INODE_INVALID_ARGUMENT:      return "CACHE_INODE_INVALID_ARGUMENT";
      case CACHE_INODE_INSERT_ERROR:          return "CACHE_INODE_INSERT_ERROR";
      case CACHE_INODE_HASH_TABLE_ERROR:      return "CACHE_INODE_HASH_TABLE_ERROR";
      case CACHE_INODE_FSAL_EACCESS:          return "CACHE_INODE_FSAL_EACCESS";
      case CACHE_INODE_IS_A_DIRECTORY:        return "CACHE_INODE_IS_A_DIRECTORY";
      case CACHE_INODE_FSAL_EPERM:            return "CACHE_INODE_FSAL_EPERM";
      case CACHE_INODE_NO_SPACE_LEFT:         return "CACHE_INODE_NO_SPACE_LEFT";
      case CACHE_INODE_CACHE_CONTENT_ERROR:   return "CACHE_INODE_CACHE_CONTENT_ERROR";
      case CACHE_INODE_CACHE_CONTENT_EXISTS:  return "CACHE_INODE_CACHE_CONTENT_EXISTS";
      case CACHE_INODE_CACHE_CONTENT_EMPTY:   return "CACHE_INODE_CACHE_CONTENT_EMPTY";
      case CACHE_INODE_READ_ONLY_FS:          return "CACHE_INODE_READ_ONLY_FS";
      case CACHE_INODE_IO_ERROR:              return "CACHE_INODE_IO_ERROR";
      case CACHE_INODE_FSAL_ESTALE:           return "CACHE_INODE_FSAL_ESTALE";
      case CACHE_INODE_FSAL_ERR_SEC:          return "CACHE_INODE_FSAL_ERR_SEC";
      case CACHE_INODE_STATE_CONFLICT:        return "CACHE_INODE_STATE_CONFLICT";
      case CACHE_INODE_QUOTA_EXCEEDED:        return "CACHE_INODE_QUOTA_EXCEEDED";
      case CACHE_INODE_DEAD_ENTRY:            return "CACHE_INODE_DEAD_ENTRY";
      case CACHE_INODE_ASYNC_POST_ERROR:      return "CACHE_INODE_ASYNC_POST_ERROR";
      case CACHE_INODE_NOT_SUPPORTED:         return "CACHE_INODE_NOT_SUPPORTED";
      case CACHE_INODE_STATE_ERROR:           return "CACHE_INODE_STATE_ERROR";
      case CACHE_INODE_DELAY:                 return "CACHE_INODE_FSAL_DELAY";
      case CACHE_INODE_NAME_TOO_LONG:         return "CACHE_INODE_NAME_TOO_LONG";
      case CACHE_INODE_BAD_COOKIE:            return "CACHE_INODE_BAD_COOKIE";
      case CACHE_INODE_FILE_BIG:              return "CACHE_INODE_FILE_BIG";
      case CACHE_INODE_KILLED:                return "CACHE_INODE_KILLED";
    }
  return "unknown";
}

/**
 *
 * cache_inode_compare_key_fsal: Compares two keys used in cache inode.
 *
 * Compare two keys used in cache inode. These keys are basically made from FSAL
 * related information.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 * @return 0 if keys are the same, 1 otherwise
 *
 * @see FSAL_handlecmp
 *
 */
int cache_inode_compare_key_fsal(hash_buffer_t *buff1, hash_buffer_t *buff2)
{
  /* Test if one of the entries is NULL */
  if(buff1->pdata == NULL)
    return (buff2->pdata == NULL) ? 0 : 1;
  else
    {
      if(buff2->pdata == NULL) {
        return -1;              /* left member is the greater one */
      }
      int rc;

      rc = (((buff1->len == buff2->len) &&
             (memcmp(buff1->pdata, buff2->pdata, buff1->len) == 0)) ?
            0 :
            1);

      return rc;
    }
  /* This line should never be reached */
}                               /* cache_inode_compare_key_fsal */


/**
 *
 * @brief Set the fsal_time in a pentry struct to the current time.
 *
 * Sets the fsal_time in a pentry struct to the current time. This
 * function is using gettimeofday.
 *
 * @param ptime [OUT] pointer to time to be set
 *
 * @return 0 if keys if successfully build, -1 otherwise
 *
 */
int cache_inode_set_time_current(fsal_time_t *ptime)
{
  struct timeval t ;

  if (ptime == NULL)
    return -1;

  if (gettimeofday(&t, NULL) != 0)
    return -1;

  ptime->seconds  = t.tv_sec;
  ptime->nseconds = 1000*t.tv_usec;

  return 0;
} /* cache_inode_set_time_current */


/**
 *
 * @brief Adds a new entry to the cache
 *
 * This funcion adds a new entry to the cache.  It will allocate
 * entries of any kind.
 *
 * @param fsdata [IN] FSAL data for the entry to be created
 * @param attr [IN] Attributes to be stored in the cache entry
 *                  (must not be NULL)
 * @param type [IN] Type of entry to create
 * @param create_arg [IN] Type specific creation data
 * @param client [IN,OUT] Structure for per-thread resource management
 * @param context [IN] FSAL credentials
 * @param flags [IN] Vary the function's operation (newly created
 *                   object, extra ref, etc.)
 * @param status [OUT] Returned status.
 *
 * @return the same as *status
 *
 */
cache_entry_t *
cache_inode_new_entry(cache_inode_fsal_data_t *fsdata,
                      fsal_attrib_list_t *attr,
                      cache_inode_file_type_t type,
                      cache_inode_create_arg_t *create_arg,
                      cache_inode_client_t *client,
                      fsal_op_context_t *context,
                      uint32_t flags,
                      cache_inode_status_t *status)
{
     cache_entry_t *entry = NULL;
     hash_buffer_t key, value;
     int rc = 0;
     bool_t lrurefed = FALSE;
     bool_t weakrefed = FALSE;
     bool_t locksinited = FALSE;
     bool_t typespec = FALSE;
     bool_t latched = FALSE;
     struct hash_latch latch;
     hash_error_t hrc = 0;

     assert(attr);

     key.pdata = fsdata->fh_desc.start;
     key.len = fsdata->fh_desc.len;

     /* Check if the entry doesn't already exists */
     /* This is slightly ugly, since we make two tries in the event
        that the lru reference fails. */
     hrc = HashTable_GetLatch(fh_to_cache_entry_ht, &key, &value,
                              TRUE, &latch);
     if ((hrc != HASHTABLE_SUCCESS) && (hrc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          *status = CACHE_INODE_HASH_TABLE_ERROR;
          LogCrit(COMPONENT_CACHE_INODE, "Hash access failed with code %d"
                  " - this should not have happened", hrc);
          goto out;
     }
     if (hrc == HASHTABLE_SUCCESS) {
          /* Entry is already in the cache, do not add it */
          entry = value.pdata;
          *status = CACHE_INODE_ENTRY_EXISTS;
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Trying to add an already existing "
                   "entry. Found entry %p type: %d, New type: %d",
                   entry, entry->type, type);
          if (cache_inode_lru_ref(entry, client, LRU_FLAG_NONE) ==
              CACHE_INODE_SUCCESS) {
               /* Release the subtree hash table mutex acquired in
                  HashTable_GetEx */
               HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
               goto out;
          } else {
               /* Entry is being deconstructed, so just replace it. */
               entry = NULL;
               *status = CACHE_INODE_SUCCESS;
          }
     }
     /* We hold the latch from this point, doing all initialization
        that does not require I/O, inserting the entry and releasing
        the latch before we continue. */
     latched = TRUE;

     /* Pull an entry off the LRU */
     entry = cache_inode_lru_get(client, status, 0);
     if (entry == NULL) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: cache_inode_lru_get failed");
          *status = CACHE_INODE_MALLOC_ERROR;
          goto out;
     }
     /* This should be the sentinel, plus one to use the entry we
        just returned. */
     lrurefed = TRUE;

     memset(&entry->handle, 0, sizeof(entry->handle));
     memcpy(&entry->handle,
            fsdata->fh_desc.start,
            fsdata->fh_desc.len);
     entry->fh_desc.start = (caddr_t) &entry->handle;
     entry->fh_desc.len = fsdata->fh_desc.len;

     /* Enroll the object in the weakref table */

     entry->weakref =
          cache_inode_weakref_insert(entry);
     assert(entry->weakref.ptr != 0); /* A NULL pointer here would
                                         indicate a programming
                                         error, such as an old entry
                                         not being unenrolled from
                                         the table. */
     weakrefed = TRUE;

     /* Initialize the entry locks */
     if (((rc = pthread_rwlock_init(&entry->attr_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&entry->content_lock, NULL)) != 0) ||
         ((rc = pthread_rwlock_init(&entry->state_lock, NULL)) != 0)) {
          /* Recycle */
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: pthread_rwlock_init "
                  "returned %d (%s)", rc, strerror(rc));
          *status = CACHE_INODE_INIT_ENTRY_FAILED;
          goto out;
     }
     locksinited = TRUE;

     /* Initialize common fields */

     entry->type = type;
     entry->flags = 0;
     init_glist(&entry->state_list);

     switch (type) {
     case REGULAR_FILE:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a REGULAR_FILE, entry=%p",
                   entry);

          /* No locks, yet. */
          init_glist(&entry->object.file.lock_list);

          entry->object.file.open_fd.openflags = FSAL_O_CLOSED;
          memset(&(entry->object.file.open_fd.fd), 0, sizeof(fsal_file_t));
          memset(&(entry->object.file.unstable_data), 0,
                 sizeof(cache_inode_unstable_data_t));
          memset(&(entry->object.file.share_state), 0,
                 sizeof(cache_inode_share_t));
          break;

     case DIRECTORY:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a DIRECTORY, entry=%p",
                   entry);

          /* If the directory is newly created, it is empty.  Because
             we know its content, we consider it read. */
          if ((create_arg != NULL) &&
              (create_arg->newly_created_dir)) {
               atomic_set_int_bits(&entry->flags,
                                   CACHE_INODE_TRUST_CONTENT |
                                   CACHE_INODE_DIR_POPULATED);
          } else {
               atomic_clear_int_bits(&entry->flags,
                                     CACHE_INODE_TRUST_CONTENT |
                                     CACHE_INODE_DIR_POPULATED);
          }

          entry->object.dir.avl.collisions = 0;
          entry->object.dir.nbactive = 0;
          entry->object.dir.referral = NULL;
          entry->object.dir.parent.ptr = NULL;
          entry->object.dir.parent.gen = 0;
          entry->object.dir.root = FALSE;
          /* init avl tree */
          cache_inode_avl_init(entry);
          break;
     case SYMBOLIC_LINK:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a SYMBOLIC_LINK pentry=%p ",
                   entry);
          GetFromPool(entry->object.symlink, &client->pool_entry_symlink,
                      cache_inode_symlink_t);
          if (entry->object.symlink == NULL) {
               LogDebug(COMPONENT_CACHE_INODE,
                        "Can't allocate entry symlink from symlink pool");

               *status = CACHE_INODE_MALLOC_ERROR;
               goto out;
          }
          FSAL_pathcpy(&entry->object.symlink->content,
                       &create_arg->link_content);
          break;

     case SOCKET_FILE:
     case FIFO_FILE:
     case BLOCK_FILE:
     case CHARACTER_FILE:
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Adding a special file of type %d "
                   "entry=%p", type, entry);
          break;

     case FS_JUNCTION:
          /* I don't think this ever actually gets called */
          abort();
          break;

     default:
          /* Should never happen */
          *status = CACHE_INODE_INCONSISTENT_ENTRY;
          LogMajor(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: unknown type %u provided",
                   type);
          goto out;
     }
     typespec = TRUE;

     /* Use the supplied attributes and fix up metadata */
     entry->attributes = *attr;
     cache_inode_fixup_md(entry);

     /* Adding the entry in the hash table */
     key.pdata = entry->fh_desc.start;
     key.len = entry->fh_desc.len;

     value.pdata = entry;
     value.len = sizeof(cache_entry_t);

     if((rc =
         HashTable_SetLatched(fh_to_cache_entry_ht, &key, &value,
                              &latch, TRUE, NULL, NULL))
        != HASHTABLE_SUCCESS) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: entry could not be added to hash, "
                  "rc=%d", rc);
          *status = CACHE_INODE_HASH_SET_ERROR;
          goto out;
     }
     latched = FALSE;


     LogDebug(COMPONENT_CACHE_INODE,
              "cache_inode_new_entry: New entry %p added", entry);
     *status = CACHE_INODE_SUCCESS;

out:
     if (*status != CACHE_INODE_SUCCESS) {
          /* Deconstruct the object */
          if (typespec) {
               switch (type) {
               case SYMBOLIC_LINK:
                    cache_inode_release_symlink(entry,
                                                &client->pool_entry_symlink);
                    break;

               default:
                    break;
               }
          }
          if (locksinited) {
               pthread_rwlock_destroy(&entry->attr_lock);
               pthread_rwlock_destroy(&entry->content_lock);
               pthread_rwlock_destroy(&entry->state_lock);
          }
          if (weakrefed) {
               cache_inode_weakref_delete(&entry->weakref);
          }
          if (latched) {
               HashTable_ReleaseLatched(fh_to_cache_entry_ht, &latch);
          }
          if (lrurefed) {
               cache_inode_lru_unref(entry, client, LRU_FLAG_NONE);
          }
          if (*status != CACHE_INODE_ENTRY_EXISTS) {
               entry = NULL;
          }
     }

     return entry;
}                               /* cache_inode_new_entry */

/**
 *
 * cache_clean_entry: cleans an entry when it is garbagge collected.
 *
 * Cleans an entry when it is garbagge collected.
 *
 * @param pentry [INOUT] the entry to be cleaned.
 *
 * @return CACHE_INODE_SUCCESS in all cases.
 *
 */
cache_inode_status_t cache_inode_clean_entry(cache_entry_t * pentry)
{
  pentry->type = RECYCLED;
  pthread_rwlock_destroy(&pentry->content_lock);
  pthread_rwlock_destroy(&pentry->state_lock);
  pthread_rwlock_destroy(&pentry->attr_lock);
  return CACHE_INODE_SUCCESS;
}

/**
 *
 * @brief converts an FSAL error to the corresponding cache_inode error.
 *
 * Converts an FSAL error to the corresponding cache_inode error.
 *
 * @param fsal_status [IN] fsal error to be converted.
 *
 * @return the result of the conversion.
 *
 */
cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status)
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

    case ERR_FSAL_DEADLOCK:
    case ERR_FSAL_BLOCKED:
    case ERR_FSAL_INTERRUPT:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_NOT_INIT:
    case ERR_FSAL_ALREADY_INIT:
    case ERR_FSAL_BAD_INIT:
    case ERR_FSAL_NO_QUOTA:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_MLINK:
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
          "cache_inode_error_convert: default conversion to CACHE_INODE_FSAL_ERROR for error %d, line %u should never be reached",
           fsal_status.major, __LINE__);
  return CACHE_INODE_FSAL_ERROR;
}                               /* cache_inode_error_convert */

/**
 *
 * cache_inode_fsal_type_convert: converts an FSAL type to the cache_inode type to be used.
 *
 * Converts an FSAL type to the cache_inode type to be used.
 *
 * @param type [IN] the input FSAL type.
 *
 * @return the result of the conversion.
 *
 */
cache_inode_file_type_t cache_inode_fsal_type_convert(fsal_nodetype_t type)
{
  cache_inode_file_type_t rctype;

  switch (type)
    {
    case FSAL_TYPE_DIR:
      rctype = DIRECTORY;
      break;

    case FSAL_TYPE_FILE:
      rctype = REGULAR_FILE;
      break;

    case FSAL_TYPE_LNK:
      rctype = SYMBOLIC_LINK;
      break;

    case FSAL_TYPE_BLK:
      rctype = BLOCK_FILE;
      break;

    case FSAL_TYPE_FIFO:
      rctype = FIFO_FILE;
      break;

    case FSAL_TYPE_CHR:
      rctype = CHARACTER_FILE;
      break;

    case FSAL_TYPE_SOCK:
      rctype = SOCKET_FILE;
      break;

    case FSAL_TYPE_JUNCTION:
      rctype = FS_JUNCTION ;
      break ;

    default:
      rctype = UNASSIGNED;
      break;
    }

  return rctype;
}                               /* cache_inode_fsal_type_convert */

/**
 *
 * cache_inode_type_are_rename_compatible: test if an existing entry could be scrtached during a rename.
 *
 * test if an existing entry could be scrtached during a rename. No mutext management.
 *
 * @param pentry_src  [IN] the source pentry (the one to be renamed)
 * @param pentry_dest [IN] the dest pentry (the one to be scratched during the rename)
 *
 * @return TRUE if rename if allowed (types are compatible), FALSE if not.
 *
 */
int cache_inode_type_are_rename_compatible(cache_entry_t * pentry_src,
                                           cache_entry_t * pentry_dest)
{
  /* TRUE is both entries are non directories or to directories and the second is empty */
  if(pentry_src->type == DIRECTORY)
    {
      if(pentry_dest->type == DIRECTORY)
        {
          if(cache_inode_is_dir_empty(pentry_dest) == CACHE_INODE_SUCCESS)
            return TRUE;
          else
            return FALSE;
        }
      else
        return FALSE;
    }
  else
    {
      /* pentry_src is not a directory */
      if(pentry_dest->type == DIRECTORY)
        return FALSE;
      else
        return TRUE;
    }
}                               /* cache_inode_type_are_rename_compatible */

/**
 *
 * cache_inode_print_dir: prints the content of a pentry that is a directory segment.
 *
 * Prints the content of a pentry that is a DIRECTORY.
 * /!\ This function is provided for debugging purpose only, it makes no sanity check on the arguments.
 *
 * @param pentry [IN] the input pentry.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_print_dir(cache_entry_t * cache_entry_root)/* release internal ref */
{
  struct avltree_node *dirent_node;
  cache_inode_dir_entry_t *dirent;
  int i = 0;

  if(cache_entry_root->type != DIRECTORY)
    {
      LogDebug(COMPONENT_CACHE_INODE,
                   "This entry is not a directory");
      return;
    }

  dirent_node = avltree_first(&cache_entry_root->object.dir.avl.t);
  do {
      dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
                                    node_hk);
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "Name = %s, DIRECTORY entry = (%p, %"PRIu64") i=%d",
                   dirent->name.name,
                   dirent->entry.ptr,
                   dirent->entry.gen,
                   i);
      i++;
  } while ((dirent_node = avltree_next(dirent_node)));

  LogFullDebug(COMPONENT_CACHE_INODE, "------------------");
}                               /* cache_inode_print_dir */

/**
 *
 * cache_inode_release_symlink: release an entry's symlink component, if
 * present
 *
 * releases an allocated symlink component, if any
 *
 * @param pool [INOUT] pool which owns pentry
 * @param pentry [INOUT] entry to be released
 *
 * @return  (void)
 *
 */
void cache_inode_release_symlink(cache_entry_t * pentry,
                                 struct prealloc_pool *pool)
{
    assert(pentry);
    assert(pentry->type == SYMBOLIC_LINK);
    if (pentry->object.symlink)
     {
        ReleaseToPool(pentry->object.symlink, pool);
        pentry->object.symlink = NULL;
     }
}

/**
 *
 * cache_inode_release_dirents: release cached dirents associated
 * with an entry.
 *
 * releases dirents associated with pentry.  this is simple, but maybe
 * should be abstracted.
 *
 * @param pentry [INOUT] entry to be released
 * @param pclient [IN] related pclient
 * @param which [INOUT] caches to clear (dense, sparse, or both)
 *
 * @return  (void)
 *
 */
void cache_inode_release_dirents(cache_entry_t           * pentry,
                                 cache_inode_client_t    * pclient,
                                 cache_inode_avl_which_t   which)
{
    struct avltree_node     * dirent_node      = NULL ;
    struct avltree_node     * next_dirent_node = NULL ;
    struct avltree          * tree             = NULL ;
    cache_inode_dir_entry_t * dirent           = NULL ;

    /* Won't see this */
    if (pentry->type != DIRECTORY)
        return;

    switch( which )
    {
    case CACHE_INODE_AVL_NAMES:
        tree = &pentry->object.dir.avl.t;
	break;

    case CACHE_INODE_AVL_COOKIES:
        tree = &pentry->object.dir.avl.c;
        break;

    case CACHE_INODE_AVL_BOTH:
	cache_inode_release_dirents(pentry, pclient, CACHE_INODE_AVL_NAMES);
	cache_inode_release_dirents(pentry, pclient, CACHE_INODE_AVL_COOKIES);
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
             dirent = avltree_container_of( dirent_node,
                                            cache_inode_dir_entry_t,
                                            node_hk);
             avltree_remove(dirent_node, tree);
             ReleaseToPool(dirent, &pclient->pool_dir_entry);
	     dirent_node = next_dirent_node;
	   }

          if (tree == &pentry->object.dir.avl.t) {
              pentry->object.dir.nbactive = 0;
              atomic_clear_int_bits(&pentry->flags,
                                    (CACHE_INODE_TRUST_CONTENT |
                                     CACHE_INODE_DIR_POPULATED));
          }
    }
}

/**
 * @brief Return true if a file holds state
 *
 * This function returns true if state is held on the file.  The state
 * lock must be held for read when calling this function.
 *
 * @param entry [in] The file to be checked
 *
 * @return TRUE if state is held, FALSE otherwise.
 */

inline bool_t
cache_inode_file_holds_state(cache_entry_t *entry)
{
     return (entry != NULL) &&
             ((entry->type == REGULAR_FILE && !glist_empty(&entry->object.file.lock_list)) ||
              !glist_empty(&entry->state_list));
} /* cache_inode_file_holds_state */

/**
 *
 * cache_inode_get_gc_policy: Set the cache_inode garbage collecting policy.
 *
 * @return the current policy.
 *
 */
cache_inode_gc_policy_t cache_inode_get_gc_policy(void)
{
  return cache_inode_gc_policy;
}                               /* cache_inode_get_gc_policy */

/**
 *
 * cache_inode_set_gc_policy: Set the cache_inode garbage collecting policy.
 *
 * @param policy [IN] policy to be set.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_set_gc_policy(cache_inode_gc_policy_t policy)
{
  cache_inode_gc_policy = policy;
}                               /* cache_inode_set_gc_policy */

/**
 * \brief Conditionally refresh attributes
 *
 * This function tests whether we should still trust the current
 * attributes and, if not, refresh them.
 */

cache_inode_status_t
cache_inode_check_trust(cache_entry_t *entry,
                        fsal_op_context_t *context,
                        cache_inode_client_t *client)
{
     time_t current_time = 0;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     time_t oldmtime = 0;
     fsal_status_t fsal_status = {0, 0};

     if ((entry->type == FS_JUNCTION) ||
         (entry->type == UNASSIGNED) ||
         (entry->type == RECYCLED)) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_check_attrs called on file %p of bad type %d",
                  entry, entry->type);

          status = CACHE_INODE_BAD_TYPE;
          goto out;
        }

     pthread_rwlock_rdlock(&entry->attr_lock);
     current_time = time(NULL);

     oldmtime = entry->attributes.mtime.seconds;

     /* Do we need a refresh? */
     if (((client->expire_type_attr == CACHE_INODE_EXPIRE_NEVER) ||
          (current_time - entry->attr_time < client->grace_period_attr)) &&
         (entry->flags & CACHE_INODE_TRUST_ATTRS) &&
         !((client->getattr_dir_invalidation) && (entry->type == DIRECTORY))) {
          goto unlock;
     }

     pthread_rwlock_unlock(&entry->attr_lock);

     /* Update the atributes */
     pthread_rwlock_wrlock(&entry->attr_lock);
     current_time = time(NULL);

     /* Make sure no one else has first */
     if (((client->expire_type_attr == CACHE_INODE_EXPIRE_NEVER) ||
          (current_time - entry->attr_time < client->grace_period_attr)) &&
         (entry->flags & CACHE_INODE_TRUST_ATTRS) &&
         !((client->getattr_dir_invalidation) && (entry->type == DIRECTORY))) {
          goto unlock;
     }

     if ((status = cache_inode_refresh_attrs(entry, context, client))
         != CACHE_INODE_SUCCESS) {
          goto unlock;
     }

     if (entry->type == SYMBOLIC_LINK &&
         ((client->expire_type_link != CACHE_INODE_EXPIRE_NEVER &&
          ((current_time - entry->change_time >=
            client->grace_period_link))) ||
          !(entry->flags & CACHE_INODE_TRUST_CONTENT))) {
          pthread_rwlock_wrlock(&entry->content_lock);
          pthread_rwlock_unlock(&entry->attr_lock);

          assert(entry->object.symlink);

          fsal_status = FSAL_readlink(&entry->handle,
                                      context,
                                      &entry->object.symlink->content,
                                      NULL);

          if (FSAL_IS_ERROR(fsal_status)) {
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(entry, client);
               }
               status = cache_inode_error_convert(fsal_status);
          }
          pthread_rwlock_unlock(&entry->content_lock);
          goto out;
     } else if ((entry->type == DIRECTORY) &&
                (oldmtime < entry->attributes.mtime.seconds)) {
          pthread_rwlock_wrlock(&entry->content_lock);
          pthread_rwlock_unlock(&entry->attr_lock);

          atomic_clear_int_bits(&entry->flags, CACHE_INODE_TRUST_CONTENT |
                                CACHE_INODE_DIR_POPULATED);

          if (cache_inode_invalidate_all_cached_dirent(entry, client,
                                                       &status)
              != CACHE_INODE_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE,
                       "cache_inode_invalidate_all_cached_dirent "
                       "returned %d (%s)", status,
                       cache_inode_err_str(status));
          }

          pthread_rwlock_unlock(&entry->content_lock);
          goto out;
     }

unlock:

     pthread_rwlock_unlock(&entry->attr_lock);

out:
     return status;
}
