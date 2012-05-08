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
 * cache_inode_readdir.c : Reads the content of a directory. Contains also the needed function for directory browsing.
 *
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
#include "stuff_alloc.h"
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
 * @param entry [in,out] The directory to be managed
 * @param client [in,out] Structure for per-client resource management
 * @param status [OUT] Returned status.
 *
 * @return the same as *status
 *
 */
cache_inode_status_t
cache_inode_invalidate_all_cached_dirent(cache_entry_t *entry,
                                         cache_inode_client_t *client,
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
     cache_inode_release_dirents(entry, client, CACHE_INODE_AVL_BOTH);

     /* Mark directory as not populated */
     atomic_clear_int_bits(&entry->flags, (CACHE_INODE_DIR_POPULATED |
                                           CACHE_INODE_TRUST_CONTENT));
     *status = CACHE_INODE_SUCCESS;

     return *status;
}                               /* cache_inode_invalidate_all_cached_dirent */


/**
 *
 * cache_inode_operate_cached_dirent: locates a dirent in the cached dirent,
 * and perform an operation on it.
 *
 * Looks up for an dirent in the cached dirent. Thus function searches
 * only in the entries listed in the dir_entries array. Some entries
 * may be missing but existing and not be cached (if no readdir was
 * ever performed on the entry for example. This function provides a
 * way to operate on the dirent.
 *
 * @param pentry_parent [IN] directory entry to be searched.
 * @param name [IN] name for the searched entry.
 * @param newname [IN] newname if function is used to rename a dirent
 * @param pclient [INOUT] resource allocated by the client for the nfs management.
 * @param dirent_op [IN] operation (ADD, LOOKUP or REMOVE) to do on the dirent
 *        if found.
 *
 * @return returned status.
 *
 */
cache_inode_status_t
cache_inode_operate_cached_dirent(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  fsal_name_t * newname,
                                  cache_inode_client_t * pclient,
                                  cache_inode_dirent_op_t dirent_op)
{
     cache_inode_dir_entry_t dirent_key[1], *dirent, *dirent2, *dirent3;
     cache_inode_status_t pstatus = CACHE_INODE_SUCCESS;
     int code = 0;

     /* Sanity check */
     if(pentry_parent->type != DIRECTORY) {
         pstatus = CACHE_INODE_BAD_TYPE;
         goto out;
     }

     /* If no active entry, do nothing */
     if (pentry_parent->object.dir.nbactive == 0) {
       if (!((pentry_parent->flags & CACHE_INODE_TRUST_CONTENT) &&
             (pentry_parent->flags & CACHE_INODE_DIR_POPULATED))) {
         /* We cannot serve negative lookups. */
           /* pstatus == CACHE_INODE_SUCCESS; */
       } else {
           pstatus = CACHE_INODE_NOT_FOUND;
       }
       goto out;
     }

     FSAL_namecpy(&dirent_key->name, pname);
     dirent = cache_inode_avl_qp_lookup_s(pentry_parent, dirent_key, 1);
     if ((!dirent) || (dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
       if (!((pentry_parent->flags & CACHE_INODE_TRUST_CONTENT) &&
             (pentry_parent->flags & CACHE_INODE_DIR_POPULATED))) {
         /* We cannot serve negative lookups. */
         /* pstatus == CACHE_INODE_SUCCESS; */
       } else {
         pstatus = CACHE_INODE_NOT_FOUND;
       }
       goto out;
     }


     /* We perform operations anyway even if
        CACHE_INODE_TRUST_CONTENT is clear.  That way future upcalls
        can call in to this function to update the content to be
        correct.  We just don't ever return a not found or exists
        error. */

     switch (dirent_op) {
     case CACHE_INODE_DIRENT_OP_REMOVE:
         /* mark deleted */
         avl_dirent_set_deleted(pentry_parent, dirent);
         pentry_parent->object.dir.nbactive--;
         break;

     case CACHE_INODE_DIRENT_OP_RENAME:
         /* change the installed inode only the rename can succeed */
         FSAL_namecpy(&dirent_key->name, newname);
         dirent2 = cache_inode_avl_qp_lookup_s(pentry_parent,
                                               dirent_key, 1);
         if (dirent2) {
             /* rename would cause a collision */
             if (pentry_parent->flags &
                 CACHE_INODE_TRUST_CONTENT) {
                 /* We are not up to date. */
                 /* pstatus == CACHE_INODE_SUCCESS; */
             } else {
                 pstatus = CACHE_INODE_ENTRY_EXISTS;
             }
         } else {
             /* try to rename--no longer in-place */
             avl_dirent_set_deleted(pentry_parent, dirent);
             GetFromPool(dirent3, &pclient->pool_dir_entry,
                         cache_inode_dir_entry_t);
             FSAL_namecpy(&dirent3->name, newname);
             dirent3->flags = DIR_ENTRY_FLAG_NONE;
             dirent3->entry = dirent->entry;
             code = cache_inode_avl_qp_insert(pentry_parent, dirent3);
             switch (code) {
             case 0:
                 /* CACHE_INODE_SUCCESS */
                 break;
             case 1:
                 /* we reused an existing dirent, dirent has been deep
                  * copied, dispose it */
                 ReleaseToPool(dirent3, &pclient->pool_dir_entry);
                 /* CACHE_INODE_SUCCESS */
                 break;
             case -1:
                 /* collision, tree state unchanged (unlikely) */
                 pstatus = CACHE_INODE_ENTRY_EXISTS;
                 /* dirent is on persist tree, undelete it */
                 avl_dirent_clear_deleted(pentry_parent, dirent);
                 /* dirent3 was never inserted */
                 ReleaseToPool(dirent3, &pclient->pool_dir_entry);
             default:
                 LogCrit(COMPONENT_NFS_READDIR,
                         "DIRECTORY: insert error renaming dirent "
                         "(%s, %s)",
                         pname->name, newname->name);
                 pstatus = CACHE_INODE_INSERT_ERROR;
                 break;
             }
         } /* !found */
         break;

     default:
         /* Should never occur, in any case, it costs nothing to handle
          * this situation */
         pstatus = CACHE_INODE_INVALID_ARGUMENT;
         break;

     }                       /* switch */

out:
     return (pstatus);
}                               /* cache_inode_operate_cached_dirent */

/**
 *
 * @brief Adds a directory entry to a cached directory.
 *
 * A dirent pointing to a cache entry counts as an internal reference
 * to that entry, similar to the internal reference owned by the hash
 * table.  So when this function returns successfully,
 * pentry_added->refcount is increased by 1, but the increase is not
 * charged to the call path (and should not be returned until the
 * dirent becomes unreachable).
 *
 * Adds a directory entry to a cached directory. This is use when
 * creating a new entry through nfs and keep it to the cache. It also
 * allocates and caches the entry.  This function can be call
 * iteratively, within a loop (like what is done in
 * cache_inode_readdir_populate).  In this case, pentry_parent should
 * be set to the value returned in *pentry_next.  This function should
 * never be used for managing a junction.
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be
 *                              managed.
 * @param name          [IN]    name of the entry to add.
 * @param pentry_added  [IN]    the pentry added to the dirent array
 * @param pentry_next   [OUT]   the next pentry to use for next call.
 * @param pclient       [INOUT] resource allocated by the client for the nfs
 *                              management.
 * @param pstatus       [OUT]   returned status.
 *
 * @return the DIRECTORY that contain this entry in its array_dirent\n
 * @return NULL if failed, see *pstatus for error's meaning.
 *
 */
cache_inode_status_t
cache_inode_add_cached_dirent(cache_entry_t *pentry_parent,
                              fsal_name_t *pname,
                              cache_entry_t *pentry_added,
                              cache_inode_dir_entry_t **pnew_dir_entry,
                              cache_inode_client_t *pclient,
                              fsal_op_context_t *pcontext,
                              cache_inode_status_t *pstatus)
{
     cache_inode_dir_entry_t *new_dir_entry = NULL;
     int code = 0;

     *pstatus = CACHE_INODE_SUCCESS;

     /* Sanity check */
     if(pentry_parent->type != DIRECTORY) {
          *pstatus = CACHE_INODE_BAD_TYPE;
          return *pstatus;
     }

     /* in cache inode avl, we always insert on pentry_parent */
     GetFromPool(new_dir_entry, &pclient->pool_dir_entry,
                 cache_inode_dir_entry_t);
     if(new_dir_entry == NULL) {
          *pstatus = CACHE_INODE_MALLOC_ERROR;
          return *pstatus;
     }

     new_dir_entry->flags = DIR_ENTRY_FLAG_NONE;

     FSAL_namecpy(&new_dir_entry->name, pname);
     new_dir_entry->entry = pentry_added->weakref;

     /* add to avl */
     code = cache_inode_avl_qp_insert(pentry_parent, new_dir_entry);
     switch (code) {
     case 0:
         /* CACHE_INODE_SUCCESS */
         break;
     case 1:
         /* we reused an existing dirent, dirent has been deep
          * copied, dispose it */
         ReleaseToPool(new_dir_entry, &pclient->pool_dir_entry);
         /* CACHE_INODE_SUCCESS */
         break;
     default:
         /* collision, tree not updated--release both pool objects and return
          * err */
         ReleaseToPool(new_dir_entry, &pclient->pool_dir_entry);
         *pstatus = CACHE_INODE_ENTRY_EXISTS;
         return *pstatus;
         break;
     }

     if (pnew_dir_entry) {
         *pnew_dir_entry = new_dir_entry;
     }

     /* we're going to succeed */
     pentry_parent->object.dir.nbactive++;

     return *pstatus;
} /* cache_inode_add_cached_dirent */

/**
 *
 * cache_inode_remove_cached_dirent: Removes a directory entry to a cached
 * directory.
 *
 * Removes a directory entry to a cached directory. No MT safety managed here !!
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be
 * managed.
 * @param name [IN] name of the entry to remove.
 * @param pclient [INOUT] ressource allocated by the client for the nfs
 * management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_remove_cached_dirent(
    cache_entry_t * pentry_parent,
    fsal_name_t * pname,
    cache_inode_client_t * pclient,
    cache_inode_status_t * pstatus)
{

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  *pstatus = cache_inode_operate_cached_dirent(pentry_parent,
                                               pname,
                                               NULL,
                                               pclient,
                                               CACHE_INODE_DIRENT_OP_REMOVE);
  return (*pstatus);

}                               /* cache_inode_remove_cached_dirent */

/**
 *
 * @brief Cache complete directory contents
 *
 * This function fully reads a complete directory from the FSAL and
 * caches botht the names and associated entries.  The content lock
 * must be held on the directory being read.
 * safety managed here !!
 *
 * @param[in]     directory  Entry for the parent directory to be read
 * @param[in,out] client     Per-thread resource management structure
 * @param[in]     context    FSAL credentials
 * @param[out]    status     Returned status
 *
 */
cache_inode_status_t
cache_inode_readdir_populate(cache_entry_t *directory,
                             cache_inode_client_t *client,
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
                                              client,
                                              status) != CACHE_INODE_SUCCESS)
    return *status;

  /* Open the directory */
  dir_attributes.asked_attributes = client->attrmask;
  fsal_status = FSAL_opendir(&directory->handle,
                             context, &dir_handle, &dir_attributes);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *status = cache_inode_error_convert(fsal_status);
      if (fsal_status.major == ERR_FSAL_STALE) {
           cache_inode_kill_entry(directory, client);
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
                       client->attrmask,
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
              object_attributes.asked_attributes = client->attrmask;
              fsal_status
                = FSAL_readlink(&array_dirent[iter].handle,
                                context,
                                &create_arg.link_content, &object_attributes);

              if(FSAL_IS_ERROR(fsal_status))
                {
                     *status = cache_inode_error_convert(fsal_status);
                     if (fsal_status.major == ERR_FSAL_STALE) {
                          cache_inode_kill_entry(directory, client);
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
                                      client,
                                      context,
                                      CACHE_INODE_FLAG_NONE,
                                      status)) == NULL)
            goto bail;
          cache_status
            = cache_inode_add_cached_dirent(directory,
                                            &(array_dirent[iter].name),
                                            entry,
                                            &new_dir_entry,
                                            client,
                                            context,
                                            status);

          /* Once the weakref is stored in the directory entry, we
             can release the reference we took on the entry. */
          cache_inode_lru_unref(entry, client, 0);

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
  atomic_set_int_bits(&directory->flags,
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
 * cache_inode_readdir: Reads a directory.
 *
 * If content caching is enabled, iterate over the cached directory
 * entries (possibly after populating the cache) calling a callback
 * function for each one.  Otherwise call the FSAL directly.
 *
 * This is the only function in the cache_inode_readdir.c file that manages MT
 * safety on a directory cache entry.
 *
 * @param dir_entry [IN] Entry for the parent directory to be read.
 * @param policy [IN] Caching policy.
 * @param cookie [IN] Cookie for the readdir operation (basically the offset).
 * @param nbfound [OUT] Number of entries returned.
 * @param eod_met [OUT] A flag to know if end of directory was met
 *                      during this call.
 * @param client [INOUT] Ressource allocated by the client for the
 *                        nfs management.
 * @param context [IN] FSAL credentials
 * @param cb [IN] The callback function to receive entries
 * @param cb_opaque [IN] A pointerp assed as the first argument to cb
 * @param status [OUT] returned status.
 *
 * @return The same value as *status
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_BAD_TYPE if entry is not related to a directory
 * @retval CACHE_INODE_LRU_ERROR if allocation error occured when
 *                               validating the entry
 */
cache_inode_status_t
cache_inode_readdir(cache_entry_t * dir_entry,
                    uint64_t cookie,
                    unsigned int *nbfound,
                    bool_t *eod_met,
                    cache_inode_client_t *client,
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
     if (dir_entry->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          goto unlock_attrs;
     }

     cache_inode_lock_trust_attrs(dir_entry, context, client);
     /* Check if user (as specified by the credentials) is authorized to read
      * the directory or not */
     if (cache_inode_access_no_mutex(dir_entry,
                                     access_mask,
                                     client,
                                     context,
                                     status)
         != CACHE_INODE_SUCCESS) {
          goto unlock_attrs;
     }

     if (!((dir_entry->flags & CACHE_INODE_TRUST_CONTENT) &&
           (dir_entry->flags & CACHE_INODE_DIR_POPULATED))) {
          pthread_rwlock_wrlock(&dir_entry->content_lock);
          pthread_rwlock_unlock(&dir_entry->attr_lock);
          if (cache_inode_readdir_populate(dir_entry,
                                           client,
                                           context,
                                           status)
              != CACHE_INODE_SUCCESS) {
               goto unlock_dir;
          }
     } else {
          pthread_rwlock_rdlock(&dir_entry->content_lock);
          pthread_rwlock_unlock(&dir_entry->attr_lock);
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
          dirent = cache_inode_avl_lookup_k(dir_entry, cookie,
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
         dirent_node = avltree_first(&dir_entry->object.dir.avl.t);
     }

     LogFullDebug(COMPONENT_NFS_READDIR,
                  "About to readdir in cache_inode_readdir: pentry=%p "
                  "cookie=%"PRIu64" collisions %d",
                  dir_entry,
                  cookie,
                  dir_entry->object.dir.avl.collisions);

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
                                         client,
                                         LRU_REQ_SCAN))
              == NULL) {
               /* Entry fell out of the cache, load it back in. */
               if ((entry
                    = cache_inode_lookup_impl(dir_entry,
                                              &dirent->name,
                                              client,
                                              context,
                                              &lookup_status))
                   == NULL) {
                    if (lookup_status == CACHE_INODE_NOT_FOUND) {
                         /* Directory changed out from under us.
                            Invalidate it, skip the name, and keep
                            going. */
                         atomic_clear_int_bits(&dir_entry->flags,
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

          cache_inode_lock_trust_attrs(entry, context, client);
          in_result = cb(cb_opaque,
                         dirent->name.name,
                         &entry->handle,
                         &entry->attributes,
                         dirent->hk.k);
          (*nbfound)++;
          pthread_rwlock_unlock(&entry->attr_lock);
          cache_inode_lru_unref(entry, client, 0);
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

     pthread_rwlock_unlock(&dir_entry->content_lock);
     return *status;

unlock_attrs:

     pthread_rwlock_unlock(&dir_entry->attr_lock);
     return *status;
} /* cache_inode_readdir */
