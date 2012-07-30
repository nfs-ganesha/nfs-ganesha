/**
 *
 * \file    cache_inode_remove.c
 * \date    $Date: 2006/01/31 10:18:58 $
 * \version $Revision: 1.32 $
 * \brief   Removes an entry of any type.
 */

/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "HashData.h"
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

     pthread_rwlock_rdlock(&entry->content_lock);
     status = cache_inode_is_dir_empty(entry);
     pthread_rwlock_unlock(&entry->content_lock);

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
     hash_buffer_t key, val;
     struct gsh_buffdesc fh_desc;
     fsal_status_t fsal_status = {0, 0};
     hash_error_t rc = 0;

     if (! entry->obj_handle)
       goto unref;

     entry->obj_handle->ops->handle_to_key(entry->obj_handle,
                                           &fh_desc);
     key.pdata = fh_desc.addr;
     key.len = fh_desc.len;
     val.pdata = entry;
     val.len = sizeof(cache_entry_t);

     rc = HashTable_DelSafe(fh_to_cache_entry_ht,
                            &key,
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
 * @param[in]  entry   Entry for the parent directory to be managed
 * @param[in]  name    Name to be removed
 * @param[out] attr    Attributes of the directory on success
 * @param[in]  context FSAL credentials
 * @param[out] status  Returned status
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 */

cache_inode_status_t
cache_inode_remove(cache_entry_t *entry,
                   const char *name,
                   struct attrlist *attr,
                   struct req_op_context *req_ctx,
                   cache_inode_status_t *status)
{
     cache_inode_status_t cache_status;
     fsal_accessflags_t access_mask = 0;

     /* Get the attribute lock and check access */
     pthread_rwlock_wrlock(&entry->attr_lock);

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD));

     if((*status
         = cache_inode_access_sw(entry,
                                 access_mask,
                                 req_ctx,
                                 &cache_status,
                                 FALSE))
        != CACHE_INODE_SUCCESS) {
          goto unlock_attr;
     }

     /* Acquire the directory lock and remove the entry */

     pthread_rwlock_wrlock(&entry->content_lock);

     cache_inode_remove_impl(entry,
                             name,
                             req_ctx,
                             status,
                             /* Keep the attribute lock so we can copy
                                attributes back to the caller.  I plan
                                to get rid of this later. --ACE */
                             CACHE_INODE_FLAG_ATTR_HAVE |
                             CACHE_INODE_FLAG_ATTR_HOLD |
                             CACHE_INODE_FLAG_CONTENT_HAVE);

     *attr = entry->obj_handle->attributes;

unlock_attr:

     pthread_rwlock_unlock(&entry->attr_lock);

     return *status;
}                               /* cache_inode_remove */

/**
 * @brief Implement actual work of removing file
 *
 * Actually remove an entry from the directory.  Assume that the
 * directory contents and attributes are locked for writes.  The
 * attribute lock is released unless keep_md_lock is TRUE.
 *
 * @param[in] entry   Entry for the parent directory to be managed.
 * @param[in] name    Name of the entry that we are looking for in the cache.
 * @param[in] context FSAL credentials
 * @param[in] status  Returned status
 * @param[in] flags   Flags to control lock retention
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */

cache_inode_status_t
cache_inode_remove_impl(cache_entry_t *entry,
                        const char *name,
                        struct req_op_context *req_ctx,
                        cache_inode_status_t *status,
                        uint32_t flags)
{
     cache_entry_t *to_remove_entry = NULL;
     fsal_status_t fsal_status = {0, 0};
#ifdef _USE_NFS4_ACL
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
#endif /* _USE_NFS4_ACL */

     if(entry->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     if (!(flags & CACHE_INODE_FLAG_CONTENT_HAVE)) {
          pthread_rwlock_rdlock(&entry->content_lock);
          flags |= CACHE_INODE_FLAG_CONTENT_HAVE;
     }

     /* Factor this somewhat.  In the case where the directory hasn't
        been populated, the entry may not exist in the cache and we'd
        be bringing it in just to dispose of it. */

     /* Looks up for the entry to remove */
     if ((to_remove_entry
          = cache_inode_lookup_impl(entry,
                                    name,
                                    req_ctx,
                                    status)) == NULL) {
          goto out;
     }

     if( !sticky_dir_allows(entry->obj_handle,
			    to_remove_entry->obj_handle,
			    req_ctx->creds)) {
	 *status = CACHE_INODE_FSAL_EPERM;
	 goto out;
     }
     /* Lock the attributes (so we can decrement the link count) */
     pthread_rwlock_wrlock(&to_remove_entry->attr_lock);

     LogDebug(COMPONENT_CACHE_INODE,
              "---> Cache_inode_remove : %s", name);


#ifdef _USE_NFS4_ACL
     saved_acl = entry->attributes.acl;
#endif /* _USE_NFS4_ACL */
     fsal_status = entry->obj_handle->ops->unlink(entry->obj_handle, name);
     if( !FSAL_IS_ERROR(fsal_status))
	  fsal_status = entry->obj_handle->ops->getattrs(entry->obj_handle,
							 &entry->obj_handle->attributes);

     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry);
          }
          goto unlock;
     } else {
#ifdef _USE_NFS4_ACL
          /* Decrement refcount on saved ACL */
          nfs4_acl_release_entry(saved_acl, &acl_status);
          if (acl_status != NFS_V4_ACL_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE,
                       "Failed to release old acl, status=%d",
                       acl_status);
          }
#endif /* _USE_NFS4_ACL */
     }
     cache_inode_fixup_md(entry);

     if ((flags & CACHE_INODE_FLAG_ATTR_HAVE) &&
         !(flags & CACHE_INODE_FLAG_ATTR_HOLD)) {
          pthread_rwlock_unlock(&entry->attr_lock);
     }

     /* Remove the entry from parent dir_entries avl */
     cache_inode_remove_cached_dirent(entry, name, status);

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_remove_cached_dirent: status=%d", *status);

     /* Update the attributes for the removed entry */
     fsal_status
	     = to_remove_entry->obj_handle->ops->getattrs(to_remove_entry->obj_handle,
							      &to_remove_entry->obj_handle->attributes);
     if(FSAL_IS_ERROR(fsal_status)) {
	     if(fsal_status.major == ERR_FSAL_STALE)
		     to_remove_entry->obj_handle->attributes.numlinks = 0;
     }

     /** @TODO this logic is a bit bogus in the new api.
      * First, we have the attributes already in hdl->attributes but
      * we copy them somewhere else just for giggles.  There are times when
      * we should get a copy but we should do to that up here under the attribute
      * lock and leave the poor fsal alone.  The logic of being a DIR and
      * its link count always being > 0 does make sense but the getattrs
      * method above will return ERR_FSAL_STALE if it is gone so hacking
      * numlinks to 0 is a hack.  We should just take the word of getattrs
      * and when it says the object is stale, it is stale as in dead, buried
      * and it's 401k eaten up by the relatives.  For now, until the whole
      * thing actually passes things like pynfs, leave this and fake the
      * numlinks.  Eventually, get rid of the whole attributes pointer
      * stuff, particularly the "do we support this attribute?" stuff
      * entirely and replace it with a static "this attr is valid and
      * no VFAT isn's suddenly going to sprout wings and fly". bits
      * for those cases where someone is really interested.  It's like
      * walking up to Grant's Tomb every day and asking who's inside...
      */
#if 0
     if ((to_remove_entry->type != DIRECTORY) &&
         (to_remove_entry->obj_handle->attributes.numlinks > 1)) {
          if ((*status = cache_inode_refresh_attrs(to_remove_entry))
              != CACHE_INODE_SUCCESS) {
               goto unlock;
          }
     } else {
          /* Otherwise our count is zero, or it was an empty
             directory. */
          to_remove_entry->obj_handle->attributes.numlinks = 0;
     }
#endif

     /* Now, delete "to_remove_entry" from the cache inode and free
        its associated resources, but only if numlinks == 0 */
     if (to_remove_entry->obj_handle->attributes.numlinks == 0) {
          /* Destroy the entry when everyone's references to it have
             been relinquished.  Most likely now. */
          pthread_rwlock_unlock(&to_remove_entry->attr_lock);
          /* Kill off the sentinel reference (and mark the entry so
             it doesn't get recycled while a reference exists.) */
          cache_inode_lru_kill(to_remove_entry);
     } else {
     unlock:

          pthread_rwlock_unlock(&to_remove_entry->attr_lock);
     }

out:
     if ((flags & CACHE_INODE_FLAG_CONTENT_HAVE) &&
         !(flags & CACHE_INODE_FLAG_CONTENT_HOLD)) {
          pthread_rwlock_unlock(&entry->content_lock);
     }

     /* This is for the reference taken by lookup */
     if (to_remove_entry)
       {
         cache_inode_put(to_remove_entry);
       }

     return *status;
}
