/**
 *
 * \file    cache_inode_remove.c
 * \author  $Author: leibovic $
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

#include "LRU_List.h"
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * cache_inode_is_dir_empty: checks if a directory is empty or not. No mutex management.
 *
 * Checks if a directory is empty or not. No mutex management
 *
 * @param pentry [IN] entry to be checked (should be of type DIRECTORY)
 *
 * @retval CACHE_INODE_SUCCESS is directory is empty\n
 * @retval CACHE_INODE_BAD_TYPE is pentry is not of type DIRECTORY
 * @retval CACHE_INODE_DIR_NOT_EMPTY if pentry is not empty
 *
 */
cache_inode_status_t cache_inode_is_dir_empty(cache_entry_t *pentry)
{
     cache_inode_status_t status;

     /* Sanity check */
     if(pentry->type != DIRECTORY) {
          return CACHE_INODE_BAD_TYPE;
     }

     status = (pentry->object.dir.nbactive == 0) ?
          CACHE_INODE_SUCCESS :
          CACHE_INODE_DIR_NOT_EMPTY;

     return status;
}                               /* cache_inode_is_dir_empty */

/**
 *
 * cache_inode_is_dir_empty_WithLock: checks if a directory is empty or not, BUT has lock management.
 *
 * Checks if a directory is empty or not, BUT has lock management.
 *
 * @param pentry [IN] entry to be checked (should be of type DIRECTORY)
 *
 * @return CACHE_INODE_SUCCESS is directory is empty\n
 * @return CACHE_INODE_BAD_TYPE is pentry is not of type DIRECTORY\n
 * @return CACHE_INODE_DIR_NOT_EMPTY if pentry is not empty
 *
 */
cache_inode_status_t cache_inode_is_dir_empty_WithLock(cache_entry_t * pentry)
{
     cache_inode_status_t status;

     pthread_rwlock_rdlock(&pentry->content_lock);
     status = cache_inode_is_dir_empty(pentry);
     pthread_rwlock_unlock(&pentry->content_lock);

     return status;
}                               /* cache_inode_is_dir_empty_WithLock */

/**
 * @brief Clean resources associated with entry
 *
 * This function frees the various resources associated wiith a cache
 * entry.
 *
 * @param entry [in] Entry to be cleaned
 * @param client [in,out] Structure for per-thread resource management
 *
 * @return CACHE_INODE_SUCCESS or various errors
 */
cache_inode_status_t
cache_inode_clean_internal(cache_entry_t *entry,
                           cache_inode_client_t *client)
{
     hash_buffer_t key, val;
     hash_error_t rc = 0;

     key.pdata = entry->fh_desc.start;
     key.len = entry->fh_desc.len;


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

     /* Delete from the weakref table */
     cache_inode_weakref_delete(&entry->weakref);

     if (entry->type == SYMBOLIC_LINK) {
          pthread_rwlock_wrlock(&entry->content_lock);
          cache_inode_release_symlink(entry, &client->pool_entry_symlink);
          pthread_rwlock_unlock(&entry->content_lock);
     }

     return CACHE_INODE_SUCCESS;
} /* cache_inode_clean_internal */

/**
 *
 * @brief Public function to remove a name from a directory.
 *
 * Removes a pentry addressed by its parent pentry and its FSAL name.
 *
 * @param pentry [IN] Entry for the parent directory to be managed
 * @param pnode_name [IN] Name to be removed
 * @param pattr [OUT] Attributes of the directory on success
 * @param pclient [INOUT] Ressource allocated by the client for NFS management
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] Returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */

cache_inode_status_t cache_inode_remove(cache_entry_t *pentry,
                                        fsal_name_t *pnode_name,
                                        fsal_attrib_list_t *pattr,
                                        cache_inode_client_t *pclient,
                                        fsal_op_context_t *pcontext,
                                        cache_inode_status_t *pstatus)
{
     cache_inode_status_t status;
     fsal_accessflags_t access_mask = 0;

     /* Get the attribute lock and check access */
     pthread_rwlock_wrlock(&pentry->attr_lock);

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD));

     if((*pstatus
         = cache_inode_access_sw(pentry,
                                 access_mask,
                                 pclient,
                                 pcontext,
                                 &status,
                                 FALSE))
        != CACHE_INODE_SUCCESS) {
          goto unlock_attr;
     }

     /* Acquire the directory lock and remove the entry */

     pthread_rwlock_wrlock(&pentry->content_lock);

     cache_inode_remove_impl(pentry,
                             pnode_name,
                             pclient,
                             pcontext,
                             pstatus,
                             /* Keep the attribute lock so we can copy
                                attributes back to the caller.  I plan
                                to get rid of this later. --ACE */
                             CACHE_INODE_FLAG_ATTR_HAVE |
                             CACHE_INODE_FLAG_ATTR_HOLD |
                             CACHE_INODE_FLAG_CONTENT_HAVE);

     *pattr = pentry->attributes;

unlock_attr:

     pthread_rwlock_unlock(&pentry->attr_lock);

     return *pstatus;
}                               /* cache_inode_remove */

/**
 *
 * @brief Implement actual work of removing file
 *
 * Actually remove an entry from the directory.  Assume that the
 * directory contents and attributes are locked for writes.  The
 * attribute lock is released unless keep_md_lock is TRUE.
 *
 * @param entry [IN] Entry for the parent directory to be managed.
 * @param name [IN] Name of the entry that we are looking for in the cache.
 * @param client [INOUT] Ressource allocated by the client for NFS management.
 * @param context [IN] FSAL credentials
 * @param status [OUT] Returned status.
 * @param flags [IN] Flags to control lock retention
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when
 *                               validating the entry
 *
 */
cache_inode_status_t
cache_inode_remove_impl(cache_entry_t *entry,
                        fsal_name_t *name,
                        cache_inode_client_t *client,
                        fsal_op_context_t *context,
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
                                    client,
                                    context,
                                    status)) == NULL) {
          goto out;
     }

     /* Lock the attributes (so we can decrement the link count) */
     pthread_rwlock_wrlock(&to_remove_entry->attr_lock);

     LogDebug(COMPONENT_CACHE_INODE,
              "---> Cache_inode_remove : %s", name->name);


#ifdef _USE_NFS4_ACL
     saved_acl = entry->attributes.acl;
#endif /* _USE_NFS4_ACL */
     fsal_status = FSAL_unlink(&entry->handle,
                               name,
                               context,
                               &entry->attributes);

     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry, client);
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
     cache_inode_remove_cached_dirent(entry, name, client, status);

     LogFullDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_remove_cached_dirent: status=%d", *status);

     /* Update the attributes for the removed entry */

     if ((to_remove_entry->type != DIRECTORY) &&
         (to_remove_entry->attributes.numlinks > 1)) {
          if ((*status = cache_inode_refresh_attrs(to_remove_entry,
                                                   context,
                                                   client))
              != CACHE_INODE_SUCCESS) {
               goto unlock;
          }
     } else {
          /* Otherwise our count is zero, or it was an empty
             directory. */
          to_remove_entry->attributes.numlinks = 0;
     }

     /* Now, delete "to_remove_entry" from the cache inode and free
        its associated resources, but only if numlinks == 0 */
     if (to_remove_entry->attributes.numlinks == 0) {
          /* Destroy the entry when everyone's references to it have
             been relinquished.  Most likely now. */
          pthread_rwlock_unlock(&to_remove_entry->attr_lock);
          /* This unref is for the sentinel */
          if ((*status =
               cache_inode_lru_unref(to_remove_entry,
                                     client,
                                     0)) != CACHE_INODE_SUCCESS) {
               goto out;
          }
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
         cache_inode_put(to_remove_entry, client);
       }

     return *status;
}
