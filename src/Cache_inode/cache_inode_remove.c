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

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "nfs_exports.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

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
     hash_error_t rc = 0;

     if (entry->fh_desc.start == 0)
         return CACHE_INODE_SUCCESS;

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
          PTHREAD_RWLOCK_WRLOCK(&entry->content_lock);
          cache_inode_release_symlink(entry);
          PTHREAD_RWLOCK_UNLOCK(&entry->content_lock);
     }

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
                   fsal_name_t *name,
                   fsal_attrib_list_t *attr,
                   fsal_op_context_t *context,
                   cache_inode_status_t *status)
{
     cache_entry_t *to_remove_entry = NULL;
     cache_inode_status_t tmp_status;
     cache_inode_status_t status_ref_entry = CACHE_INODE_SUCCESS;
     fsal_status_t fsal_status = {0, 0};

     if(entry->type != DIRECTORY) {
          *status = CACHE_INODE_BAD_TYPE;
          return *status;
     }

     /* Get the attribute lock and check access */
     PTHREAD_RWLOCK_WRLOCK(&entry->attr_lock);

     /* Factor this somewhat.  In the case where the directory hasn't
        been populated, the entry may not exist in the cache and we'd
        be bringing it in just to dispose of it. */

     /* Looks up for the entry to remove */
     if ((to_remove_entry
          = cache_inode_lookup_impl(entry,
                                    name,
                                    context,
                                    status)) == NULL) {
          goto out;
     }

     LogDebug(COMPONENT_CACHE_INODE,
              "Remove %s", name->name);

     fsal_status = FSAL_unlink(&entry->handle,
                               name,
                               context,
                               NULL);

     status_ref_entry = cache_inode_refresh_attrs(entry, context);

     if(FSAL_IS_ERROR(fsal_status)) {
         *status = cache_inode_error_convert(fsal_status);
         LogDebug(COMPONENT_CACHE_INODE,
                  "FSAL_unlink returned %s",
                  cache_inode_err_str(*status));
         goto out;
     }

     /* Update the attributes for the removed entry */
     (void)cache_inode_refresh_attrs_locked(to_remove_entry, context);

     if ((*status = status_ref_entry) != CACHE_INODE_SUCCESS) { 
         LogDebug(COMPONENT_CACHE_INODE,
                  "cache_inode_refresh_attrs_locked(entry %p) returned %s",
                  entry,
                  cache_inode_err_str(status_ref_entry));
         goto out;
     }

     cache_inode_fixup_md(entry);

     /* Acquire the directory lock and remove the entry */

     PTHREAD_RWLOCK_WRLOCK(&entry->content_lock);

     /* Remove the entry from parent dir_entries avl */
     if(cache_inode_remove_cached_dirent(entry,
                                         name,
                                         &tmp_status)
        != CACHE_INODE_SUCCESS)
       {
         LogDebug(COMPONENT_CACHE_INODE,
                  "remove entry failed with status %s",
                  cache_inode_err_str(tmp_status));
         cache_inode_invalidate_all_cached_dirent(entry,
                                                  &tmp_status);
       }

     PTHREAD_RWLOCK_UNLOCK(&entry->content_lock);

out:

     /* This is for the reference taken by lookup */
     if (to_remove_entry)
       {
         cache_inode_put(to_remove_entry);
       }

     *attr = entry->attributes;
     set_mounted_on_fileid(entry, attr, context->export_context->fe_export);

     PTHREAD_RWLOCK_UNLOCK(&entry->attr_lock);

     return *status;
}                               /* cache_inode_remove */
