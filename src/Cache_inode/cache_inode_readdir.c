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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    cache_inode_readdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.50 $
 * \brief   Reads the content of a directory.
 *
 * @brief Reads the content of a directory, also includes support
 *        functions for cached directories.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "abstract_atomic.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_avl.h"
#include "cache_inode_weakref.h"
#include "nfs_exports.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Invalidates all cached entries for a directory
 *
 * Invalidates all the entries for a cached directory.  The content
 * lock must be held when this function is called.
 *
 * @param[in,out] entry  The directory to be managed
 * @param[out]    status Returned status.
 *
 * @return the same as *status
 *
 */
cache_inode_status_t
cache_inode_invalidate_all_cached_dirent(cache_entry_t *entry,
                                         cache_inode_status_t *status)
{
     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /* Only DIRECTORY entries are concerned */
     if (entry->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          return *status;
     }

     /* Mark directory as not populated */
     atomic_clear_uint32_t_bits(&entry->flags, (CACHE_INODE_DIR_POPULATED |
                                                CACHE_INODE_TRUST_CONTENT));
     *status = CACHE_INODE_SUCCESS;

     return *status;
} /* cache_inode_invalidate_all_cached_dirent */


/**
 * @brief Perform an operation on it on a cached entry
 *
 * This function looks up an entry in the drectory cache and performs
 * the indicated operation.  If the directory has not been populated,
 * it will not return not found errors.
 *
 * The caller must hold the content lock on the directory.
 *
 * @param[in] directory The directory to be operated upon
 * @param[in] name      The name of the relevant entry
 * @param[in] newname   The new name for renames
 * @param[in] dirent_op The operation (LOOKUP, REMOVE, or RENAME) to
 *                      perform
 *
 * @retval CACHE_INODE_SUCCESS on success or failure in an unpopulated
 *                             directory.
 * @retval CACHE_INODE_BAD_TYPE if the supplied cache entry is not a
 *                             directory.
 * @retval CACHE_INODE_NOT_FOUND on lookup failure in a populated
 *                             directory.
 * @retval CACHE_INODE_ENTRY_EXISTS on rename collission in a
 *                             populated directory.
 */

cache_inode_status_t
cache_inode_operate_cached_dirent(cache_entry_t *directory,
                                  fsal_name_t *name,
                                  fsal_name_t *newname,
                                  cache_inode_dirent_op_t dirent_op)
{
     cache_inode_dir_entry_t dirent_key[1], *dirent, *dirent2, *dirent3;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     int code = 0;

     /* Sanity check */
     if(directory->type != DIRECTORY) {
         status = CACHE_INODE_BAD_TYPE;
         goto out;
     }
     LogFullDebug(COMPONENT_CACHE_INODE,
                  "%s %p name=%s newname=%s",
                  dirent_op == CACHE_INODE_DIRENT_OP_REMOVE ?
                  "REMOVE" : "RENAME",
                  directory,
                  name->name,
                  newname->name);

     /* If no active entry, do nothing */
     if (directory->object.dir.nbactive == 0) {
       if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
             (directory->flags & CACHE_INODE_DIR_POPULATED))) {
         /* We cannot serve negative lookups. */
           /* status == CACHE_INODE_SUCCESS; */
           LogFullDebug(COMPONENT_CACHE_INODE,
                        "We cannot serve negative lookups (1).");
       } else {
           LogFullDebug(COMPONENT_CACHE_INODE,
                        "Negative lookup (1).");
           status = CACHE_INODE_NOT_FOUND;
       }
       goto out;
     }

     FSAL_namecpy(&dirent_key->name, name);
     dirent = cache_inode_avl_qp_lookup_s(directory, dirent_key, 1);
     if ((!dirent) || (dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
       if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
             (directory->flags & CACHE_INODE_DIR_POPULATED)) ||
            (dirent_op == CACHE_INODE_DIRENT_OP_REMOVE)) {
         /* We cannot serve negative lookups. */
         /* status == CACHE_INODE_SUCCESS; */
         LogFullDebug(COMPONENT_CACHE_INODE,
                      "We cannot serve negative lookups (2).");
       } else {
         LogFullDebug(COMPONENT_CACHE_INODE,
                      "Negative lookup (2).");
         status = CACHE_INODE_NOT_FOUND;
       }
       goto out;
     }


     /* We perform operations anyway even if CACHE_INODE_TRUST_CONTENT
        is clear.  That way future upcalls can call in to this
        function to update the content to be correct.  We just don't
        ever return a not found or exists error. */

     switch (dirent_op) {
     case CACHE_INODE_DIRENT_OP_REMOVE:
         /* mark deleted */
         avl_dirent_set_deleted(directory, dirent);
         directory->object.dir.nbactive--;
         break;

     case CACHE_INODE_DIRENT_OP_RENAME:
         FSAL_namecpy(&dirent_key->name, newname);
         dirent2 = cache_inode_avl_qp_lookup_s(directory,
                                               dirent_key, 1);
         if (dirent2) {
             /* rename would cause a collision */
             if (directory->flags &
                 CACHE_INODE_TRUST_CONTENT) {
                 /* We are not up to date. */
                 /* status == CACHE_INODE_SUCCESS; */
             } else {
                 status = CACHE_INODE_ENTRY_EXISTS;
             }
         } else {
             /* try to rename--no longer in-place */
             avl_dirent_set_deleted(directory, dirent);
             dirent3 = pool_alloc(cache_inode_dir_entry_pool, NULL);
             FSAL_namecpy(&dirent3->name, newname);
             dirent3->flags = DIR_ENTRY_FLAG_NONE;
             dirent3->entry_wkref = dirent->entry_wkref;
             code = cache_inode_avl_qp_insert(directory, dirent3);
             switch (code) {
             case 0:
                 /* CACHE_INODE_SUCCESS */
                 break;
             case 1:
                 /* we reused an existing dirent, dirent has been deep
                  * copied, dispose it */
                  pool_free(cache_inode_dir_entry_pool, dirent3);
                 /* CACHE_INODE_SUCCESS */
                 break;
             case -1:
                 /* collision, tree state unchanged (unlikely) */
                 status = CACHE_INODE_ENTRY_EXISTS;
                 /* dirent is on persist tree, undelete it */
                 avl_dirent_clear_deleted(directory, dirent);
                 /* dirent3 was never inserted */
                 pool_free(cache_inode_dir_entry_pool, dirent3);
             default:
                 LogCrit(COMPONENT_NFS_READDIR,
                         "DIRECTORY: insert error renaming dirent "
                         "(%s, %s)",
                         name->name, newname->name);
                 status = CACHE_INODE_INSERT_ERROR;
                 break;
             }
         } /* !found */
         break;

     default:
         /* Should never occur, in any case, it costs nothing to handle
          * this situation */
         status = CACHE_INODE_INVALID_ARGUMENT;
         break;

     }                       /* switch */

out:
     return (status);
} /* cache_inode_operate_cached_dirent */

/**
 *
 * @brief Adds a directory entry to a cached directory.
 *
 * This function adds a new directory entry to a directory.  Directory
 * entries have only weak references, so they do not prevent recycling
 * or freeing the entry they locate.  This function may be called
 * either once (for handling creation) or iteratively in directory
 * population.
 *
 * @param[in,out] parent    Cache entry of the directory being updated
 * @param[in]     name      The name to add to the entry
 * @param[in]     entry     The cache entry associated with name
 * @param[out]    dir_entry The directory entry newly added (optional)
 * @param[out]    status    Same as return value
 *
 * @return CACHE_INODE_SUCCESS or errors on failure.
 *
 */
cache_inode_status_t
cache_inode_add_cached_dirent(cache_entry_t *parent,
                              fsal_name_t *name,
                              cache_entry_t *entry,
                              cache_inode_dir_entry_t **dir_entry,
                              cache_inode_status_t *status)
{
     cache_inode_dir_entry_t *new_dir_entry = NULL;
     int code = 0;

     *status = CACHE_INODE_SUCCESS;

     /* Sanity check */
     if(parent->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          return *status;
     }

     /* in cache inode avl, we always insert on pentry_parent */
     new_dir_entry = pool_alloc(cache_inode_dir_entry_pool, NULL);
     if(new_dir_entry == NULL) {
          *status = CACHE_INODE_MALLOC_ERROR;
          return *status;
     }

     new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;

     FSAL_namecpy(&new_dir_entry->name, name);
     if(entry)
       new_dir_entry->entry_wkref = entry->weakref;

     new_dir_entry->itr_present = FALSE;

     /* add to avl */
     code = cache_inode_avl_qp_insert(parent, new_dir_entry);
     switch (code) {
     case 0:
         /* CACHE_INODE_SUCCESS */
         break;
     case 1:
         /* we reused an existing dirent, dirent has been deep
          * copied, dispose it */
         pool_free(cache_inode_dir_entry_pool, new_dir_entry);
         /* CACHE_INODE_SUCCESS */
         break;
     default:
         /* collision, tree not updated--release both pool objects and return
          * err */
         pool_free(cache_inode_dir_entry_pool, new_dir_entry);
         *status = CACHE_INODE_ENTRY_EXISTS;
         return *status;
         break;
     }

     if (dir_entry) {
         *dir_entry = new_dir_entry;
     }

     /* we're going to succeed */
     parent->object.dir.nbactive++;

     return *status;
} /* cache_inode_add_cached_dirent */

/**
 *
 * @brief Removes an entry from a cached directory.
 *
 * This function removes the named entry from a cached directory.  The
 * caller must hold the content lock.
 *
 * @param[in,out] directory The cache entry representing the directory
 * @param[in]     name      The name indicating the entry to remove
 * @param[out]    status    Returned status
 *
 * @retval CACHE_INODE_SUCCESS on success.
 * @retval CACHE_INODE_BAD_TYPE if directory is not a directory.
 * @retval The result of cache_inode_operate_cached_dirent
 *
 */
cache_inode_status_t
cache_inode_remove_cached_dirent(cache_entry_t *directory,
                                 fsal_name_t *name,
                                 cache_inode_status_t *status)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(directory->type != DIRECTORY)
    {
      *status = CACHE_INODE_BAD_TYPE;
      return *status;
    }

  *status
       = cache_inode_operate_cached_dirent(directory,
                                           name,
                                           NULL,
                                           CACHE_INODE_DIRENT_OP_REMOVE);
  return (*status);

} /* cache_inode_remove_cached_dirent */

static
void cache_inode_validate_all_cached_dirent(cache_entry_t *directory,
                                            fsal_op_context_t *context)
{
  /* Used in verifying if any dirents should be removed. */
  struct avltree_node *dirent_node = NULL;
  struct avltree_node *next_dirent_node = NULL;
  cache_inode_dir_entry_t *dirent = NULL;
  struct avltree *tree;

  tree = &directory->object.dir.avl.t;
  if (!tree)
    return; /* Nothing to do */

  dirent_node = avltree_first(tree);
  while( dirent_node )
    {
      next_dirent_node = avltree_next(dirent_node);
      dirent = avltree_container_of(dirent_node,
                                    cache_inode_dir_entry_t,
                                    node_hk);
      if(dirent->itr_present == FALSE)
        {
          /* quick removal of dirent to dir.avl.c */
           LogFullDebug(COMPONENT_CACHE_INODE,
                        "deleting: %s", dirent->name.name);
          avl_dirent_set_deleted(directory, dirent);
          directory->object.dir.nbactive--;
        }
      else
        dirent->itr_present = FALSE;

      dirent_node = next_dirent_node;
    }
}

/**
 *
 * @brief Cache complete directory contents
 *
 * This function reads a complete directory from the FSAL and caches
 * both the names and filess.  The content lock must be held on the
 * directory being read.
 *
 * @param[in]     directory  Entry for the parent directory to be read
 * @param[in]     context    FSAL credentials
 * @param[out]    status     Returned status
 *
 */
cache_inode_status_t
cache_inode_readdir_populate(cache_entry_t *directory,
                             fsal_op_context_t *context,
                             cache_inode_status_t *status)
{
  fsal_dir_t dir_handle;
  fsal_status_t fsal_status;

  fsal_cookie_t begin_cookie;
  fsal_cookie_t end_cookie;
  fsal_count_t found = 0;
  uint32_t iter = 0;
  fsal_boolean_t eod = FALSE;

  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_dirent_t array_dirent[FSAL_READDIR_SIZE + 20];
  cache_inode_dir_entry_t *dirent = NULL;
  cache_inode_dir_entry_t dirent_key;

  uint64_t i = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *status = CACHE_INODE_SUCCESS;

  /* Only DIRECTORY entries are concerned */
  if(directory->type != DIRECTORY)
    {
      *status = CACHE_INODE_BAD_TYPE;
      return *status;
    }

  if((directory->flags & CACHE_INODE_DIR_POPULATED) &&
     (directory->flags & CACHE_INODE_TRUST_CONTENT))
    {
      *status = CACHE_INODE_SUCCESS;
      return *status;
    }

  /* Open the directory */
  fsal_status = FSAL_opendir(&directory->handle, context, &dir_handle, NULL);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
           LogEvent(COMPONENT_CACHE_INODE,
                "FSAL returned STALE from opendir");
           cache_inode_kill_entry(directory);
      }
      return *status;
    }

  /* Loop for readding the directory */
  FSAL_SET_COOKIE_BEGINNING(begin_cookie);
  FSAL_SET_COOKIE_BEGINNING(end_cookie);
  eod = FALSE;

  do
    {
      fsal_status
        = FSAL_readdir(&dir_handle,
                       context,
                       begin_cookie,
                       cache_inode_params.attrmask,
                       FSAL_READDIR_SIZE * sizeof(fsal_dirent_t),
                       array_dirent, &end_cookie, &found, &eod);

      if(FSAL_IS_ERROR(fsal_status))
        {
          *status = cache_inode_error_convert(fsal_status);
          goto bail;
        }

      for(iter = 0; iter < found; iter++)
        {
          LogMidDebug(COMPONENT_CACHE_INODE,
                       "cache readdir populate found entry %s",
                       array_dirent[iter].name.name);

          /* It is not needed to cache '.' and '..' */
          if(!FSAL_namecmp(&(array_dirent[iter].name),
                           (fsal_name_t *) & FSAL_DOT) ||
             !FSAL_namecmp(&(array_dirent[iter].name),
                           (fsal_name_t *) & FSAL_DOT_DOT))
            {
              LogMidDebug(COMPONENT_CACHE_INODE,
                          "cache readdir populate : do not cache . and ..");
              continue;
            }

          memset(&dirent_key, 0, sizeof(dirent_key));
          FSAL_namecpy(&dirent_key.name, &(array_dirent[iter].name));
          dirent = cache_inode_avl_qp_lookup_s(directory, &dirent_key, 1);
          if(!dirent)
            {
              /* Missing from AVL tree add it. */
              cache_status = cache_inode_add_cached_dirent(directory,
                                          &(array_dirent[iter].name),
                                          (cache_entry_t *)NULL,
                                          &dirent,
                                          status);

              if(cache_status != CACHE_INODE_SUCCESS
                 && cache_status != CACHE_INODE_ENTRY_EXISTS)
                goto bail;
            }
          dirent->itr_present = TRUE;

          /*
           * Remember the FSAL readdir cookie associated with this
           * dirent.  This is needed for partial directory reads.
           *
           * to_uint64 should be a lightweight operation--it is in the
           * current default implementation.
           *
           * I'm ignoring the status because the default operation is
           * a memcpy-- we already -have- the cookie. */

          if (cache_status != CACHE_INODE_ENTRY_EXISTS)
              FSAL_cookie_to_uint64(&array_dirent[iter].handle,
                                    context, &array_dirent[iter].cookie,
                                    &(dirent->fsal_cookie));
        } /* iter */

      /* Get prepared for next step */
      begin_cookie = end_cookie;

      /* next offset */
      i++;
    }
  while(eod != TRUE);

  /* Close the directory */
  fsal_status = FSAL_closedir(&dir_handle, context);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      return *status;
    }

  /* End of work */
  atomic_set_uint32_t_bits(&directory->flags,
                           (CACHE_INODE_DIR_POPULATED |
                            CACHE_INODE_TRUST_CONTENT));
  /* for name cache and cookie cache validate all entries*/
  cache_inode_validate_all_cached_dirent(directory, context);
  *status = CACHE_INODE_SUCCESS;
  return *status;

bail:
  /* Close the directory */
  FSAL_closedir(&dir_handle, context);
  return *status;
}                               /* cache_inode_readdir_populate */

/**
 *
 * @brief Reads a directory
 *
 * This function iterates over the cached directory entries (possibly
 * after populating the cache) and invokes a supplied callback
 * function for each one.
 *
 * The caller must not hold the attribute or content locks on
 * directory.
 *
 * @param[in]  directory The directory to be read
 * @param[in]  cookie    Starting cookie for the readdir operation
 * @param[out] nbfound   Number of entries returned.
 * @param[out] eod_met   Whether the end of directory was met
 * @param[in]  context   FSAL credentials
 * @param[in]  cb        The callback function to receive entries
 * @param[in]  cb_opaque A pointer passed as the first argument to cb
 * @param[out] status    Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_BAD_TYPE if entry is not related to a directory
 */
cache_inode_status_t
cache_inode_readdir(cache_entry_t *directory,
                    uint64_t cookie,
                    unsigned int *nbfound,
                    bool_t *eod_met,
                    fsal_op_context_t *context,
                    fsal_attrib_mask_t attrmask,
                    cache_inode_readdir_cb_t cb,
                    void *cb_opaque,
                    cache_inode_status_t *status)
{
     /* The entry being examined */
     cache_inode_dir_entry_t *dirent = NULL;
     /* The node in the tree being traversed */
     struct avltree_node *dirent_node;
     /* The access mask corresponding to permission to list directory
        entries */
     fsal_accessflags_t access_mask
          = (FSAL_MODE_MASK_SET(FSAL_R_OK) |
             FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR));
     fsal_accessflags_t access_mask_attr
          = (FSAL_MODE_MASK_SET(FSAL_R_OK) |
             FSAL_MODE_MASK_SET(FSAL_X_OK) |
             FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR) |
             FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE));
     /* True if the most recently traversed directory entry has been
        added to the caller's result. */
     bool_t in_result = TRUE;
     cache_inode_status_t attr_status;

     /* Set to TRUE if we invalidate the directory */
     bool_t invalid = FALSE;

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     /* readdir can be done only with a directory */
     if (directory->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          /* no lock acquired so far, just return status */
          return *status;
     }

     /* cache_inode_lock_trust_attrs can return an error, and no lock will be
        acquired */
     *status = cache_inode_lock_trust_attrs(directory, context, FALSE);
     if (*status != CACHE_INODE_SUCCESS)
       return *status;

     /* Adjust access mask if ACL is asked for.
      * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
      */
     if((attrmask & FSAL_ATTR_ACL) != 0) {
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
          access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);
     }

     /* Check if user (as specified by the credentials) is authorized to read
      * the directory or not */
     if (cache_inode_access_no_mutex(directory,
                                     access_mask,
                                     context,
                                     status)
         != CACHE_INODE_SUCCESS) {
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "permission check for directory status=%s",
                       cache_inode_err_str(*status));
          goto unlock_attrs;
     }

     if(attrmask != 0) {
          /* Check for access permission to get attributes */
          if (cache_inode_access_no_mutex(directory,
                                          access_mask_attr,
                                          context,
                                          &attr_status)
              != CACHE_INODE_SUCCESS) {
               LogFullDebug(COMPONENT_CACHE_INODE,
                            "permission check for attributes status=%s",
                            cache_inode_err_str(attr_status));
          }
     } else {
          /* No attributes requested, we don't need permission */
          attr_status = CACHE_INODE_SUCCESS;
     }

     PTHREAD_RWLOCK_RDLOCK(&directory->content_lock);
     PTHREAD_RWLOCK_UNLOCK(&directory->attr_lock);
     if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
           (directory->flags & CACHE_INODE_DIR_POPULATED))) {
          PTHREAD_RWLOCK_UNLOCK(&directory->content_lock);
          PTHREAD_RWLOCK_WRLOCK(&directory->content_lock);
          if (cache_inode_readdir_populate(directory,
                                           context,
                                           status)
              != CACHE_INODE_SUCCESS) {
               goto unlock_dir;
          }
     }

     /* deal with initial cookie value:
      * 1. cookie is invalid (-should- be checked by caller)
      * 2. cookie is 0 (first cookie) -- ok
      * 3. cookie is > than highest dirent position (error)
      * 4. cookie <= highest dirent position but > highest cached cookie
      *    (currently equivalent to #2, because we pre-populate the cookie avl)
      * 5. cookie is in cached range -- ok */

     if (cookie > 0) {
          /* N.B., cache_inode_avl_qp_insert_s ensures k > 2 */
          if (cookie < 3) {
               *status = CACHE_INODE_BAD_COOKIE;
               goto unlock_dir;
          }

          /* we assert this can now succeed */
          dirent = cache_inode_avl_lookup_k(directory, cookie,
                                            CACHE_INODE_FLAG_NEXT_ACTIVE);
          if (!dirent) {
               LogFullDebug(COMPONENT_NFS_READDIR,
                            "%s: seek to cookie=%"PRIu64" fail",
                            __func__, cookie);
               *status = CACHE_INODE_NOT_FOUND;
               goto unlock_dir;
          }

          /* dirent is the NEXT entry to return, since we sent
           * CACHE_INODE_FLAG_NEXT_ACTIVE */
          dirent_node = &dirent->node_hk;

     } else {
          /* initial readdir */
         dirent_node = avltree_first(&directory->object.dir.avl.t);
     }

     LogFullDebug(COMPONENT_NFS_READDIR,
                  "About to readdir in cache_inode_readdir: directory=%p "
                  "cookie=%"PRIu64" collisions %d",
                  directory,
                  cookie,
                  directory->object.dir.avl.collisions);

     /* Now satisfy the request from the cached readdir--stop when either
      * the requested sequence or dirent sequence is exhausted */
     *nbfound = 0;
     *eod_met = FALSE;

     for( ; 
         in_result && dirent_node;
         dirent_node = avltree_next(dirent_node)) {

          cache_entry_t *entry = NULL;
          cache_inode_status_t lookup_status = 0;

          dirent = avltree_container_of(dirent_node,
                                        cache_inode_dir_entry_t,
                                        node_hk);

          entry = cache_inode_weakref_get(&dirent->entry_wkref,
                                          LRU_REQ_SCAN);

          if(entry == NULL) {
               /* Entry fell out of the cache, load it back in.
                * Note that we don't restore the weakref...
                */
               entry = cache_inode_lookup_weakref(directory,
                                                  &dirent->name,
                                                  context,
                                                  &lookup_status);

               if(entry == NULL) {
                    LogFullDebug(COMPONENT_NFS_READDIR,
                                 "Lookup returned %s",
                                 cache_inode_err_str(lookup_status));
                    if (lookup_status == CACHE_INODE_NOT_FOUND) {
                         /* Directory changed out from under us.
                            Indicate we should invalidate it,
                            skip the name, and keep going. */
                         invalid = TRUE;
                         continue;
                    } else {
                         /* Something is more seriously wrong,
                            probably an inconsistency. */
                         *status = lookup_status;
                         goto unlock_dir;
                    }
               } else {
                    /* Add the ref to dirent */
                    dirent->entry_wkref = entry->weakref;
               }
          }

          LogFullDebug(COMPONENT_NFS_READDIR,
                       "cache_inode_readdir: dirent=%p name=%s "
                       "cookie=%"PRIu64" (probes %d)",
                       dirent, dirent->name.name,
                       dirent->hk.k, dirent->hk.p);

          *status = cache_inode_lock_trust_attrs(entry, context, FALSE);

          if (*status != CACHE_INODE_SUCCESS)
            {
              cache_inode_lru_unref(entry, 0);
              if(*status == CACHE_INODE_FSAL_ESTALE)
                {
                  LogDebug(COMPONENT_NFS_READDIR,
                           "cache_inode_lock_trust_attrs returned %s for %s - skipping entry",
                           cache_inode_err_str(*status),
                           dirent->name.name);

                  /* Directory changed out from under us.
                     Indicate we should invalidate it,
                     skip the name, and keep going. */
                  invalid = TRUE;
                  continue;
                }

              LogCrit(COMPONENT_NFS_READDIR,
                      "cache_inode_lock_trust_attrs returned %s for %s - bailing out",
                      cache_inode_err_str(*status),
                      dirent->name.name);

              goto unlock_dir;
            }

          if(attr_status == CACHE_INODE_SUCCESS) {
                    set_mounted_on_fileid(entry,
                                           &entry->attributes,
                                           context->export_context->fe_export);

                    in_result = cb(cb_opaque,
                                   dirent->name.name,
                                   entry,
                                   TRUE,
                                   context,
                                   dirent->hk.k);
          } else {
                    /* Even though permission is denied, we pass the
                     * cache_entry_t so v3 READDIR can return the fileid
                     */
                    in_result = cb(cb_opaque,
                                   dirent->name.name,
                                   entry,
                                   FALSE,
                                   context,
                                   dirent->hk.k);
          }

          (*nbfound)++;

          PTHREAD_RWLOCK_UNLOCK(&entry->attr_lock);
          cache_inode_lru_unref(entry, 0);

          if (!in_result) {
               break;
          }
     }

     /* We have reached the last node and every node traversed was
        added to the result */;

     if (!dirent_node && in_result) {
          *eod_met = TRUE;
     } else {
          *eod_met = FALSE;
     }

unlock_dir:

     PTHREAD_RWLOCK_UNLOCK(&directory->content_lock);

     if(invalid) {
          cache_inode_status_t tmp_status;

          PTHREAD_RWLOCK_WRLOCK(&directory->content_lock);

          cache_inode_invalidate_all_cached_dirent(directory, &tmp_status);

          if(tmp_status != CACHE_INODE_SUCCESS) {
               LogCrit(COMPONENT_NFS_READDIR,
                       "Error %s resolvind invalidated directory",
                       cache_inode_err_str(tmp_status));
          }

          PTHREAD_RWLOCK_UNLOCK(&directory->content_lock);
          
     }
     return *status;

unlock_attrs:

     PTHREAD_RWLOCK_UNLOCK(&directory->attr_lock);
     return *status;
} /* cache_inode_readdir */
