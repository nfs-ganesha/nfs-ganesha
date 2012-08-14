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

#include "abstract_atomic.h"
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
 * @param[in]  entry        The link to read
 * @param[out] link_content The location into which to write the
 *                          target
 * @param[in]  context      FSAL operation context
 * @param[out] status       Status of the operation
 *
 * @return CACHE_INODE_SUCCESS on success, other things on failure.
 */

cache_inode_status_t
cache_inode_readlink(cache_entry_t *entry,
                     fsal_path_t *link_content,
                     fsal_op_context_t *context,
                     cache_inode_status_t *status)
{
     fsal_status_t fsal_status = {0, 0};

     /* Set the return default to CACHE_INODE_SUCCESS */
     *status = CACHE_INODE_SUCCESS;

     if (entry->type != SYMBOLIC_LINK) {
          *status = CACHE_INODE_BAD_TYPE;
          return *status;
     }

     assert(entry->object.symlink);
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
               fsal_status
                    = FSAL_readlink(&entry->handle,
                                    context,
                                    &entry->object.symlink->content,
                                    NULL);
               if (!(FSAL_IS_ERROR(fsal_status))) {
                    atomic_set_uint32_t_bits(&entry->flags,
                                             CACHE_INODE_TRUST_CONTENT);
               }
          }
     }
     if (!(FSAL_IS_ERROR(fsal_status))) {
          FSAL_pathcpy(link_content,
                       &(entry->object.symlink->content));
     }
     pthread_rwlock_unlock(&entry->content_lock);

     if (FSAL_IS_ERROR(fsal_status)) {
          *status = cache_inode_error_convert(fsal_status);
          if (fsal_status.major == ERR_FSAL_STALE) {
               LogEvent(COMPONENT_CACHE_INODE,
                        "FSAL returned STALE from readlink");
               cache_inode_kill_entry(entry);
          }
          return *status;
     }

     return *status;
} /* cache_inode_readlink */
