/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file fsal_up_thread.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "HashTable.h"
#include "fsal_up.h"
#include "sal_functions.h"

/**
 * @brief Invalidate cached attributes and content
 *
 * We call into the cache and invalidate at once, since the operation
 * is inexpensive by design.
 *
 * @param[in] invalidate Invalidation parameters
 * @param[in] file       The file to invalidate
 *
 * @retval 0 on success.
 * @retval ENOENT if the entry is not in the cache.  (Harmless, since
 *         if it's not cached, there's nothing to invalidate.)
 */

static int
invalidate_imm(struct fsal_up_event_invalidate *invalidate,
               struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "Calling cache_inode_invalidate()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }

        return rc;
}

/**
 * @brief Signal a lock grant
 *
 * Since the SAL has its own queue for such operations, we simply
 * queue there.
 *
 * @param[in] grant Details of the granted lock
 * @param[in] file  File on which the lock is granted
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int
lock_grant_imm(struct fsal_up_event_lock_grant *grant,
               struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "calling cache_inode_get()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                LogDebug(COMPONENT_FSAL_UP,
                         "Lock Grant found entry %p",
                         entry);

                grant_blocked_lock_upcall(entry,
                                          grant->lock_owner,
                                          &grant->lock_param);

                if (entry) {
                        cache_inode_put(entry);
                }
        }

        return rc;
}

/**
 * @brief Signal lock availability
 *
 * Since the SAL has its own queue for such operations, we simply
 * queue there.
 *
 * @param[in] avail Details of the available lock
 * @param[in] file  File on which the lock has become available
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int
lock_avail_imm(struct fsal_up_event_lock_avail *avail,
               struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                LogDebug(COMPONENT_FSAL_UP,
                         "Lock Grant found entry %p",
                         entry);

                available_blocked_lock_upcall(entry,
                                              avail->lock_owner,
                                              &avail->lock_param);

                if (entry) {
                        cache_inode_put(entry);
                }
        }

        return rc;
}


struct fsal_up_vector fsal_up_top = {
        .lock_grant_imm = lock_grant_imm,
        .lock_grant_queue = NULL,

        .lock_avail_imm = lock_avail_imm,
        .lock_avail_queue = NULL,

        .invalidate_imm = invalidate_imm,
        .invalidate_queue = NULL,

        .layoutrecall_imm = NULL,
        .layoutrecall_queue = NULL
};
