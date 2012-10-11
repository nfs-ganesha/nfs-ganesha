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

int
fsal_up_invalidate_imm(struct fsal_up_event_invalidate *invalidate,
                       struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_invalidate()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }

        return rc;
}

int
fsal_up_lock_grant_imm(struct fsal_up_event_lock_grant *grant,
                       struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_get()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                LogDebug(COMPONENT_FSAL_UP,
                         "FSAL_UP_DUMB: Lock Grant found entry %p",
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

struct fsal_up_vector fsal_up_top = {
        .lock_grant_imm = fsal_up_lock_grant_imm,
        .lock_grant_queue = NULL,

        .invalidate_imm = fsal_up_invalidate_imm,
        .invalidate_queue = NULL,

        .layoutrecall_imm = NULL,
        .layoutrecall_queue = NULL
};
