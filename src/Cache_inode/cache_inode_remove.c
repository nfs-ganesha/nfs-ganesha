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
 *
 * @file cache_inode_remove.c
 * @brief Removes an entry of any type.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * @brief Checks if a directory is empty without a lock.
 *
 * This function checks if the supplied directory is empty.  The
 * caller must hold the content lock.
 *
 * @param[in] entry Entry to be checked (should be of type DIRECTORY)
 *
 * @retval CACHE_INODE_SUCCESS is directory is empty
 * @retval CACHE_INODE_BAD_TYPE is pentry is not of type DIRECTORY
 * @retval CACHE_INODE_DIR_NOT_EMPTY if pentry is not empty
 *
 */
cache_inode_status_t
cache_inode_is_dir_empty(cache_entry_t *entry)
{
     cache_inode_status_t status;

     /* Sanity check */
     if(entry->type != DIRECTORY) {
          return CACHE_INODE_BAD_TYPE;
     }

     status = (entry->object.dir.nbactive == 0) ?
          CACHE_INODE_SUCCESS :
          CACHE_INODE_DIR_NOT_EMPTY;

     return status;
} /* cache_inode_is_dir_empty */

/**
 *
 * @brief Checks if a directory is empty, acquiring lock
 *
 * This function checks if the supplied cache entry represents an
 * empty directory.  This function acquires the content lock, which
 * must not be held by the caller.
 *
 * @param[in] entry Entry to be checked (should be of type DIRECTORY)
 *
 * @retval CACHE_INODE_SUCCESS is directory is empty
 * @retval CACHE_INODE_BAD_TYPE is pentry is not of type DIRECTORY
 * @retval CACHE_INODE_DIR_NOT_EMPTY if pentry is not empty
 *
 */

cache_inode_status_t
cache_inode_is_dir_empty_WithLock(cache_entry_t *entry)
{
     cache_inode_status_t status;

     PTHREAD_RWLOCK_rdlock(&entry->content_lock);
     status = cache_inode_is_dir_empty(entry);
     PTHREAD_RWLOCK_unlock(&entry->content_lock);

     return status;
}                               /* cache_inode_is_dir_empty_WithLock */

/**
 * @brief Clean resources associated with entry
 *
 * This function frees the various resources associated wiith a cache
 * entry.
 *
 * @param[in] entry Entry to be cleaned
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */
cache_inode_status_t
cache_inode_clean_internal(cache_entry_t *entry)
{
     struct gsh_buffdesc val;
     struct gsh_buffdesc fh_desc;
     fsal_status_t fsal_status = {0, 0};
     hash_error_t rc = 0;

     if (! entry->obj_handle)
       goto unref;

     entry->obj_handle->ops->handle_to_key(entry->obj_handle,
					   &fh_desc);
     val.addr = entry;
     val.len = sizeof(cache_entry_t);

     rc = HashTable_DelSafe(fh_to_cache_entry_ht,
			    &fh_desc,
			    &val);

     /* Nonexistence is as good as success. */
     if ((rc != HASHTABLE_SUCCESS) &&
         (rc != HASHTABLE_ERROR_NO_SUCH_KEY)) {
          /* XXX this seems to logically prevent relcaiming the HashTable LRU
           * reference, and it seems to indicate a very serious problem */
          LogCrit(COMPONENT_CACHE_INODE,
                  "HashTable_Del error %d in cache_inode_clean_internal", rc);
          return CACHE_INODE_INCONSISTENT_ENTRY;
     }

/* release the handle object too
 */
     fsal_status = entry->obj_handle->ops->release(entry->obj_handle);
     if (FSAL_IS_ERROR(fsal_status)) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_lru_clean: Couldn't free FSAL ressources "
                  "fsal_status.major=%u", fsal_status.major);
     }

     entry->obj_handle = NULL;

 unref:
     /* Delete from the weakref table */
     cache_inode_weakref_delete(&entry->weakref);

     return CACHE_INODE_SUCCESS;
} /* cache_inode_clean_internal */

/**
 *
 * @brief Public function to remove a name from a directory.
 *
 * Removes a name from the supplied directory.  The caller should hold
 * no locks on the directory.
 *
 * @param[in] entry   Entry for the parent directory to be managed
 * @param[in] name    Name to be removed
 * @param[in] req_ctx Request context
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_remove(cache_entry_t *entry,
		   const char *name,
		   struct req_op_context *req_ctx)
{
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     fsal_accessflags_t access_mask = 0;

     /* Get the attribute lock and check access */
     PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
		    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD));

     status = cache_inode_access_sw(entry,
				    access_mask,
				    req_ctx,
				    false);
     if (status != CACHE_INODE_SUCCESS) {
	  PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	  return status;
     }

     /* Acquire the directory lock and remove the entry */

     PTHREAD_RWLOCK_wrlock(&entry->content_lock);

     status = cache_inode_remove_impl(entry,
				      name,
				      req_ctx,
				      CACHE_INODE_FLAG_ATTR_HAVE |
				      CACHE_INODE_FLAG_CONTENT_HAVE);

     return status;
}

/**
 * @brief Implement actual work of removing file
 *
 * Actually remove an entry from the directory.  Assume that the
 * directory contents and attributes are locked for writes.  The
 * attribute lock is released unless keep_md_lock is true.
 *
 * @param[in] entry   Entry for the parent directory to be managed.
 * @param[in] name    Name of the entry that we are looking for in the cache.
 * @param[in] req_ctx Request context
 * @param[in] flags   Flags to control lock retention
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_remove_impl(cache_entry_t *entry,
			const char *name,
			struct req_op_context *req_ctx,
			uint32_t flags)
{
     cache_entry_t *to_remove_entry = NULL;
     fsal_status_t fsal_status = {0, 0};
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
     cache_inode_status_t status = CACHE_INODE_SUCCESS;

     if(entry->type != DIRECTORY) {
	  if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
	      !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
		  PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	  }
          status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          PTHREAD_RWLOCK_rdlock(&entry->content_lock);
          flags |= CACHE_INODE_FLAG_CONTENT_HAVE;
     }

     /* Factor this somewhat.  In the case where the directory hasn't
        been populated, the entry may not exist in the cache and we'd
        be bringing it in just to dispose of it. */

     /* Looks up for the entry to remove */
     status = cache_inode_lookup_impl(entry,
				      name,
				      req_ctx,
				      &to_remove_entry);

     if (to_remove_entry == NULL) {
	 if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
	     !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
		 PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	 }
	 goto out;
     }

     if(!sticky_dir_allows(entry->obj_handle,
			   to_remove_entry->obj_handle,
			   req_ctx->creds)) {
         status = CACHE_INODE_FSAL_EPERM;
	 if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
	     !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
		 PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	 }
         goto out;
     }
     /* Lock the attributes (so we can decrement the link count) */
     PTHREAD_RWLOCK_wrlock(&to_remove_entry->attr_lock);

     LogDebug(COMPONENT_CACHE_INODE,
              "---> Cache_inode_remove : %s", name);


     saved_acl = entry->obj_handle->attributes.acl;
     fsal_status = entry->obj_handle->ops->unlink(entry->obj_handle, req_ctx,
                                                  name);
     if(!FSAL_IS_ERROR(fsal_status)) {
          /* Is this actually necessary?  We don't actually want the
             attributes copied, but the memcpy used by the
             FSAL shouldn't overlap. */
             fsal_status = entry->obj_handle->ops->getattrs(entry->obj_handle,                                                              req_ctx);
     }
     if (FSAL_IS_ERROR(fsal_status)) {
          status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry);
          }
	  if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
	      !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
		  PTHREAD_RWLOCK_unlock(&entry->attr_lock);
	  }
          goto unlock;
     } else {
          /* Decrement refcount on saved ACL */
          nfs4_acl_release_entry(saved_acl, &acl_status);
          if (acl_status != NFS_V4_ACL_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE,
                       "Failed to release old acl, status=%d",
                       acl_status);
          }
     }
     cache_inode_fixup_md(entry);

     if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
         !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
          PTHREAD_RWLOCK_unlock(&entry->attr_lock);
     }

     /* Remove the entry from parent dir_entries avl */
     cache_inode_remove_cached_dirent(entry, name);

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_remove_cached_dirent: status=%d", status);

     /* Update the attributes for the removed entry */
     fsal_status
          = to_remove_entry->obj_handle->ops
             ->getattrs(to_remove_entry->obj_handle, req_ctx);
     if(FSAL_IS_ERROR(fsal_status)) {
          if(fsal_status.major == ERR_FSAL_STALE)
               to_remove_entry->obj_handle->attributes.numlinks = 0;
     }

     if (cache_inode_refresh_attrs(to_remove_entry, req_ctx)
         != CACHE_INODE_SUCCESS) {
             goto unlock;
     }

     /* Now, delete "to_remove_entry" from the cache inode and free
        its associated resources, but only if numlinks == 0 */
     if (to_remove_entry->obj_handle->attributes.numlinks == 0) {
          /* Destroy the entry when everyone's references to it have
             been relinquished.  Most likely now. */
          PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);
          /* Kill off the sentinel reference (and mark the entry so
             it doesn't get recycled while a reference exists.) */
          cache_inode_lru_kill(to_remove_entry);
     } else {
     unlock:

          PTHREAD_RWLOCK_unlock(&to_remove_entry->attr_lock);
     }

out:
     if ((flags & CACHE_INODE_FLAG_CONTENT_HAVE) &&
         !(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          PTHREAD_RWLOCK_unlock(&entry->content_lock);
     }

     /* This is for the reference taken by lookup */
     if (to_remove_entry) {
         cache_inode_put(to_remove_entry);
     }

     return status;
}
/** @} */
