/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @addtogroup cache_inode
 * @{
 */

/**
 * @file    cache_inode_invalidate.c
 * @brief   Invalidate the cached data on a cache entry
 *
 */

#include "config.h"
#include "abstract_atomic.h"
#include "log.h"
#include "hashtable.h"
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
 * @brief invalidates an entry in the cache
 *
 * This function invalidates the related cache entry correponding to a
 * FSAL handle. It is designed to be called when an FSAL upcall is
 * triggered.
 *
 * @param[in] entry  FSAL handle for the entry to be invalidated
 * @param[in] flags  Control flags
 *
 * @retval CACHE_INODE_SUCCESS if operation is a success
 * @retval CACHE_INODE_INVALID_ARGUMENT bad parameter(s) as input
 * @retval CACHE_INODE_STATE_CONFLICT if invalidating this entry would
 *                                    result is state conflict
 * @retval Other errors shows a FSAL error.
 */

cache_inode_status_t
cache_inode_invalidate(cache_entry_t *entry, uint32_t flags)
{
	cache_inode_status_t status = CACHE_INODE_SUCCESS;

	if (!(flags & CACHE_INODE_INVALIDATE_GOT_LOCK))
		PTHREAD_RWLOCK_wrlock(&entry->attr_lock);

	/* We can invalidate entries with state just fine.  We force
	   cache_inode to contact the FSAL for any use of content or
	   attributes, and if the FSAL indicates the entry is stale,
	   it can be disposed of then. */

	/* We should have a way to invalidate content and attributes
	   separately.  Or at least a way to invalidate attributes
	   without invalidating content (since any change in content
	   really ought to modify mtime, at least.) */

	if (flags & CACHE_INODE_INVALIDATE_ATTRS)
		atomic_clear_uint32_t_bits(&entry->flags,
					   CACHE_INODE_TRUST_ATTRS);

	if (flags & CACHE_INODE_INVALIDATE_CONTENT)
		atomic_clear_uint32_t_bits(&entry->flags,
					   CACHE_INODE_TRUST_CONTENT |
					   CACHE_INODE_DIR_POPULATED);

	/* lock order requires that we release entry->attr_lock before
	 * calling cache_inode_close! */
	if (!(flags & CACHE_INODE_INVALIDATE_GOT_LOCK))
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);

	if (((flags & CACHE_INODE_INVALIDATE_CLOSE) != 0)
	    && (entry->type == REGULAR_FILE))
		status = cache_inode_close(entry, CACHE_INODE_FLAG_REALLYCLOSE);

	/* Memory copying attributes with every call is expensive.
	   Let's not do it.  */

	return status;
}				/* cache_inode_invalidate */

/** @} */
