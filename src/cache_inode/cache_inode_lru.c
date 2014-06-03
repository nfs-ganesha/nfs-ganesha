/*
 * Vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/**
 * @addtogroup cache_inode
 * @{
 */

#include "config.h"
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <misc/timespec.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "fsal.h"
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "abstract_atomic.h"
#include "cache_inode_hash.h"
#include "gsh_intrinsic.h"
#include "sal_functions.h"

/**
 *
 * @file cache_inode_lru.c
 * @author Matt Benjamin <matt@linuxbox.com>
 * @brief Constant-time cache inode cache management implementation
 */

/**
 * @page LRUOverview LRU Overview
 *
 * This module implements a constant-time cache management strategy
 * based on LRU.  Some ideas are taken from 2Q [Johnson and Shasha 1994]
 * and MQ [Zhou, Chen, Li 2004].  In this system, cache management does
 * interact with cache entry lifecycle, but the lru queue is not a garbage
 * collector. Most imporantly, cache management operations execute in constant
 * time, as expected with LRU (and MQ).
 *
 * Cache entries in use by a currently-active protocol request (or other
 * operation) have a positive refcount, and threfore should not be present
 * at the cold end of an lru queue if the cache is well-sized.
 *
 * Cache entries with lock and open state are not eligible for collection
 * under ordinary circumstances, so are kept on a separate lru_pinned
 * list to retain constant time.
 *
 * As noted below, initial references to cache entries may only be granted
 * under the cache inode hash table latch.  Likewise, entries must first be
 * made unreachable to the cache inode hash table, then independently reach
 * a refcnt of 0, before they may be disposed or recycled.
 */

struct lru_state lru_state;

/**
 * A single queue structure.
 */

struct lru_q {
	struct glist_head q;	/* LRU is at HEAD, MRU at tail */
	enum lru_q_id id;
	uint64_t size;
};


/**
 * A single queue lane, holding both movable and pinned entries.
 */

struct lru_q_lane {
	struct lru_q L1;
	struct lru_q L2;
	struct lru_q pinned;	/* uncollectable, due to state */
	struct lru_q cleanup;	/* deferred cleanup */
	pthread_mutex_t mtx;
	/* LRU thread scan position */
	struct {
		bool active;
		struct glist_head *glist;
		struct glist_head *glistn;
	} iter;
	struct {
		char *func;
		uint32_t line;
	} locktrace;
	 CACHE_PAD(0);
};

#define QLOCK(qlane) \
	do { \
		pthread_mutex_lock(&(qlane)->mtx); \
		(qlane)->locktrace.func = (char *) __func__; \
		(qlane)->locktrace.line = __LINE__; \
	} while (0)

#define QUNLOCK(qlane) \
	pthread_mutex_unlock(&(qlane)->mtx)

/**
 * A multi-level LRU algorithm inspired by MQ [Zhou].  Transition from
 * L1 to L2 implies various checks (open files, etc) have been
 * performed, so ensures they are performed only once.  A
 * correspondence to the "scan resistance" property of 2Q and MQ is
 * accomplished by recycling/clean loads onto the LRU of L1.  Async
 * processing onto L2 constrains oscillation in this algorithm.
 */

static struct lru_q_lane LRU[LRU_N_Q_LANES];

/**
 * This is a global counter of files opened by cache_inode.  This is
 * preliminary expected to go away.  Problems with this method are
 * that it overcounts file descriptors for FSALs that don't use them
 * for open files, and, under the Lieb Rearchitecture, FSALs will be
 * responsible for caching their own file descriptors, with
 * interfaces for Cache_Inode to interrogate them as to usage or
 * instruct them to close them.
 */

size_t open_fd_count = 0;

/**
 * The refcount mechanism distinguishes 3 key object states:
 *
 * 1. unreferenced (unreachable)
 * 2. unincremented, but reachable
 * 3. incremented
 *
 * It seems most convenient to make unreferenced correspond to refcount==0.
 * Then refcount==1 is a SENTINEL_REFCOUNT in which the only reference to
 * the entry is the set of functions which can grant new references.  An
 * object with refcount > 1 has been referenced by some thread, which must
 * release its reference at some point.
 *
 * More specifically, in the current implementation, reachability is
 * serialized by the cache lookup table latch.
 *
 * Currently, I propose to distinguish between objects with positive refcount
 * and objects with state.  The latter could be evicted, in the normal case,
 * only with loss of protocol correctness, but may have only the sentinel
 * refcount.  To preserve constant time operation, they are stored in an
 * independent partition of the LRU queue.
 */

static pthread_mutex_t lru_mtx;
static struct fridgethr *lru_fridge;

enum lru_edge {
	LRU_HEAD,		/* LRU */
	LRU_TAIL		/* MRU */
};

static const uint32_t FD_FALLBACK_LIMIT = 0x400;

/* Some helper macros */
#define LRU_NEXT(n) \
	(atomic_inc_uint32_t(&(n)) % LRU_N_Q_LANES)

/* Delete lru, use iif the current thread is not the LRU
 * thread.  The node being removed is lru, glist a pointer to L1's q,
 * qlane its lane. */
#define LRU_DQ_SAFE(lru, q) \
	do { \
		if ((lru)->qid == LRU_ENTRY_L1) { \
			struct lru_q_lane *qlane = &LRU[(lru)->lane]; \
			if (unlikely((qlane->iter.active) && \
				     ((&(lru)->q) == qlane->iter.glistn))) { \
				qlane->iter.glistn = (lru)->q.next; \
			} \
		} \
		glist_del(&(lru)->q); \
		--((q)->size); \
	} while (0)

#define LRU_ENTRY_L1_OR_L2(e) \
	(((e)->lru.qid == LRU_ENTRY_L2) || \
	 ((e)->lru.qid == LRU_ENTRY_L1))

#define LRU_ENTRY_RECLAIMABLE(e, n) \
	(LRU_ENTRY_L1_OR_L2(e) && \
	((n) == LRU_SENTINEL_REFCOUNT+1) && \
	 ((e)->fh_hk.inavl))

/**
 * @brief Initialize a single base queue.
 *
 * This function initializes a single queue partition (L1, L1 pinned, L2,
 * etc)
 */
static inline void
lru_init_queue(struct lru_q *q, enum lru_q_id qid)
{
	glist_init(&q->q);
	q->id = qid;
	q->size = 0;
}

static inline void
lru_init_queues(void)
{
	int ix;

	pthread_mutex_init(&lru_mtx, NULL);

	for (ix = 0; ix < LRU_N_Q_LANES; ++ix) {
		struct lru_q_lane *qlane = &LRU[ix];

		/* one mutex per lane */
		pthread_mutex_init(&qlane->mtx, NULL);

		/* init iterator */
		qlane->iter.active = false;

		/* init lane queues */
		lru_init_queue(&LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&LRU[ix].pinned, LRU_ENTRY_PINNED);
		lru_init_queue(&LRU[ix].cleanup, LRU_ENTRY_CLEANUP);
	}
}

/**
 * @brief Return a pointer to the current queue of entry
 *
 * This function returns a pointer to the queue on which entry is linked,
 * or NULL if entry is not on any queue.
 *
 * The lane lock corresponding to entry is LOCKED.
 *
 * @param[in] entry  The entry.
 *
 * @return A pointer to entry's current queue, NULL if none.
 */
static inline struct lru_q *
lru_queue_of(cache_entry_t *entry)
{
	struct lru_q *q;

	switch (entry->lru.qid) {
	case LRU_ENTRY_PINNED:
		q = &LRU[(entry->lru.lane)].pinned;
		break;
	case LRU_ENTRY_L1:
		q = &LRU[(entry->lru.lane)].L1;
		break;
	case LRU_ENTRY_L2:
		q = &LRU[(entry->lru.lane)].L2;
		break;
	case LRU_ENTRY_CLEANUP:
		q = &LRU[(entry->lru.lane)].cleanup;
		break;
	default:
		/* LRU_NO_LANE */
		q = NULL;
		break;
	}			/* switch */

	return q;
}

/**
 * @brief Get the appropriate lane for a cache_entry
 *
 * This function gets the LRU lane by taking the modulus of the
 * supplied pointer.
 *
 * @param[in] entry  A pointer to a cache entry
 *
 * @return The LRU lane in which that entry should be stored.
 */
static inline uint32_t
lru_lane_of_entry(cache_entry_t *entry)
{
	return (uint32_t) (((uintptr_t) entry) % LRU_N_Q_LANES);
}

/**
 * @brief Insert an entry into the specified queue and lane
 *
 * This function determines the queue corresponding to the supplied
 * lane and flags, inserts the entry into that queue, and updates the
 * entry to hold the flags and lane.
 *
 * The caller MUST NOT hold a lock on the queue lane.
 *
 * @param[in] entry  The entry to insert
 * @param[in] q      The queue to insert on
 * @param[in] lane   The lane corresponding to entry address
 * @param[in] edge   One of LRU_HEAD (LRU) or LRU_TAIL (MRU)
 */
static inline void
lru_insert_entry(cache_entry_t *entry, struct lru_q *q,
		 uint32_t lane, enum lru_edge edge)
{
	cache_inode_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lane];

	lru->lane = lane;	/* permanently fix lane */
	lru->qid = q->id;	/* initial */

	QLOCK(qlane);

	switch (edge) {
	case LRU_HEAD:
		glist_add(&q->q, &lru->q);
		break;
	case LRU_TAIL:
	default:
		glist_add_tail(&q->q, &lru->q);
		break;
	}
	++(q->size);

	QUNLOCK(qlane);
}

/**
 * @brief pin an entry
 *
 * Pins an entry.  The corresponding q lane is LOCKED.  The entry is NOT
 * on the CLEANUP queue.
 *
 * @param[in] entry  The entry to pin
 * @param[in] entry  Its qlane (which we just computed)
 * @param[in] flags  (TBD)
 */
static inline void
cond_pin_entry(cache_entry_t *entry, uint32_t flags)
{
	cache_inode_lru_t *lru = &entry->lru;

	if (!(lru->qid == LRU_ENTRY_PINNED)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ_SAFE(lru, q);

		/* in with the new */
		lru->qid = LRU_ENTRY_PINNED;
		q = &LRU[(lru->lane)].pinned;
		glist_add(&q->q, &lru->q);
		++(q->size);

	} /* ! PINNED  (&& !CLEANUP) */
}

/**
 * @brief Clean an entry for recycling.
 *
 * This function cleans an entry up before it's recycled or freed.
 *
 * @param[in] entry  The entry to clean
 */
static inline void
cache_inode_lru_clean(cache_entry_t *entry)
{
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

	if (is_open(entry)) {
		cache_status =
		    cache_inode_close(entry,
				      CACHE_INODE_FLAG_REALLYCLOSE |
				      CACHE_INODE_FLAG_NOT_PINNED);
		if (cache_status != CACHE_INODE_SUCCESS) {
			LogCrit(COMPONENT_CACHE_INODE_LRU,
				"Error closing file in cleanup: %d.",
				cache_status);
		}
	}

	if (entry->type == DIRECTORY)
		cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);

	/* Free FSAL resources */
	if (entry->obj_handle) {
		entry->obj_handle->ops->release(entry->obj_handle);
		entry->obj_handle = NULL;
	}

	/* Clean out the export mapping before deconstruction */
	clean_mapping(entry);

	/* Finalize last bits of the cache entry */
	cache_inode_key_delete(&entry->fh_hk.key);
	pthread_rwlock_destroy(&entry->content_lock);
	pthread_rwlock_destroy(&entry->state_lock);
	pthread_rwlock_destroy(&entry->attr_lock);
}

/**
 * @brief Try to pull an entry off the queue
 *
 * This function examines the end of the specified queue and if the
 * entry found there can be re-used, it returns with the entry
 * locked.  Otherwise, it returns NULL.  The caller MUST NOT hold a
 * lock on the queue when this function is called.
 *
 * This function follows the locking discipline detailed above.  it
 * returns an lru entry removed from the queue system and which we are
 * permitted to dispose or recycle.
 */

static uint32_t reap_lane;

static inline cache_inode_lru_t *
lru_reap_impl(enum lru_q_id qid)
{
	uint32_t lane;
	struct lru_q_lane *qlane;
	struct lru_q *lq;
	cache_inode_lru_t *lru;
	cache_entry_t *entry;
	uint32_t refcnt;
	cih_latch_t latch;
	int ix;

	lane = LRU_NEXT(reap_lane);
	for (ix = 0; ix < LRU_N_Q_LANES; ++ix, lane = LRU_NEXT(reap_lane)) {
		qlane = &LRU[lane];
		lq = (qid == LRU_ENTRY_L1) ? &qlane->L1 : &qlane->L2;

		QLOCK(qlane);
		lru = glist_first_entry(&lq->q, cache_inode_lru_t, q);
		if (!lru)
			goto next_lane;
		refcnt = atomic_inc_int32_t(&lru->refcnt);
		entry = container_of(lru, cache_entry_t, lru);
		if (unlikely(refcnt != (LRU_SENTINEL_REFCOUNT + 1))) {
			/* cant use it. */
			cache_inode_lru_unref(entry, LRU_UNREF_QLOCKED);
			goto next_lane;
		}
		/* potentially reclaimable */
		QUNLOCK(qlane);
		/* entry must be unreachable from CIH when recycled */
		if (cih_latch_entry
		    (entry, &latch, CIH_GET_WLOCK, __func__, __LINE__)) {
			QLOCK(qlane);
			refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
			/* there are two cases which permit reclaim,
			 * entry is:
			 * 1. reachable but unref'd (refcnt==2)
			 * 2. unreachable, being removed (plus refcnt==0)
			 *  for safety, take only the former
			 */
			if (LRU_ENTRY_RECLAIMABLE(entry, refcnt)) {
				/* it worked */
				struct lru_q *q = lru_queue_of(entry);
				cih_remove_latched(entry, &latch,
						   CIH_REMOVE_QLOCKED);
				LRU_DQ_SAFE(lru, q);
				entry->lru.qid = LRU_ENTRY_NONE;
				QUNLOCK(qlane);
				cih_latch_rele(&latch);
				goto out;
			}
			cih_latch_rele(&latch);
			/* return the ref we took above--unref deals
			 * correctly with reclaim case */
			cache_inode_lru_unref(entry, LRU_UNREF_QLOCKED);
		} else {
			/* ! QLOCKED */
			continue;
		}
 next_lane:
		QUNLOCK(qlane);
	}			/* foreach lane */

	/* ! reclaimable */
	lru = NULL;
 out:
	return lru;
}

static inline cache_inode_lru_t *
lru_try_reap_entry(void)
{
	cache_inode_lru_t *lru;

	if (lru_state.entries_used < lru_state.entries_hiwat)
		return NULL;

	lru = lru_reap_impl(LRU_ENTRY_L2);
	if (!lru)
		lru = lru_reap_impl(LRU_ENTRY_L1);

	return lru;
}

/**
 * @brief Push a cache_inode_killed entry to the cleanup queue
 * for out-of-line cleanup
 *
 * This function appends entry to the appropriate lane of the
 * global cleanup queue, and marks the entry.
 *
 * @param[in] entry  The entry to clean
 */
void
cache_inode_lru_cleanup_push(cache_entry_t *entry)
{
	cache_inode_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];

	QLOCK(qlane);

	if (!(lru->qid == LRU_ENTRY_CLEANUP)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ_SAFE(lru, q);

		/* in with the new */
		lru->qid = LRU_ENTRY_CLEANUP;
		q = &qlane->cleanup;
		glist_add(&q->q, &lru->q);
		++(q->size);
	}

	QUNLOCK(qlane);
}

/**
 * @brief Push an entry to the cleanup queue that may be unexported
 * for out-of-line cleanup
 *
 * This routine is used to try pushing a cache inode into the cleanup
 * queue. If the entry ends up with another LRU reference before this
 * is accomplished, then don't push it to cleanup.
 *
 * This will be used when unexporting an export. Any cache inode entry
 * that only belonged to that export is a candidate for cleanup.
 * However, it is possible the entry is still accessible via another
 * export, and an LRU reference might be gained before we can lock the
 * AVL tree. In that case, the entry must be left alone (thus
 * cache_inode_kill_entry is NOT suitable for this purpose).
 *
 * @param[in] entry  The entry to clean
 */
void cache_inode_lru_cleanup_try_push(cache_entry_t *entry)
{
	cache_inode_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	cih_latch_t latch;

	QLOCK(qlane);

	if (cih_latch_entry(entry, &latch, CIH_GET_WLOCK,
			    __func__, __LINE__)) {
		uint32_t refcnt;
		refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
		/* there are two cases which permit reclaim,
		 * entry is:
		 * 1. reachable but unref'd (refcnt==2)
		 * 2. unreachable, being removed (plus refcnt==0)
		 *    for safety, take only the former
		 */
		if (LRU_ENTRY_RECLAIMABLE(entry, refcnt)) {
			/* it worked */
			struct lru_q *q = lru_queue_of(entry);
			cih_remove_latched(entry, &latch,
					   CIH_REMOVE_QLOCKED);
			LRU_DQ_SAFE(lru, q);
			entry->lru.qid = LRU_ENTRY_CLEANUP;
		}

		cih_latch_rele(&latch);
	}

	QUNLOCK(qlane);
}

/**
 * @brief Function that executes in the lru thread
 *
 * This function performs long-term reorganization, compaction, and
 * other operations that are not performed in-line with referencing
 * and dereferencing.
 *
 * This function is responsible for deferred cleanup of cache entries
 * killed in request or upcall (or most other) contexts.
 *
 * This function is responsible for cleaning the FD cache.  It works
 * by the following rules:
 *
 *  - If the number of open FDs is below the low water mark, do
 *    nothing.
 *
 *  - If the number of open FDs is between the low and high water
 *    mark, make one pass through the queues, and exit.  Each pass
 *    consists of taking an entry from L1, examining to see if it is a
 *    regular file not bearing state with an open FD, closing the open
 *    FD if it is, and then moving it to L2.  The advantage of the two
 *    level system is twofold: First, seldom used entries congregate
 *    in L2 and the promotion behaviour provides some scan
 *    resistance.  Second, once an entry is examined, it is moved to
 *    L2, so we won't examine the same cache entry repeatedly.
 *
 *  - If the number of open FDs is greater than the high water mark,
 *    we consider ourselves to be in extremis.  In this case we make a
 *    number of passes through the queue not to exceed the number of
 *    passes that would be required to process the number of entries
 *    equal to a biggest_window percent of the system specified
 *    maximum.
 *
 *  - If we are in extremis, and performing the maximum amount of work
 *    allowed has not moved the open FD count required_progress%
 *    toward the high water mark, increment lru_state.futility.  If
 *    lru_state.futility reaches futility_count, temporarily disable
 *    FD caching.
 *
 *  - Every time we wake through timeout, reset futility_count to 0.
 *
 *  - If we fall below the low water mark and FD caching has been
 *    temporarily disabled, re-enable it.
 *
 * This function uses the lock discipline for functions accessing LRU
 * entries through a queue partition.
 *
 * @param[in] ctx Fridge context
 */

#define CL_FLAGS \
	(CACHE_INODE_FLAG_REALLYCLOSE| \
	 CACHE_INODE_FLAG_NOT_PINNED| \
	 CACHE_INODE_FLAG_CONTENT_HAVE| \
	 CACHE_INODE_FLAG_CONTENT_HOLD)

static void
lru_run(struct fridgethr_context *ctx)
{
	/* Index */
	size_t lane = 0;
	/* True if we were explicitly awakened. */
	bool woke = ctx->woke;
	/* Finalized */
	uint32_t fdratepersec = 1, fds_avg, fddelta;
	float fdnorm, fdwait_ratio, fdmulti;
	time_t threadwait = fridgethr_getwait(ctx);
	/* True if we are taking extreme measures to reclaim FDs */
	bool extremis = false;
	/* Total work done in all passes so far.  If this exceeds the
	 * window, stop.
	 */
	size_t totalwork = 0;
	uint64_t totalclosed = 0;
	/* The current count (after reaping) of open FDs */
	size_t currentopen = 0;
	struct lru_q *q;

	SetNameFunction("cache_lru");

	fds_avg = (lru_state.fds_hiwat - lru_state.fds_lowat) / 2;

	if (cache_param.use_fd_cache)
		extremis = (atomic_fetch_size_t(&open_fd_count) >
			    lru_state.fds_hiwat);

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "LRU awakes.");

	if (!woke) {
		/* If we make it all the way through a timed sleep
		   without being woken, we assume we aren't racing
		   against the impossible. */
		lru_state.futility = 0;
	}

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "lru entries: %zu",
		     lru_state.entries_used);

	/* Reap file descriptors.  This is a preliminary example of the
	   L2 functionality rather than something we expect to be
	   permanent.  (It will have to adapt heavily to the new FSAL
	   API, for example.) */

	if ((atomic_fetch_size_t(&open_fd_count) < lru_state.fds_lowat)
	    && cache_param.use_fd_cache) {
		LogDebug(COMPONENT_CACHE_INODE_LRU,
			 "FD count is %zd and low water mark is "
			 "%d: not reaping.",
			 atomic_fetch_size_t(&open_fd_count),
			 lru_state.fds_lowat);
		if (cache_param.use_fd_cache
		    && !lru_state.caching_fds) {
			lru_state.caching_fds = true;
			LogEvent(COMPONENT_CACHE_INODE_LRU,
				 "Re-enabling FD cache.");
		}
	} else {
		/* The count of open file descriptors before this run
		   of the reaper. */
		size_t formeropen = atomic_fetch_size_t(&open_fd_count);
		/* Work done in the most recent pass of all queues.  if
		   value is less than the work to do in a single queue,
		   don't spin through more passes. */
		size_t workpass = 0;

		time_t curr_time = time(NULL);
		fdratepersec =
		    (curr_time <=
		     lru_state.prev_time) ? 1 : (formeropen -
						 lru_state.prev_fd_count) /
		    (curr_time - lru_state.prev_time);

		LogFullDebug(COMPONENT_CACHE_INODE_LRU,
			     "fdrate:%u fdcount:%zd slept for %" PRIu64 " sec",
			     fdratepersec, formeropen,
			     curr_time - lru_state.prev_time);

		if (extremis) {
			LogDebug(COMPONENT_CACHE_INODE_LRU,
				 "Open FDs over high water mark, "
				 "reapring aggressively.");
		}

		/* Total fds closed between all lanes and all current runs. */
		do {
			workpass = 0;
			for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
				/* The amount of work done on this lane on
				   this pass. */
				size_t workdone = 0;
				/* The entry being examined */
				cache_inode_lru_t *lru = NULL;
				/* Number of entries closed in this run. */
				size_t closed = 0;
				/* a cache_status */
				cache_inode_status_t cache_status =
				    CACHE_INODE_SUCCESS;
				/* a cache entry */
				cache_entry_t *entry;
				/* Current queue lane */
				struct lru_q_lane *qlane = &LRU[lane];
				q = &qlane->L1;
				/* entry refcnt */
				uint32_t refcnt;

				LogDebug(COMPONENT_CACHE_INODE_LRU,
					 "Reaping up to %d entries from lane "
					 "%zd",
					 lru_state.per_lane_work, lane);

				LogFullDebug(COMPONENT_CACHE_INODE_LRU,
					     "formeropen=%zd totalwork=%zd "
					     "workpass=%zd closed:%zd "
					     "totalclosed:%" PRIu64, formeropen,
					     totalwork, workpass, closed,
					     totalclosed);

				QLOCK(qlane);
				qlane->iter.active = true;	/* ACTIVE */
				/* While for_each_safe per se is NOT MT-safe,
				 * the iteration can be made so by the
				 * convention that any competing thread which
				 * would invalidate the iteration also adjusts
				 * glist and (in particular) glistn */
				glist_for_each_safe(qlane->iter.glist,
						    qlane->iter.glistn, &q->q) {
					struct lru_q *q;

					/* check per-lane work */
					if (workdone >= lru_state.per_lane_work)
						goto next_lane;

					lru =
					    glist_entry(qlane->iter.glist,
							cache_inode_lru_t, q);
					refcnt =
					    atomic_inc_int32_t(&lru->refcnt);

					/* get entry early */
					entry =
					    container_of(lru, cache_entry_t,
							 lru);

					/* check refcnt in range */
					if (unlikely(refcnt > 2)) {
						cache_inode_lru_unref(
							entry,
							LRU_UNREF_QLOCKED);
						workdone++; /* but count it */
						/* qlane LOCKED, lru refcnt is
						 * restored */
						continue;
					}

					/* Move entry to MRU of L2 */
					q = &qlane->L1;
					LRU_DQ_SAFE(lru, q);
					lru->qid = LRU_ENTRY_L2;
					q = &qlane->L2;
					glist_add(&q->q, &lru->q);
					++(q->size);

					/* Drop the lane lock while performing
					 * (slow) operations on entry */
					QUNLOCK(qlane);

					/* Acquire the content lock first; we
					 * may need to look at fds and close
					 * it. */
					PTHREAD_RWLOCK_wrlock(&entry->
							      content_lock);
					if (is_open(entry)) {
						cache_status =
						    cache_inode_close(
							    entry, CL_FLAGS);
						if (cache_status !=
						    CACHE_INODE_SUCCESS) {
							LogCrit(
						      COMPONENT_CACHE_INODE_LRU,
							"Error closing file in LRU thread.");
						} else {
							++totalclosed;
							++closed;
						}
					}
					PTHREAD_RWLOCK_unlock(&entry->
							      content_lock);

					QLOCK(qlane);	/* QLOCKED */
					cache_inode_lru_unref(
						entry,
						LRU_UNREF_QLOCKED);
					++workdone;
				} /* for_each_safe lru */

 next_lane:
				qlane->iter.active = false; /* !ACTIVE */
				QUNLOCK(qlane);
				LogDebug(COMPONENT_CACHE_INODE_LRU,
					 "Actually processed %zd entries on "
					 "lane %zd closing %zd descriptors",
					 workdone, lane, closed);
				workpass += workdone;
			}	/* foreach lane */
			totalwork += workpass;
		} while (extremis && (workpass >= lru_state.per_lane_work)
			 && (totalwork < lru_state.biggest_window));

		currentopen = atomic_fetch_size_t(&open_fd_count);
		if (extremis
		    && ((currentopen > formeropen)
			|| (formeropen - currentopen <
			    (((formeropen -
			       lru_state.fds_hiwat) *
			      cache_param.required_progress) /
			     100)))) {
			if (++lru_state.futility >
			    cache_param.futility_count) {
				LogCrit(COMPONENT_CACHE_INODE_LRU,
					"Futility count exceeded.  The LRU "
					"thread is unable to make progress in "
					"reclaiming FDs.  Disabling FD cache.");
				lru_state.caching_fds = false;
			}
		}
	}

	/* The following calculation will progressively garbage collect
	 * more frequently as these two factors increase:
	 * 1. current number of open file descriptors
	 * 2. rate at which file descriptors are being used.
	 *
	 * When there is little activity, this thread will sleep at the
	 * "LRU_Run_Interval" from the config.
	 *
	 * When there is a lot of activity, the thread will sleep for a
	 * much shorter time.
	 */
	lru_state.prev_fd_count = currentopen;
	lru_state.prev_time = time(NULL);

	fdnorm = (fdratepersec + fds_avg) / fds_avg;
	fddelta =
	    (currentopen >
	     lru_state.fds_lowat) ? (currentopen - lru_state.fds_lowat) : 0;
	fdmulti = (fddelta * 10) / fds_avg;
	fdmulti = fdmulti ? fdmulti : 1;
	fdwait_ratio =
	    lru_state.fds_hiwat / ((lru_state.fds_hiwat + fdmulti * fddelta) *
				   fdnorm);
	time_t new_thread_wait = threadwait * fdwait_ratio;
	if (new_thread_wait < cache_param.lru_run_interval / 10)
		new_thread_wait = cache_param.lru_run_interval / 10;

	fridgethr_setwait(ctx, new_thread_wait);

	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "After work, open_fd_count:%zd  count:%" PRIu64 " fdrate:%u "
		 "threadwait=%" PRIu64 "\n",
		 atomic_fetch_size_t(&open_fd_count),
		 lru_state.entries_used, fdratepersec, threadwait);
	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		     "currentopen=%zd futility=%d totalwork=%zd "
		     "biggest_window=%d extremis=%d lanes=%d " "fds_lowat=%d ",
		     currentopen, lru_state.futility, totalwork,
		     lru_state.biggest_window, extremis, LRU_N_Q_LANES,
		     lru_state.fds_lowat);
}

/* Public functions */

/**
 * Initialize subsystem
 */
int
cache_inode_lru_pkginit(void)
{
	/* Return code from system calls */
	int code = 0;
	/* Rlimit for open file descriptors */
	struct rlimit rlim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};
	struct fridgethr_params frp;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = cache_param.lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	atomic_store_size_t(&open_fd_count, 0);

	/* Set high and low watermark for cache entries.  This seems a
	   bit fishy, so come back and revisit this. */
	lru_state.entries_hiwat = cache_param.entries_hwmark;
	lru_state.entries_used = 0;

	/* Find out the system-imposed file descriptor limit */
	if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
		code = errno;
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"Call to getrlimit failed with error %d.  "
			"This should not happen.  Assigning default of %d.",
			code, FD_FALLBACK_LIMIT);
		lru_state.fds_system_imposed = FD_FALLBACK_LIMIT;
	} else {
		if (rlim.rlim_cur < rlim.rlim_max) {
			/* Save the old soft value so we can fall back to it
			   if setrlimit fails. */
			rlim_t old_soft = rlim.rlim_cur;
			LogInfo(COMPONENT_CACHE_INODE_LRU,
				"Attempting to increase soft limit from %"
				PRIu64 " " "to hard limit of %" PRIu64 "",
				(uint64_t) rlim.rlim_cur,
				(uint64_t) rlim.rlim_max);
			rlim.rlim_cur = rlim.rlim_max;
			if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
				code = errno;
				LogWarn(COMPONENT_CACHE_INODE_LRU,
					"Attempt to raise soft FD limit to "
					"hard FD limit "
					"failed with error %d.  Sticking to "
					"soft limit.",
					code);
				rlim.rlim_cur = old_soft;
			}
		}
		if (rlim.rlim_cur == RLIM_INFINITY) {
			FILE *nr_open;

			nr_open = fopen("/proc/sys/fs/nr_open", "r");
			if (nr_open == NULL) {
				code = errno;
				goto err_open;
			}
			code = fscanf(nr_open, "%" SCNu32 "\n",
				      &lru_state.fds_system_imposed);
			if (code != 1) {
				code = errno;
				LogMajor(COMPONENT_CACHE_INODE_LRU,
					 "The rlimit on open file descriptors "
					 "is infinite and the attempt to find "
					 "the system maximum failed with error "
					 "%d.  Assigning the default fallback "
					 "of %d which is almost certainly too "
					 "small.  If you are on a Linux system,"
					 " this should never happen.  If "
					 "you are running some other system, "
					 "please set an rlimit on file "
					 "descriptors (for example, "
					 "with ulimit) for this process and "
					 " consider editing " __FILE__
					 "to add support for finding "
					 "your system's maximum.", code,
					 FD_FALLBACK_LIMIT);
				lru_state.fds_system_imposed =
				    FD_FALLBACK_LIMIT;
			}
			fclose(nr_open);
err_open:
			;
		} else {
			lru_state.fds_system_imposed = rlim.rlim_cur;
		}
		LogInfo(COMPONENT_CACHE_INODE_LRU,
			"Setting the system-imposed limit on FDs to %d.",
			lru_state.fds_system_imposed);
	}

	lru_state.fds_hard_limit =
	    (cache_param.fd_limit_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.fds_hiwat =
	    (cache_param.fd_hwmark_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.fds_lowat =
	    (cache_param.fd_lwmark_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.futility = 0;

	lru_state.per_lane_work =
	    (cache_param.reaper_work / LRU_N_Q_LANES);
	lru_state.biggest_window =
	    (cache_param.biggest_window *
	     lru_state.fds_system_imposed) / 100;

	lru_state.prev_fd_count = 0;

	lru_state.caching_fds = cache_param.use_fd_cache;

	/* init queue complex */
	lru_init_queues();

	/* spawn LRU background thread */
	code = fridgethr_init(&lru_fridge, "LRU_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Unable to initialize LRU fridge, error code %d.",
			 code);
		return code;
	}

	code = fridgethr_submit(lru_fridge, lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Unable to start LRU thread, error code %d.", code);
		return code;
	}

	return 0;
}

/**
 * Shutdown subsystem
 *
 * @return 0 on success, POSIX errors on failure.
 */
int
cache_inode_lru_pkgshutdown(void)
{
	int rc = fridgethr_sync_command(lru_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(lru_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Failed shutting down LRU thread: %d", rc);
	}
	return rc;
}

static inline bool init_rw_locks(cache_entry_t *entry)
{
	int rc;
	bool attr_lock_init = false;
	bool content_lock_init = false;

	/* Initialize the entry locks */
	rc = pthread_rwlock_init(&entry->attr_lock, NULL);

	if (rc != 0)
		goto fail;

	attr_lock_init = true;

	rc = pthread_rwlock_init(&entry->content_lock, NULL);

	if (rc != 0)
		goto fail;

	content_lock_init = true;

	rc = pthread_rwlock_init(&entry->state_lock, NULL);

	if (rc == 0)
		return true;

fail:

	LogCrit(COMPONENT_CACHE_INODE,
		"pthread_rwlock_init returned %d (%s)",
		rc, strerror(rc));

	if (attr_lock_init)
		pthread_rwlock_destroy(&entry->attr_lock);

	if (content_lock_init)
		pthread_rwlock_destroy(&entry->content_lock);

	return false;
}

static cache_inode_status_t
alloc_cache_entry(cache_entry_t **entry)
{
	cache_inode_status_t status;
	cache_entry_t *nentry;

	nentry = pool_alloc(cache_inode_entry_pool, NULL);
	if (!nentry) {
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"can't allocate a new entry from cache pool");
		status = CACHE_INODE_MALLOC_ERROR;
		goto out;
	}

	/* Initialize the entry locks */
	if (!init_rw_locks(nentry)) {
		/* Recycle */
		status = CACHE_INODE_INIT_ENTRY_FAILED;
		pool_free(cache_inode_entry_pool, nentry);
		nentry = NULL;
		goto out;
	}

	status = CACHE_INODE_SUCCESS;
	atomic_inc_int64_t(&lru_state.entries_used);

 out:
	*entry = nentry;
	return status;
}

/**
 * @brief Re-use or allocate an entry
 *
 * This function repurposes a resident entry in the LRU system if the
 * system is above low-water mark, and allocates a new one otherwise.
 * On success, this function always returns an entry with two
 * references (one for the sentinel, one to allow the caller's use.)
 *
 * @param[out] entry Returned status
 *
 * @return CACHE_INODE_SUCCESS or error.
 */
cache_inode_status_t
cache_inode_lru_get(cache_entry_t **entry)
{
	cache_inode_lru_t *lru;
	cache_inode_status_t status = CACHE_INODE_SUCCESS;
	cache_entry_t *nentry = NULL;
	uint32_t lane;

	lru = lru_try_reap_entry();
	if (lru) {
		/* we uniquely hold entry */
		nentry = container_of(lru, cache_entry_t, lru);
		LogFullDebug(COMPONENT_CACHE_INODE_LRU,
			     "Recycling entry at %p.", nentry);
		cache_inode_lru_clean(nentry);
		if (!init_rw_locks(nentry)) {
			/* Recycle */
			status = CACHE_INODE_INIT_ENTRY_FAILED;
			pool_free(cache_inode_entry_pool, nentry);
			nentry = NULL;
			goto out;
		}
	} else {
		/* alloc entry */
		status = alloc_cache_entry(&nentry);
		if (!nentry)
			goto out;
	}

	/* Since the entry isn't in a queue, nobody can bump refcnt. */
	nentry->lru.refcnt = 2;
	nentry->lru.pin_refcnt = 0;
	nentry->lru.cf = 0;

	/* Enqueue. */
	lane = lru_lane_of_entry(nentry);
	lru_insert_entry(nentry, &LRU[lane].L1, lane, LRU_HEAD);

 out:
	*entry = nentry;
	return status;
}

/**
 * @brief Function to let the state layer pin an entry
 *
 * This function moves the given entry to the pinned queue partition
 * for its lane.  If the entry is already pinned, it is a no-op.
 *
 * @param[in] entry  The entry to be moved
 *
 * @retval CACHE_INODE_SUCCESS if the entry was moved.
 * @retval CACHE_INODE_DEAD_ENTRY if the entry is in the process of
 *                                disposal
 */
cache_inode_status_t
cache_inode_inc_pin_ref(cache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];

	/* Pin ref is infrequent, and never concurrent because SAL invariantly
	 * holds the state lock exclusive whenever it is called. */
	QLOCK(qlane);
	if (entry->lru.qid == LRU_ENTRY_CLEANUP) {
		QUNLOCK(qlane);
		return CACHE_INODE_DEAD_ENTRY;
	}

	/* Pin if not pinned already */
	cond_pin_entry(entry, LRU_FLAG_NONE /* future */);

	/* take pin ref count */
	entry->lru.pin_refcnt++;

	QUNLOCK(qlane);		/* !LOCKED (lane) */

	return CACHE_INODE_SUCCESS;
}

/**
 * @brief Function to let the state layer rlease a pin
 *
 * This function moves the given entry out of the pinned queue
 * partition for its lane.  If the entry is not pinned, it is a
 * no-op.
 *
 * @param[in] entry      The entry to be moved
 * @param[in] closefile  Indicates if file should be closed
 *
 */
void cache_inode_dec_pin_ref(cache_entry_t *entry, bool closefile)
{
	uint32_t lane = entry->lru.lane;
	cache_inode_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lane];

	/* Pin ref is infrequent, and never concurrent because SAL invariantly
	 * holds the state lock exclusive whenever it is called. */
	QLOCK(qlane);

	entry->lru.pin_refcnt--;
	if (unlikely(entry->lru.pin_refcnt == 0)) {

		/* entry could infrequently be on the cleanup queue */
		if (lru->qid == LRU_ENTRY_PINNED) {
			/* remove from pinned */
			struct lru_q *q = &qlane->pinned;
			/* XXX skip L1 iteration fixups */
			glist_del(&lru->q);
			--(q->size);
			/* add to MRU of L1 */
			lru->qid = LRU_ENTRY_L1;
			q = &qlane->L1;
			glist_add_tail(&q->q, &lru->q);
			++(q->size);
		}

		if (closefile == TRUE) {
			cache_inode_close(entry,
					  CACHE_INODE_FLAG_REALLYCLOSE |
					  CACHE_INODE_FLAG_NOT_PINNED);
		}
	}

	QUNLOCK(qlane);
}

/**
 * @brief Return true if a file is pinned.
 *
 * This function returns true if a file is pinned.
 *
 * @param[in] entry The file to be checked
 *
 * @return true if pinned, false otherwise.
 */
bool
cache_inode_is_pinned(cache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	int rc;

	QLOCK(qlane);
	rc = (entry->lru.pin_refcnt > 0);
	QUNLOCK(qlane);

	return rc;
}

/**
 * @brief Get a reference
 *
 * This function acquires a reference on the given cache entry.
 *
 * @param[in] entry  The entry on which to get a reference
 * @param[in] flags  One of LRU_REQ_INITIAL, LRU_REQ_SCAN, else LRU_FLAG_NONE
 *
 * A flags value of LRU_REQ_INITIAL or LRU_REQ_SCAN indicates an initial
 * reference.  A non-initial reference is an "extra" reference in some call
 * path, hence does not influence LRU, and is lockless.
 *
 * A flags value of LRU_REQ_INITIAL indicates an ordinary initial reference,
 * and strongly influences LRU.  LRU_REQ_SCAN indicates a scan reference
 * (currently, READDIR) and weakly influences LRU.  Ascan reference should not
 * be taken by call paths which may open a file descriptor.  In both cases, the
 * L1->L2 boundary is sticky (scan resistence).
 *
 * @retval CACHE_INODE_SUCCESS if the reference was acquired
 */
void
cache_inode_lru_ref(cache_entry_t *entry, uint32_t flags)
{
	atomic_inc_int32_t(&entry->lru.refcnt);

	/* adjust LRU on initial refs */
	if (flags & (LRU_REQ_INITIAL | LRU_REQ_SCAN)) {

		cache_inode_lru_t *lru = &entry->lru;
		struct lru_q_lane *qlane = &LRU[lru->lane];
		struct lru_q *q;

		/* do it less */
		if ((atomic_inc_int32_t(&entry->lru.cf) % 3) != 0)
			goto out;

		QLOCK(qlane);

		switch (lru->qid) {
		case LRU_ENTRY_L1:
			q = lru_queue_of(entry);
			if (flags & LRU_REQ_INITIAL) {
				/* advance entry to MRU (of L1) */
				LRU_DQ_SAFE(lru, q);
				glist_add_tail(&q->q, &lru->q);
				--(q->size);
			} else {
				/* do not advance entry in L1 on LRU_REQ_SCAN
				 * (scan resistence) */
			}
			break;
		case LRU_ENTRY_L2:
			q = lru_queue_of(entry);
			if (flags & LRU_REQ_INITIAL) {
				/* move entry to LRU of L1 */
				glist_del(&lru->q);	/* skip L1 fixups */
				--(q->size);
				lru->qid = LRU_ENTRY_L1;
				q = &qlane->L1;
				glist_add(&q->q, &lru->q);
				++(q->size);
			} else {
				/* advance entry to MRU of L2 */
				glist_del(&lru->q);	/* skip L1 fixups */
				glist_add_tail(&q->q, &lru->q);
			}
			break;
		default:
			/* do nothing */
			break;
		}		/* switch qid */
		QUNLOCK(qlane);
	}			/* initial ref */
 out:
	return;
}

/**
 * @brief Relinquish a reference
 *
 * This function relinquishes a reference on the given cache entry.
 * It follows the disposal/recycling lock discipline given at the
 * beginning of the file.
 *
 * The supplied entry is always either unlocked or destroyed by the
 * time this function returns.
 *
 * @param[in] entry  The entry on which to release a reference
 * @param[in] flags  Currently significant are and LRU_FLAG_LOCKED
 *                   (indicating that the caller holds the LRU mutex
 *                   lock for this entry.)
 */
void
cache_inode_lru_unref(cache_entry_t *entry, uint32_t flags)
{
	int32_t refcnt;
	enum lru_q_id qid;

	refcnt = atomic_dec_int32_t(&entry->lru.refcnt);

	if (unlikely(refcnt == 0)) {

		uint32_t lane = entry->lru.lane;
		struct lru_q_lane *qlane = &LRU[lane];
		bool qlocked = flags & LRU_UNREF_QLOCKED;
		struct lru_q *q;

		/* we MUST recheck that refcount is still 0 */
		if (!qlocked)
			QLOCK(qlane);
		refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);

		if (unlikely(refcnt > 0)) {
			if (!qlocked)
				QUNLOCK(qlane);
			goto out;
		}

		/* save qid */
		qid = entry->lru.qid;

		/* Really zero.  Remove entry and mark it as dead. */
		q = lru_queue_of(entry);
		if (q) {
			/* as of now, entries leaving the cleanup queue
			 * are LRU_ENTRY_NONE */
			LRU_DQ_SAFE(&entry->lru, q);
		}

		if (!qlocked)
			QUNLOCK(qlane);

		/* inline cleanup */
		if (qid == LRU_ENTRY_CLEANUP) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "LRU_ENTRY_CLEANUP of entry %p",
				 entry);
			state_wipe_file(entry);
			kill_export_root_entry(entry);
			kill_export_junction_entry(entry);
		}

		cache_inode_lru_clean(entry);
		pool_free(cache_inode_entry_pool, entry);

		atomic_dec_int64_t(&lru_state.entries_used);
	}			/* refcnt == 0 */
 out:
	return;
}

/**
 * @brief Put back a raced initial reference
 *
 * This function returns an entry previously returned from
 * cache_inode_lru_get, in the uncommon circumstance that it will not
 * be used.
 *
 * @param[in] entry  The entry on which to release a reference
 * @param[in] flags  Currently significant are and LRU_FLAG_LOCKED
 *                   (indicating that the caller holds the LRU mutex
 *                   lock for this entry.)
 */
void
cache_inode_lru_putback(cache_entry_t *entry, uint32_t flags)
{
	bool qlocked = flags & LRU_UNREF_QLOCKED;
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	struct lru_q *q;

	if (!qlocked)
		QLOCK(qlane);

	q = lru_queue_of(entry);
	if (q) {
		/* as of now, entries leaving the cleanup queue
		 * are LRU_ENTRY_NONE */
		LRU_DQ_SAFE(&entry->lru, q);
	}

	/* We do NOT call lru_clean_entry, since it was never initialized. */
	pool_free(cache_inode_entry_pool, entry);
	atomic_dec_int64_t(&lru_state.entries_used);

	if (!qlocked)
		QUNLOCK(qlane);
}

/**
 *
 * @brief Wake the LRU thread to free FDs.
 *
 * This function wakes the LRU reaper thread to free FDs and should be
 * called when we are over the high water mark.
 */

void
lru_wake_thread(void)
{
	fridgethr_wake(lru_fridge);
}

/** @} */
