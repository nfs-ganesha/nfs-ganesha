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
 * @file    cache_inode_readlink.c
 * @brief   Reads a symlink.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "abstract_atomic.h"
#include "log.h"
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
 * @todo ACE: Fix this to remove the grotesque buffer hack as part of
 * callbackification.
 *
 * @param[in]  entry        The link to read
 * @param[out] link_content The location into which to write the
 *                          target
 * @param[in]  context      FSAL operation context
 *
 * @return CACHE_INODE_SUCCESS on success, other things on failure.
 */

cache_inode_status_t
cache_inode_readlink(cache_entry_t *entry,
                     struct gsh_buffdesc *link_content,
		     struct req_op_context *req_ctx)
{
     cache_inode_status_t status = CACHE_INODE_SUCCESS;
     fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};

     if (entry->type != SYMBOLIC_LINK) {
          status = CACHE_INODE_BAD_TYPE;
          return status;
     }

     PTHREAD_RWLOCK_rdlock(&entry->content_lock);
     if (!(entry->flags & CACHE_INODE_TRUST_CONTENT)) {
          /* Our data are stale.  Drop the lock, get a
             write-lock, load in new data, and copy it out to
             the caller. */
          PTHREAD_RWLOCK_unlock(&entry->content_lock);
          PTHREAD_RWLOCK_wrlock(&entry->content_lock);
          /* Make sure nobody updated the content while we were
             waiting. */
          bool refresh = !(entry->flags & CACHE_INODE_TRUST_CONTENT);

          fsal_status = entry->obj_handle->ops->readlink(entry->obj_handle,
                  req_ctx,
                  link_content->addr,
                  &link_content->len,
                  refresh);

          if (refresh && !(FSAL_IS_ERROR(fsal_status))) {
              atomic_set_uint32_t_bits(&entry->flags,
                      CACHE_INODE_TRUST_CONTENT);
          }

     } else {
             fsal_status = entry->obj_handle->ops->readlink(
                                                         entry->obj_handle,
                                                         req_ctx,
                                                         link_content->addr,
                                                         &link_content->len,
                                                         false);
     }
     PTHREAD_RWLOCK_unlock(&entry->content_lock);

     if (FSAL_IS_ERROR(fsal_status)) {
          status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               cache_inode_kill_entry(entry);
          }
          return status;
     }

     return status;
}
/** @} */
