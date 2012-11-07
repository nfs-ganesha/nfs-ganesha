/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
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

#ifndef _CACHE_INODE_LRU_H
#define _CACHE_INODE_LRU_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "log.h"
#include "cache_inode.h"

/**
 *
 * \file cache_inode_lru.h
 * \author Matt Benjamin
 * \brief Constant-time cache inode cache management implementation
 *
 * \section DESCRIPTION
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

struct lru_state
{
     uint64_t entries_hiwat;
     uint64_t entries_lowat;
     uint32_t fds_system_imposed;
     uint32_t fds_hard_limit;
     uint32_t fds_hiwat;
     uint32_t fds_lowat;
     /* This is the actual counter of 'futile' attempts at reaping
        made  in a given time period.  When it reaches the futility
        count, we turn off caching of file descriptors. */
     uint32_t futility;
     uint32_t per_lane_work;
     uint32_t biggest_window;
     uint32_t flags;
     uint64_t prev_fd_count; /* previous # of open fds */
     time_t prev_time; /* previous time the gc thread was run. */
     uint64_t threadwait;
     bool_t caching_fds;
};

extern struct lru_state lru_state;

/* Flags for functions in the LRU package */

/**
 * No flag at all.
 */
static const uint32_t LRU_FLAG_NONE = 0x0000;

/**
 * Set on pinned (state-bearing) entries.
 */
static const uint32_t LRU_ENTRY_PINNED = 0x0001;

/**
 * Set on LRU entries in the L2 (scanned and colder) queue.
 */
static const uint32_t LRU_ENTRY_L2 = 0x0002;

/**
 * Set on LRU entries that are being deleted
 */
static const uint32_t LRU_ENTRY_CONDEMNED = 0x0004;

/**
 * Set if no more state may be granted.  Different from CONDEMNED in
 * that outstanding references may exist on the object, but it is no
 * longer reachable from the hash or weakref tables.
 */
static const uint32_t LRU_ENTRY_UNPINNABLE = 0x0008;

/**
 * Flag indicating that cache_inode_lru_kill has already been called,
 * making it idempotent and fixing a possible unref leak.
 */
static const uint32_t LRU_ENTRY_KILLED = 0x0010;

/**
 * The caller is fetching an initial reference
 */
static const uint32_t LRU_REQ_INITIAL = 0x0020;

/**
 * The caller is scanning the entry (READDIR)
 */
static const uint32_t LRU_REQ_SCAN = 0x0040;

/**
 * The caller holds the lock on the LRU entry.
 */
static const uint32_t LRU_FLAG_LOCKED = 0x0080;

/**
 * The entry is not initialized completely.
 */
static const uint32_t LRU_ENTRY_UNINIT = 0x0100;

/* The minimum reference count for a cache entry not being recycled. */

static const int32_t LRU_SENTINEL_REFCOUNT = 1;

static const uint32_t LRU_STATE_NONE = 0x00;
static const uint32_t LRU_STATE_RECLAIMING = 0x01;

static const uint32_t LRU_SLEEPING = 0x00000001;
static const uint32_t LRU_SHUTDOWN = 0x00000002;


/* The number of lanes comprising a logical queue.  This must be
   prime. */

#define LRU_N_Q_LANES 7

static const uint32_t LRU_NO_LANE = ~0;

extern void cache_inode_lru_pkginit(void);
extern void cache_inode_lru_pkgshutdown(void);

extern size_t open_fd_count;

extern struct cache_entry_t *cache_inode_lru_get(cache_inode_status_t *status,
                                                 uint32_t flags);
extern cache_inode_status_t cache_inode_lru_ref(
     cache_entry_t *entry,
     uint32_t flags) __attribute__((warn_unused_result));
extern void cache_inode_lru_kill(cache_entry_t *entry);
extern void cache_inode_lru_unref(cache_entry_t *entry,
                                  uint32_t flags);
extern void lru_wake_thread(uint32_t flags);
extern cache_inode_status_t cache_inode_inc_pin_ref(cache_entry_t *entry);
extern void cache_inode_unpinnable(cache_entry_t *entry);
extern cache_inode_status_t cache_inode_dec_pin_ref(cache_entry_t *entry, unsigned char closefile);
extern bool_t cache_inode_is_pinned(cache_entry_t *entry);

/**
 * Return TRUE if there are FDs available to serve open requests,
 * FALSE otherwise.  This function also wakes the LRU thread if the
 * current FD count is above the high water mark.
 */

static inline bool_t
cache_inode_lru_fds_available(void)
{
     if ((open_fd_count >= lru_state.fds_hard_limit) && lru_state.caching_fds) {
          LogCrit(COMPONENT_CACHE_INODE_LRU,
                  "FD Hard Limit Exceeded.  Disabling FD Cache and waking"
                  " LRU thread.");
          lru_state.caching_fds = FALSE;
          lru_wake_thread(LRU_FLAG_NONE);
          return FALSE;
     }
     if (open_fd_count >= lru_state.fds_hiwat) {
          LogInfo(COMPONENT_CACHE_INODE_LRU,
                  "FDs above high water mark, waking LRU thread.");
          lru_wake_thread(LRU_FLAG_NONE);
     }

     return TRUE;
}

/**
 * Return true if we are currently caching file descriptors.
 */

static inline bool_t
cache_inode_lru_caching_fds(void)
{
     return lru_state.caching_fds;
}
#endif /* _CACHE_INODE_LRU_H */
