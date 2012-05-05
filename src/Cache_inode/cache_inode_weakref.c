/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * -------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stuff_alloc.h"
#include "nlm_list.h"
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "cache_inode_weakref.h"
#include "generic_weakref.h"

/**
 *
 * \file cache_inode_weakref.c
 * \author Matt Benjamin
 * \author Adam C. Emerson
 * \brief Cache inode weak reference package
 *
 * \section DESCRIPTION
 *
 * Manage weak references to cache inode objects (e.g., references from
 * directory entries).
 */

#define WEAKREF_PARTITIONS 17

static gweakref_table_t *cache_inode_wt = NULL;

/**
 * @brief Init package
 *
 * Create the global reference table.
 */
void cache_inode_weakref_init()
{
    cache_inode_wt = gweakref_init(17);
}

/**
 * @brief Install an entry in the weakref table
 *
 * This function installs an entry in the weakref table.  The caller
 * must already hold a reference to it.  It is expected this function
 * will only be called by cache_inode_new_entry.
 */

gweakref_t cache_inode_weakref_insert(cache_entry_t *entry)
{
    return (gweakref_insert(cache_inode_wt, entry));
}

/**
 * @brief Get a reference from the weakref
 *
 * Attempt to get a reference on a weakref.  In order to prevent a
 * race condition, the function retains the read lock on the table
 * (blocking any delete) and acquires the mutex on the lru entry
 * before releasing it.  If the entry has type RECYCLED or
 * cache_inode_lru_ref fails (which it will if the refcount has
 * dropped to 0) it will act as if the entry has not existed.
 *
 * @see cache_inode_lru_unref for the other half of the story.
 *
 * @param ref [in] The weakref from which to derive a reference
 * @param client [in] The per-thread resource management structure
 * @param flags [in] Flags passed in to cache_inode_lru_ref, for scan
 *                   resistance
 *
 * @return A pointer to the cache_entry_t on success, or NULL on
 *         failure.
 */

cache_entry_t *cache_inode_weakref_get(gweakref_t *ref,
                                       cache_inode_client_t *client,
                                       uint32_t flags)
{
    pthread_rwlock_t *lock = NULL;
    cache_entry_t *entry =
        (cache_entry_t *) gweakref_lookupex(cache_inode_wt, ref, &lock);

    if (entry) {
        if (cache_inode_lru_ref(entry, client, flags)
            != CACHE_INODE_SUCCESS) {
            pthread_rwlock_unlock(lock);
            return NULL;
        }
        pthread_rwlock_unlock(lock);
    }

    return (entry);
}

/**
 * @brief Delete a reference from the table
 *
 * This function deletes a weak reference and is expected to be used
 * only by cache_inode_lru_unref, cache_inode_get, and
 * cache_inode_kill_entry.
 *
 * @todo ACE: Should cache_inode_kill entry actually use it?
 *            Probably, since we don't want people getting more
 *            references on bad filehandles, even if we are waiting
 *            for people who have references on them to relinquish
 *            them before deallocating.
 *
 * @param ref [in] The entry to delete
 */

void cache_inode_weakref_delete(gweakref_t *ref)
{
    gweakref_delete(cache_inode_wt, ref);
}

/**
 * @brief Clean up on shutdown
 *
 * Destroy the weakref table and free all resources associated with
 * it.
 */
void cache_inode_weakref_shutdown()
{
    gweakref_destroy(cache_inode_wt);
}
