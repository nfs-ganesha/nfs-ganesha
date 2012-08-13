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

     /* Get ride of entries cached in the DIRECTORY */
     cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);

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

     /* If no active entry, do nothing */
     if (directory->object.dir.nbactive == 0) {
       if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
             (directory->flags & CACHE_INODE_DIR_POPULATED))) {
         /* We cannot serve negative lookups. */
           /* status == CACHE_INODE_SUCCESS; */
       } else {
           status = CACHE_INODE_NOT_FOUND;
       }
       goto out;
     }

     FSAL_namecpy(&dirent_key->name, name);
     dirent = cache_inode_avl_qp_lookup_s(directory, dirent_key, 1);
     if ((!dirent) || (dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
       if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
             (directory->flags & CACHE_INODE_DIR_POPULATED))) {
         /* We cannot serve negative lookups. */
         /* status == CACHE_INODE_SUCCESS; */
       } else {
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
             dirent3->entry = dirent->entry;
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
     new_dir_entry->entry = entry->weakref;

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
  fsal_attrib_list_t dir_attributes;

  fsal_cookie_t begin_cookie;
  fsal_cookie_t end_cookie;
  fsal_count_t found = 0;
  uint32_t iter = 0;
  fsal_boolean_t eod = FALSE;

  cache_entry_t *entry = NULL;
  fsal_attrib_list_t object_attributes;

  cache_inode_create_arg_t create_arg = {
       .newly_created_dir = FALSE
  };
  cache_inode_file_type_t type = UNASSIGNED;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_dirent_t array_dirent[FSAL_READDIR_SIZE + 20];
  cache_inode_fsal_data_t new_entry_fsdata;
  cache_inode_dir_entry_t *new_dir_entry = NULL;
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

  /* Invalidate all the dirents */
  if(cache_inode_invalidate_all_cached_dirent(directory,
                                              status) != CACHE_INODE_SUCCESS)
    return *status;

  /* Open the directory */
  dir_attributes.asked_attributes = cache_inode_params.attrmask;
  fsal_status = FSAL_opendir(&directory->handle,
                             context, &dir_handle, &dir_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
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

          /* If dir entry is a symbolic link, its content has to be read */
          if((type =
              cache_inode_fsal_type_convert(array_dirent[iter]
                                            .attributes.type))
             == SYMBOLIC_LINK)
            {
              /* Let's read the link for caching its value */
              object_attributes.asked_attributes = cache_inode_params.attrmask;
              fsal_status
                = FSAL_readlink(&array_dirent[iter].handle,
                                context,
                                &create_arg.link_content, &object_attributes);

              if(FSAL_IS_ERROR(fsal_status))
                {
                     *status = cache_inode_error_convert(fsal_status);
                     if (fsal_status.major == ERR_FSAL_STALE) {
                          cache_inode_kill_entry(directory);
                     }
                     goto bail;
                }
            }
          else
            {
              create_arg.newly_created_dir = FALSE;
            }

          /* Try adding the entry, if it exists then this existing entry is
             returned */
          new_entry_fsdata.fh_desc.start
            = (caddr_t)(&array_dirent[iter].handle);
          new_entry_fsdata.fh_desc.len = 0;
          FSAL_ExpandHandle(context->export_context,
                            FSAL_DIGEST_SIZEOF,
                            &new_entry_fsdata.fh_desc);

          if((entry
              = cache_inode_new_entry(&new_entry_fsdata,
                                      &array_dirent[iter].attributes,
                                      type,
                                      &create_arg,
                                      status)) == NULL)
            goto bail;
          cache_status
            = cache_inode_add_cached_dirent(directory,
                                            &(array_dirent[iter].name),
                                            entry,
                                            &new_dir_entry,
                                            status);

          /* Once the weakref is stored in the directory entry, we
             can release the reference we took on the entry. */
          cache_inode_lru_unref(entry, 0);

          if(cache_status != CACHE_INODE_SUCCESS
             && cache_status != CACHE_INODE_ENTRY_EXISTS)
            goto bail;

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
                                    &new_dir_entry->fsal_cookie);
        } /* iter */

      /* Get prepared for next step */
      begin_cookie = end_cookie;

      /* next offset */
      i++;
    }
  while(eod != TRUE);

  /* Close the directory */
  fsal_status = FSAL_closedir(&dir_handle);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      return *status;
    }

  /* End of work */
  atomic_set_uint32_t_bits(&directory->flags,
                           (CACHE_INODE_DIR_POPULATED |
                            CACHE_INODE_TRUST_CONTENT));
  *status = CACHE_INODE_SUCCESS;
  return *status;

bail:
  /* Close the directory */
  FSAL_closedir(&dir_handle);
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
     const fsal_accessflags_t access_mask
          = (FSAL_MODE_MASK_SET(FSAL_R_OK) |
             FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR));
     /* True if the most recently traversed directory entry has been
        added to the caller's result. */
     bool_t in_result = TRUE;

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
     *status = cache_inode_lock_trust_attrs(directory, context);
     if (*status != CACHE_INODE_SUCCESS)
       return *status;

     /* Check if user (as specified by the credentials) is authorized to read
      * the directory or not */
     if (cache_inode_access_no_mutex(directory,
                                     access_mask,
                                     context,
                                     status)
         != CACHE_INODE_SUCCESS) {
          goto unlock_attrs;
     }

     pthread_rwlock_rdlock(&directory->content_lock);
     pthread_rwlock_unlock(&directory->attr_lock);
     if (!((directory->flags & CACHE_INODE_TRUST_CONTENT) &&
           (directory->flags & CACHE_INODE_DIR_POPULATED))) {
          pthread_rwlock_unlock(&directory->content_lock);
          pthread_rwlock_wrlock(&directory->content_lock);
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

     while (in_result && dirent_node) {
          cache_entry_t *entry = NULL;
          cache_inode_status_t lookup_status = 0;

          dirent = avltree_container_of(dirent_node,
                                        cache_inode_dir_entry_t,
                                        node_hk);

          if ((entry
               = cache_inode_weakref_get(&dirent->entry,
                                         LRU_REQ_SCAN))
              == NULL) {
               /* Entry fell out of the cache, load it back in. */
               if ((entry
                    = cache_inode_lookup_impl(directory,
                                              &dirent->name,
                                              context,
                                              &lookup_status))
                   == NULL) {
                    if (lookup_status == CACHE_INODE_NOT_FOUND) {
                         /* Directory changed out from under us.
                            Invalidate it, skip the name, and keep
                            going. */
                         atomic_clear_uint32_t_bits(&directory->flags,
                                                    CACHE_INODE_TRUST_CONTENT);
                         continue;
                    } else {
                         /* Something is more seriously wrong,
                            probably an inconsistency. */
                         *status = lookup_status;
                         goto unlock_dir;
                    }
               }
          }

          LogFullDebug(COMPONENT_NFS_READDIR,
                       "cache_inode_readdir: dirent=%p name=%s "
                       "cookie=%"PRIu64" (probes %d)",
                       dirent, dirent->name.name,
                       dirent->hk.k, dirent->hk.p);

          *status = cache_inode_lock_trust_attrs(entry, context);
          if (*status != CACHE_INODE_SUCCESS)
            {
              cache_inode_lru_unref(entry, 0);
              goto unlock_dir;
            }

          in_result = cb(cb_opaque,
                         dirent->name.name,
                         &entry->handle,
                         &entry->attributes,
                         dirent->hk.k);
          (*nbfound)++;
          pthread_rwlock_unlock(&entry->attr_lock);
          cache_inode_lru_unref(entry, 0);
          if (!in_result) {
               break;
          }
          dirent_node = avltree_next(dirent_node);
     }

     /* We have reached the last node and every node traversed was
        added to the result */;

     if (!dirent_node && in_result) {
          *eod_met = TRUE;
     } else {
          *eod_met = FALSE;
     }

unlock_dir:

     pthread_rwlock_unlock(&directory->content_lock);
     return *status;

unlock_attrs:

     pthread_rwlock_unlock(&directory->attr_lock);
     return *status;
} /* cache_inode_readdir */
