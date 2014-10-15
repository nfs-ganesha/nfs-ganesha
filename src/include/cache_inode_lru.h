/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor: Matt Benjamin <matt@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur: Philippe DENIEL   philippe.deniel@cea.fr
 *               Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
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
 * -------------
 */

/**
 * @addtogroup cache_inode
 * @{
 */

#ifndef CACHE_INODE_LRU_H
#define CACHE_INODE_LRU_H

#include "config.h"
#include "log.h"
#include "cache_inode.h"

/**
 * @file cache_inode_lru.h
 * @author Matt Benjamin
 * @brief Constant-time cache inode cache management implementation
 *
 * @section DESCRIPTION
 *
 * This module implements a constant-time cache management strategy
 * based on LRU.  Some ideas are taken from 2Q [Johnson and Shasha 1994]
 * and MQ [Zhou, Chen, Li 2004].  In this system, cache management does
 * interact with cache entry lifecycle.  Also, the cache size high- and
 * low- water mark management is maintained, but executes asynchronously
 * to avoid inline request delay.  Cache management operations execute in
 * constant time, as expected with LRU (and MQ).
 *
 * Cache entries in use by a currently-active protocol request (or other
 * operation) have a positive refcount, and threfore should not be present
 * at the cold end of an lru queue if the cache is well-sized.
 *
 * Cache entries with lock and open state are not eligible for collection
 * under ordinary circumstances, so are kept on a separate lru_pinned
 * list to retain constant time.
 *
 */

struct lru_state {
	uint64_t entries_hiwat;
	uint64_t entries_used;
	uint32_t fds_system_imposed;
	uint32_t fds_hard_limit;
	uint32_t fds_hiwat;
	uint32_t fds_lowat;
	/** This is the actual counter of 'futile' attempts at reaping
	    made  in a given time period.  When it reaches the futility
	    count, we turn off caching of file descriptors. */
	uint32_t futility;
	uint32_t per_lane_work;
	uint32_t biggest_window;
	uint64_t prev_fd_count;	/* previous # of open fds */
	time_t prev_time;	/* previous time the gc thread was run. */
	bool caching_fds;
};

extern struct lru_state lru_state;

/**
 * Flags for functions in the LRU package
 */

/**
 * No flag at all.
 */
#define LRU_FLAG_NONE  0x0000

/**
 * The caller holds the lock on the LRU entry.
 */
#define LRU_FLAG_LOCKED  0x0001

/**
 * The caller is fetching an initial reference
 */
#define LRU_REQ_INITIAL  0x0002

/**
 * The caller is doing something that doesn't care if entry is dead
 */
#define LRU_REQ_STALE_OK  0x0004

/**
 * qlane is locked
 */
#define LRU_UNREF_QLOCKED 0x0008

/**
 * The minimum reference count for a cache entry not being recycled.
 */

#define LRU_SENTINEL_REFCOUNT  1

/**
 * The number of lanes comprising a logical queue.  This must be
 * prime.
 */
#define LRU_N_Q_LANES  17

extern int cache_inode_lru_pkginit(void);
extern int cache_inode_lru_pkgshutdown(void);

extern size_t open_fd_count;

cache_inode_status_t cache_inode_lru_get(struct cache_entry_t **entry);
cache_inode_status_t cache_inode_lru_ref(cache_entry_t *entry, uint32_t flags);

/* XXX */
void cache_inode_lru_kill(cache_entry_t *entry);
void cache_inode_lru_cleanup_push(cache_entry_t *entry);
void cache_inode_lru_cleanup_try_push(cache_entry_t *entry);

void cache_inode_lru_unref(cache_entry_t *entry, uint32_t flags);
void cache_inode_lru_putback(cache_entry_t *entry, uint32_t flags);
void lru_wake_thread(void);
cache_inode_status_t cache_inode_inc_pin_ref(cache_entry_t *entry);
void cache_inode_unpinnable(cache_entry_t *entry);
void cache_inode_dec_pin_ref(cache_entry_t *entry, bool closefile);
bool cache_inode_is_pinned(cache_entry_t *entry);
void cache_inode_lru_kill_for_shutdown(cache_entry_t *entry);

/**
 *
 * @brief Release logical reference to a cache entry
 *
 * This function releases a logical reference to a cache entry
 * acquired by a previous call to cache_inode_get.
 *
 * The result is typically to decrement the reference count on entry,
 * but additional side effects include LRU adjustment, movement
 * to/from the protected LRU partition, or recyling if the caller has
 * raced an operation which made entry unreachable (and this current
 * caller has the last reference).  Caller MUST NOT make further
 * accesses to the memory pointed to by entry.
 *
 * @param[in] entry Cache entry being returned
 */
static inline void cache_inode_put(cache_entry_t *entry)
{
	cache_inode_lru_unref(entry, LRU_FLAG_NONE);
}

/**
 * Return true if there are FDs available to serve open requests,
 * false otherwise.  This function also wakes the LRU thread if the
 * current FD count is above the high water mark.
 */

static inline bool cache_inode_lru_fds_available(void)
{
	if ((atomic_fetch_size_t(&open_fd_count) >= lru_state.fds_hard_limit)
	    && lru_state.caching_fds) {
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"FD Hard Limit Exceeded.  Disabling FD Cache and waking"
			" LRU thread.");
		lru_state.caching_fds = false;
		lru_wake_thread();
		return false;
	}
	if (atomic_fetch_size_t(&open_fd_count) >= lru_state.fds_hiwat) {
		LogInfo(COMPONENT_CACHE_INODE_LRU,
			"FDs above high water mark, waking LRU thread.");
		lru_wake_thread();
	}

	return true;
}

/**
 * Return true if we are currently caching file descriptors.
 */

static inline bool cache_inode_lru_caching_fds(void)
{
	return lru_state.caching_fds;
}
#endif				/* CACHE_INODE_LRU_H */
/** @} */
