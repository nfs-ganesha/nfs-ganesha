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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    cache_inode_readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/05 09:02:36 $
 * \version $Revision: 1.16 $
 * \brief   Reads a symlink.
 *
 * cache_inode_readlink.c : Reads a symlink.
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 * @brief Read the target of a symlink
 *
 * Copy the content of a symbolic link into the address pointed to by
 * link_content.
 *
 * @todo ACE: If policy changes, do we need to allocate a symlink
 *            object, or can we assume that either policy is immutable
 *            or that the allocation would take place at the site of
 *            policy change?
 *
 * @param entry [in] The link to read
 * @param link_content [out] The location into which to write the
 *                           target
 * @param client [in] Structure for resource management
 * @param context [in] FSAL operation context
 * @param status [out] Status of the operation.
 *
 * @return CACHE_INODE_SUCCESS on success, other things on failure.
 */

cache_inode_status_t cache_inode_readlink(cache_entry_t *entry,
                                          fsal_path_t *link_content,
                                          cache_inode_client_t *client,
                                          struct user_cred *creds,
                                          cache_inode_status_t *status)
{
     fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};
     uint32_t link_size = FSAL_MAX_PATH_LEN;

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     if (entry->type != SYMBOLIC_LINK) {
          *status = CACHE_INODE_BAD_TYPE;
          return *status;
     }

     pthread_rwlock_rdlock(&entry->content_lock);
     if (!(entry->flags & CACHE_INODE_TRUST_CONTENT)) {
          /* Our data are stale.  Drop the lock, get a
             write-lock, load in new data, and copy it out to
             the caller. */
          pthread_rwlock_unlock(&entry->content_lock);
          pthread_rwlock_wrlock(&entry->content_lock);
          /* Make sure nobody updated the content while we were
             waiting. */
          if (!(entry->flags & CACHE_INODE_TRUST_CONTENT)) {
               fsal_status = entry->obj_handle->ops->readlink(entry->obj_handle,
							      link_content->path,
							      &link_size,
							      TRUE); 
               if (!(FSAL_IS_ERROR(fsal_status))) {
                    atomic_set_int_bits(&entry->flags,
                                        CACHE_INODE_TRUST_CONTENT);
               }
          }
     } else {
	  fsal_status = entry->obj_handle->ops->readlink(entry->obj_handle,
							      link_content->path,
							      &link_size,
							      FALSE);
     }
     link_content->len = link_size; /* fake FSAL_pathcpy */
     pthread_rwlock_unlock(&entry->content_lock);

     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry, client);
          }
          return *status;
     }

     return *status;
} /* cache_inode_readlink */
