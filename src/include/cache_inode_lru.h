/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @defgroup Cache_inode Cache Inode
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
	uint64_t entries_lowat;
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
	uint32_t flags;
	uint64_t prev_fd_count; /* previous # of open fds */
	time_t prev_time; /* previous time the gc thread was run. */
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
 * Set on pinned (state-bearing) entries.
 */
#define LRU_ENTRY_PINNED  0x0001

/**
 * Set on LRU entries in the L2 (scanned and colder) queue.
 */
#define LRU_ENTRY_L2  0x0002

/**
 * Set on LRU entries that are being deleted
 */
#define LRU_ENTRY_CONDEMNED  0x0004

/**
 * Set if no more state may be granted.  Different from CONDEMNED in
 * that outstanding references may exist on the object, but it is no
 * longer reachable from the hash or weakref tables.
 */
#define LRU_ENTRY_UNPINNABLE  0x0008

/**
 * Flag indicating that cache_inode_lru_kill has already been called,
 * making it idempotent and fixing a possible unref leak.
 */
#define LRU_ENTRY_KILLED  0x0010

/**
 * The inode is marked for out-of-line cleanup (may still be reachable)
 */
#define LRU_ENTRY_CLEANUP  0x0020

/**
 * The caller is fetching an initial reference
 */
#define LRU_REQ_INITIAL  0x0040

/**
 * The caller is scanning the entry (READDIR)
 */
#define LRU_REQ_SCAN  0x0080

/**
 * The caller holds the lock on the LRU entry.
 */
#define LRU_FLAG_LOCKED  0x0100

/**
 * The entry is not initialized completely.
 */
static const uint32_t LRU_ENTRY_UNINIT = 0x0200;


/**
 * No further refs or state permitted.
 */
#define LRU_ENTRY_POISON \
    (LRU_ENTRY_CONDEMNED|LRU_ENTRY_KILLED|LRU_ENTRY_CLEANUP)

/**
 * The minimum reference count for a cache entry not being recycled.
 */

#define LRU_SENTINEL_REFCOUNT  1

#define LRU_STATE_NONE  0x00
#define LRU_STATE_RECLAIMING  0x01

/**
 * The number of lanes comprising a logical queue.  This must be
 * prime.
 */

#define LRU_N_Q_LANES  7

static const uint32_t LRU_NO_LANE = ~0;

extern int cache_inode_lru_pkginit(void);
extern int cache_inode_lru_pkgshutdown(void);

extern size_t open_fd_count;

cache_inode_status_t cache_inode_lru_get(struct cache_entry_t **entry,
					 uint32_t flags);
cache_inode_status_t cache_inode_lru_ref(
	cache_entry_t *entry,
	uint32_t flags) __attribute__((warn_unused_result));


/* XXX */
void cache_inode_lru_kill(cache_entry_t *entry);
void cache_inode_lru_cleanup_push(cache_entry_t *entry);

void cache_inode_lru_unref(cache_entry_t *entry,
				  uint32_t flags);
void lru_wake_thread(void);
cache_inode_status_t cache_inode_inc_pin_ref(cache_entry_t *entry);
void cache_inode_unpinnable(cache_entry_t *entry);
cache_inode_status_t cache_inode_dec_pin_ref(cache_entry_t *entry);
bool cache_inode_is_pinned(cache_entry_t *entry);

/**
 * Return true if there are FDs available to serve open requests,
 * false otherwise.  This function also wakes the LRU thread if the
 * current FD count is above the high water mark.
 */

static inline bool cache_inode_lru_fds_available(void)
{
	if ((open_fd_count >= lru_state.fds_hard_limit) && lru_state.caching_fds) {
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"FD Hard Limit Exceeded.  Disabling FD Cache and waking"
			" LRU thread.");
		lru_state.caching_fds = false;
		lru_wake_thread();
		return false;
	}
	if (open_fd_count >= lru_state.fds_hiwat) {
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
#endif /* CACHE_INODE_LRU_H */
/** @} */
