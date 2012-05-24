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
 * \file    cache_inode_link.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.16 $
 * \brief   Creation of an hardlink.
 *
 * cache_inode_link.c : Creation of an hardlink.
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
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * cache_inode_link: hardlinks a pentry to another.
 *
 * Hard links a pentry to another. This is basically a equivalent of
 * FSAL_link in the cache inode layer.
 *
 * The caller has at least initial reference to pentry_src and
 * pentry_dir_dest.  No refcount increment is charged to the caller.
 *
 * @param pentry_src [IN] entry pointer the entry to be linked. This
 *                        can't be a directory.
 * @param pentry_dir_dest [INOUT] entry pointer for the destination
 *                                directory in which the link will be
 *                                created.
 * @param plink_name [IN] pointer to the name of the object in the
 *                        destination directory.
 * @param pattr [OUT] attributes for the linked attributes after the
 *                    operation.
 * @param pclient [INOUT] ressource allocated by the client for the
 *                        nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when
 *                               validating the entry
 * @return CACHE_INODE_BAD_TYPE either source or destination have
 *                              incorrect type
 * @return CACHE_INODE_ENTRY_EXISTS entry of that name already exists
 *                                  in destination.
 */
cache_inode_status_t cache_inode_link(cache_entry_t * pentry_src,
                                      cache_entry_t * pentry_dir_dest,
                                      fsal_name_t * plink_name,
                                      fsal_attrib_list_t * pattr,
                                      cache_inode_client_t * pclient,
                                      struct user_cred *creds,
                                      cache_inode_status_t * pstatus)
{
     fsal_status_t fsal_status = {0, 0};
     bool_t srcattrlock = FALSE;
     bool_t destattrlock = FALSE;
     bool_t destdirlock = FALSE;
     fsal_accessflags_t access_mask = 0;
#ifdef _USE_NFS4_ACL
     fsal_acl_t *saved_acl = NULL;
     fsal_acl_status_t acl_status = 0;
#endif /* _USE_NFS4_ACL */


     /* Set the return default to CACHE_INODE_SUCCESS */
     *pstatus = CACHE_INODE_SUCCESS;

     /* The file to be hardlinked can't be a DIRECTORY */
     if (pentry_src->type == DIRECTORY) {
          *pstatus = CACHE_INODE_BAD_TYPE;
          goto out;
     }


     /* Is the destination a directory? */
     if ((pentry_dir_dest->type != DIRECTORY) &&
         (pentry_dir_dest->type != FS_JUNCTION)) {
          *pstatus = CACHE_INODE_BAD_TYPE;
          goto out;
     }

     /* Acquire the attribute lock */
     pthread_rwlock_wrlock(&pentry_dir_dest->attr_lock);
     destattrlock = TRUE;

     /* Check if caller is allowed to perform the operation */
     access_mask = (FSAL_MODE_MASK_SET(FSAL_W_OK) |
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE));

     if ((*pstatus = cache_inode_access_sw(pentry_dir_dest,
                                           access_mask,
                                           pclient,
                                           creds,
                                           pstatus,
                                           FALSE))
         != CACHE_INODE_SUCCESS) {
          goto out;
     }

     /* Rather than performing a lookup first, just try to make the
        link and return the FSAL's error if it fails. */

     pthread_rwlock_wrlock(&pentry_src->attr_lock);
     srcattrlock = TRUE;

     if ((pentry_src->type == UNASSIGNED) ||
         (pentry_src->type == RECYCLED)) {
          LogCrit(COMPONENT_CACHE_INODE,
                  "Invalid source type: type=%d, line %d in file %s",
                  pentry_src->type, __LINE__, __FILE__);
          goto out;
     }

     /* Acquire the directory entry lock */
     pthread_rwlock_wrlock(&pentry_dir_dest->content_lock);
     destdirlock = TRUE;

     /* Do the link at FSAL level */
#ifdef _USE_NFS4_ACL
     saved_acl = pentry_src->attributes.acl;
#endif /* _USE_NFS4_ACL */
     fsal_status = pentry_src->obj_handle->ops->link(pentry_src->obj_handle,
						     pentry_dir_dest->obj_handle,
						     plink_name);
     if (FSAL_IS_ERROR(fsal_status)) {
          *pstatus = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               fsal_attrib_list_t attrs;
               attrs.asked_attributes = pclient->attrmask;
	       fsal_status = pentry_src->obj_handle->ops->getattrs(pentry_src->obj_handle,
								   &attrs);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(pentry_src,
                                           pclient);
               }
               attrs.asked_attributes = pclient->attrmask;
	       fsal_status = pentry_dir_dest->obj_handle->ops->getattrs(pentry_dir_dest->obj_handle,
									&attrs);
               if (fsal_status.major == ERR_FSAL_STALE) {
                    cache_inode_kill_entry(pentry_dir_dest,
                                           pclient);
               }
          }
          goto out;
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

     cache_inode_fixup_md(pentry_src);
     *pattr = pentry_src->obj_handle->attributes;
     pthread_rwlock_unlock(&pentry_src->attr_lock);
     srcattrlock = FALSE;

     /* Reload the destination directory's attributes so the caller
        will have an updated changeid. */
     cache_inode_refresh_attrs(pentry_dir_dest, pclient);
     pthread_rwlock_unlock(&pentry_dir_dest->attr_lock);
     destattrlock = FALSE;

     /* Add the new entry in the destination directory */
     if (cache_inode_add_cached_dirent(pentry_dir_dest,
                                       plink_name,
                                       pentry_src,
                                       NULL,
                                       pclient,
                                       pstatus) != CACHE_INODE_SUCCESS) {
          goto out;
     }

     pthread_rwlock_unlock(&pentry_dir_dest->content_lock);
     destdirlock = FALSE;

out:

     if (srcattrlock) {
          pthread_rwlock_unlock(&pentry_src->attr_lock);
     }

     if (destattrlock) {
          pthread_rwlock_unlock(&pentry_dir_dest->attr_lock);
     }

     if (destdirlock) {
          pthread_rwlock_unlock(&pentry_dir_dest->content_lock);
     }

     return *pstatus;
}
