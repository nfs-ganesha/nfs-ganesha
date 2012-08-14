/*
 * Vim:expandtab:shiftwidth=8:tabstop=8:
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
 * @defgroup Cache_inode Cache Inode
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
#include "nlm_list.h"
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

struct lru_q
{
     struct glist_head q; /* LRU is at HEAD, MRU at tail */
     enum lru_q_id id;
     uint64_t size;
};


/* Cache-line padding macro from MCAS */
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64 /* XXX arch-specific define */
#endif
#define CACHE_PAD(_n) char __pad ## _n [CACHE_LINE_SIZE]
#define ALIGNED_ALLOC(_s)					\
     ((void *)(((unsigned long)malloc((_s)+CACHE_LINE_SIZE*2) + \
		CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE-1)))

/**
 * A single queue lane, holding both movable and pinned entries.
 */

struct lru_q_lane
{
	struct lru_q L1;
	struct lru_q L2;
	struct lru_q pinned;  /* uncollectable, due to state */
	struct lru_q cleanup; /* deferred cleanup */
	pthread_mutex_t mtx;
	struct {
		char *func;
		uint32_t line;
	} locktrace;
     CACHE_PAD(0);
};

#define QLOCK(qlane) \
	do { \
	        pthread_mutex_lock(&(qlane)->mtx); \
		(qlane)->locktrace.func = (char*) __func__; \
		(qlane)->locktrace.line = __LINE__; \
	} while(0)

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

enum lru_edge
{
     LRU_HEAD, /* LRU */
     LRU_TAIL  /* MRU */
};

static const uint32_t FD_FALLBACK_LIMIT = 0x400;

/**
 * @brief Initialize a single base queue.
 *
 * This function initializes a single queue partition (L1, L1 pinned, L2,
 * etc)
 */
static inline void
lru_init_queue(struct lru_q *q, enum lru_q_id qid)
{
     init_glist(&q->q);
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
     } /* switch */

     return (q);
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
lru_insert_entry(cache_entry_t *entry, struct lru_q *q, uint32_t lane,
                 enum lru_edge edge)
{
     cache_inode_lru_t *lru = &entry->lru;
     struct lru_q_lane *qlane = &LRU[lane];

     lru->lane = lane; /* permanently fix lane */
     lru->qid = q->id; /* initial */

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

	if (! (lru->qid == LRU_ENTRY_PINNED)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		glist_del(&lru->q);
		--(q->size);

		/* in with the new */
		lru->qid = LRU_ENTRY_PINNED;
		q = &LRU[(lru->lane)].pinned;
		glist_add(&q->q, &lru->q);
		++(q->size);

	} /* ! PINNED or CLEANUP */
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
     fsal_status_t fsal_status = {0, 0};

     if (is_open(entry)) {
          cache_status = cache_inode_close(entry,
					   CACHE_INODE_FLAG_REALLYCLOSE |
					   CACHE_INODE_FLAG_NOT_PINNED);
          if (cache_status != CACHE_INODE_SUCCESS) {
               LogCrit(COMPONENT_CACHE_INODE_LRU,
                       "Error closing file in cleanup: %d.",
                       cache_status);
          }
     }

     if (entry->type == DIRECTORY) {
             cache_inode_release_dirents(entry, CACHE_INODE_AVL_BOTH);
     }

     /* Free FSAL resources */
     if (entry->obj_handle) {
	  /* release the handle object too */
	  fsal_status = entry->obj_handle->ops->release(entry->obj_handle);
	  if (FSAL_IS_ERROR(fsal_status)) {
	       LogCrit(COMPONENT_CACHE_INODE,
		       "Couldn't free FSAL ressources fsal_status.major=%u",
		       fsal_status.major);
	  }
	  entry->obj_handle = NULL;
     }

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
#define LRU_NEXT(n) \
	(atomic_inc_uint32_t(&(n)) % LRU_N_Q_LANES)

#define LRU_ENTRY_L1_OR_L2(e) \
    (((e)->lru.qid == LRU_ENTRY_L2) || \
     ((e)->lru.qid == LRU_ENTRY_L1))

#define LRU_ENTRY_REACHABLE_NOREFS(e, n) \
    (((n) == LRU_SENTINEL_REFCOUNT) && ((e)->fh_hk.inavl))

#define LRU_ENTRY_REMOVED(e, n) \
    (((n) == 0) && (! (e)->fh_hk.inavl))

#define LANE_NTRIES 3

static uint32_t reap_lane = 0; /* by definition */

static inline cache_inode_lru_t *
lru_reap_impl(uint32_t flags)
{
     uint32_t lane;
     struct lru_q_lane *qlane;
     struct lru_q *lq;
     struct glist_head *glist;
     struct glist_head *glistn;
     cache_inode_lru_t *lru = NULL;
     cache_entry_t *entry;
     uint32_t refcnt;
     int ix, cnt;

     lane = LRU_NEXT(reap_lane);
     for (ix = 0; ix < LRU_N_Q_LANES; ++ix, lane = LRU_NEXT(reap_lane)) {
          qlane = &LRU[lane];
          lq = (flags & LRU_ENTRY_L1) ? &qlane->L1 : &qlane->L2;
          cnt = 0;
          QLOCK(qlane);
          glist_for_each_safe(glist, glistn, &lq->q) {
               lru = glist_entry(glist, cache_inode_lru_t, q);
               if (lru) {
		    cih_latch_t latch;
		    uint32_t nrefcnt;
                    refcnt = atomic_inc_int32_t(&lru->refcnt);
                    if (unlikely(refcnt != (LRU_SENTINEL_REFCOUNT + 1))) {
                         /* cant use it. */
                         atomic_dec_int32_t(&lru->refcnt);
                         goto next_entry;
                    }
                    /* potentially reclaimable */
                    QUNLOCK(qlane);
		    entry = container_of(lru, cache_entry_t, lru);
		    /* entry must be unreachable from CIH when recycled */
		    if (cih_latch_entry(entry, &latch, CIH_GET_WLOCK,
                                        __func__, __LINE__)) {
			    QLOCK(qlane);
			    nrefcnt =
				    atomic_dec_int32_t(&entry->lru.refcnt);
                            /* there are two cases which permit reclaim,
                             * entry is:
                             * 1. reachable but unref'd (nrefcnt==1)
                             * 2. unreachable, being removed (plus nrefcnt==0)
                             * in both cases, only if the entry is on L1 or L2
                             * as expected.  for safety, take only the former
                             */
                            if (LRU_ENTRY_L1_OR_L2(entry) &&
                                LRU_ENTRY_REACHABLE_NOREFS(entry, nrefcnt)) {
				    /* it worked */
				    struct lru_q *q = lru_queue_of(entry);
                                    cih_remove_latched(entry, &latch,
                                                       CIH_REMOVE_QLOCKED);
				    glist_del(&lru->q);
				    --(q->size);
				    entry->lru.qid = LRU_ENTRY_NONE;
				    QUNLOCK(qlane);
				    cih_latch_rele(&latch);
				    goto out;
			      }
			      cih_latch_rele(&latch);
			 }
               } /* lru */
          next_entry:
               if (++cnt > LANE_NTRIES)
                    break;
          } /* foreach (initial) entry */
          QUNLOCK(qlane);
     } /* foreach lane */

out:
     return (lru);
}

static inline cache_inode_lru_t *
lru_try_reap_entry(void)
{
     uint32_t tflags;
     cache_inode_lru_t *lru;

     pthread_mutex_lock(&lru_mtx);
     tflags = lru_state.flags;
     pthread_mutex_unlock(&lru_mtx);

     if (! (tflags & LRU_STATE_RECLAIMING))
	     return (NULL);

     lru = lru_reap_impl(LRU_ENTRY_L2);
     if (! lru)
          lru = lru_reap_impl(LRU_ENTRY_L1);

     return (lru);
}

static const uint32_t S_NSECS = 1000000000UL; /* nsecs in 1s */
static const uint32_t MS_NSECS = 1000000UL; /* nsecs in 1ms */

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

	/* if this happened, it would indicate misuse or damage */
	assert(lru->qid != LRU_ENTRY_PINNED);

	if (! (lru->qid == LRU_ENTRY_CLEANUP)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		glist_del(&lru->q);
		--(q->size);

		/* in with the new */
		lru->qid = LRU_ENTRY_CLEANUP;
		q = &qlane->cleanup;
		glist_add(&q->q, &lru->q);
		++(q->size);
	}

	QUNLOCK(qlane);
}

/**
 * @brief Cache entry deferred cleanup helper routine.
 *
 * This function consumes the LRU_CQ queue, disposing state and
 * returning sentinel refs.  Final destruction of the entries
 * of course happens when their refcounts reach 0.
 *
 * @param[in] ms The time to sleep, in milliseconds.
 *
 * @retval false if the thread wakes by timeout.
 * @retval true if the thread wakes by signal.
 */
static inline uint32_t
cache_inode_lru_cleanup(void)
{
    uint32_t n_finalized = 0;
    uint32_t lane = 0;

    struct lru_q_lane *qlane;
    struct lru_q *cq;

    cache_inode_lru_t *lru;
    cache_entry_t *entry;

    for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
	    qlane = &LRU[lane];
	    cq = &qlane->cleanup;

        do {
		QLOCK(qlane);
		lru = glist_first_entry(&cq->q, cache_inode_lru_t, q);
		if (! lru) {
			QUNLOCK(qlane);
			break;
		}
		glist_del(&lru->q);
		--(cq->size);
		lru->qid = LRU_ENTRY_NONE;
		QUNLOCK(qlane);

		/* finalize */
		entry = container_of(lru, cache_entry_t, lru);
		state_wipe_file(entry);
		/* return (transferred) call path ref */
		cache_inode_lru_unref(entry, LRU_UNREF_CLEANUP);
		n_finalized++;
        } while (lru);
    }

    return (n_finalized);
}

/**
 * @param Sum the per-lane counts of pinned and un-pinned items.
 *
 * An approximate count is ok.
 *
 * @param[out] t_count  Appx. count of entries in L1+L2
 * @param[out] pinned_t_count Appx. count of entries in pinned
 */
static void
lru_counts(uint64_t *t_count,
	   uint64_t *pinned_t_count)
{
	size_t lane;
	struct lru_q_lane *qlane;
	uint64_t aging = 0;
	uint64_t pinned = 0;

	for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
		qlane =  &LRU[lane];
		/* we're relying on atomic fetch merely for a stable
		 * value (just in case we're not guaranteed one) */
		aging += atomic_fetch_uint64_t(&qlane->L1.size);
		aging += atomic_fetch_uint64_t(&qlane->L2.size);
	        pinned += atomic_fetch_uint64_t(&qlane->pinned.size);
	}

        *t_count = aging;
	*pinned_t_count = pinned;
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
static void
lru_run(struct fridgethr_context *ctx)
{
     /* Index */
     size_t lane = 0;
     /* True if we were explicitly awakened. */
     bool woke = ctx->woke;
     /* Finalized */
     uint32_t n_finalized;
     uint32_t fdratepersec=1, fds_avg, fddelta;
     float fdnorm, fdwait_ratio, fdmulti;
     uint64_t count, pinned_count;
     time_t threadwait = threadwait = fridgethr_getwait(ctx);
     /* True if we are taking extreme measures to reclaim FDs */
     bool extremis = false;
     /* Total work done in all passes so far.  If this exceeds the
      * window, stop.
      */
     size_t totalwork = 0;
     uint64_t totalclosed = 0;
     /* The current count (after reaping) of open FDs */
     size_t currentopen = 0;

     fds_avg = (lru_state.fds_hiwat - lru_state.fds_lowat) / 2;

     if (nfs_param.cache_param.use_fd_cache) {
	  extremis = (open_fd_count > lru_state.fds_hiwat);
     }

     LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		  "LRU awakes.");

     if (! woke) {
	     /* If we make it all the way through a timed sleep
		without being woken, we assume we aren't racing
		against the impossible. */
	     lru_state.futility = 0;
     }


     /* First, sum the queue counts.  This lets us know where we are
	relative to our watermarks. */
     lru_counts(&count, &pinned_count);

     LogDebug(COMPONENT_CACHE_INODE_LRU,
	      "%zu non-pinned entries. %zu pinned entries. %zu open fds.",
	      count, pinned_count, open_fd_count);

     count += pinned_count;

     LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		  "lru entries: %zu",
		  count);

     pthread_mutex_lock(&lru_mtx);
     if (count >= lru_state.entries_hiwat) {
	     lru_state.flags |= LRU_STATE_RECLAIMING;
     }
     if (count <= lru_state.entries_lowat) {
	     lru_state.flags &= ~LRU_STATE_RECLAIMING;
     }
     pthread_mutex_unlock(&lru_mtx);

     /* Reap file descriptors.  This is a preliminary example of the
	L2 functionality rather than something we expect to be
	permanent.  (It will have to adapt heavily to the new FSAL
	API, for example.) */

     if ((atomic_fetch_size_t(&open_fd_count) < lru_state.fds_lowat) &&
	 nfs_param.cache_param.use_fd_cache) {
	  LogDebug(COMPONENT_CACHE_INODE_LRU,
		   "FD count is %zd and low water mark is "
		   "%d: not reaping.",
		   open_fd_count,
		   lru_state.fds_lowat);
	  if (nfs_param.cache_param.use_fd_cache &&
	      !lru_state.caching_fds) {
	       lru_state.caching_fds = true;
	       LogEvent(COMPONENT_CACHE_INODE_LRU,
		       "Re-enabling FD cache.");
	  }
     } else {
	  /* The count of open file descriptors before this run
	     of the reaper. */
	  size_t formeropen = open_fd_count;
	  /* Work done in the most recent pass of all queues.  if
	     value is less than the work to do in a single queue,
	     don't spin through more passes. */
	  size_t workpass = 0;

	  time_t curr_time = time(NULL);
	  fdratepersec = (curr_time <= lru_state.prev_time) ?
	       1 : (open_fd_count - lru_state.prev_fd_count)/
	       (curr_time - lru_state.prev_time);

	  LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		       "fdrate:%u fdcount:%zd slept for %"PRIu64" sec",
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
		    /* The current entry being examined. */
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
		    /* safe lane traversal */
		    struct glist_head  *glist;
		    struct glist_head  *glistn;

		    LogDebug(COMPONENT_CACHE_INODE_LRU,
			     "Reaping up to %d entries from lane %zd",
			     lru_state.per_lane_work, lane);

		    LogFullDebug(COMPONENT_CACHE_INODE_LRU,
				 "formeropen=%zd totalwork=%zd "
				 "workpass=%zd closed:%zd "
				 "totalclosed:%"PRIu64,
				 formeropen, totalwork,
				 workpass, closed, totalclosed);

		    QLOCK(qlane);
		    while ((workdone < lru_state.per_lane_work) &&
			   (!glist_empty(&LRU[lane].L1.q))) {
			 /* In hindsight, it's really important to avoid
			  * restarts. */
			    glist_for_each_safe(glist, glistn,
						&LRU[lane].L1.q) {
				    uint32_t refcnt;
				    struct lru_q *q;

				    /* recheck per-lane work */
				    if (workdone >= lru_state.per_lane_work) {
					    break;
				    }

				    lru = glist_entry(glist,
						      cache_inode_lru_t, q);

				    /* Drop the lane lock while performing
				     * (slow) operations on entry */
				    atomic_inc_int32_t(&lru->refcnt);
				    QUNLOCK(qlane);

				    /* Need the entry */
				    entry = container_of(lru, cache_entry_t,
							 lru);

				    /* Acquire the content lock first; we may
				     * need to look at fds and close it. */
				    pthread_rwlock_wrlock(&entry->content_lock);
				    if (is_open(entry)) {
					    cache_status =
						    cache_inode_close(
							    entry,
							    CACHE_INODE_FLAG_REALLYCLOSE |
							    CACHE_INODE_FLAG_NOT_PINNED |
							    CACHE_INODE_FLAG_CONTENT_HAVE |
							    CACHE_INODE_FLAG_CONTENT_HOLD);
					    if (cache_status != CACHE_INODE_SUCCESS) {
						    LogCrit(COMPONENT_CACHE_INODE_LRU,
							    "Error closing file in "
							    "LRU thread.");
					    } else {
						    ++totalclosed;
						    ++closed;
					    }
				    }
				    pthread_rwlock_unlock(&entry->content_lock);

				    /* We did the (slow) cache entry ops
				     * unlocked, recheck lru before moving it
				     * to L2. */
				    QLOCK(qlane);

				    /* This can be in any order wrt the lane
				     * mutex, but this order seems most sane. */
				    refcnt = atomic_dec_int32_t(&lru->refcnt);

				    /* Since we dropped the lane mutex, recheck
				     * that the entry hasn't moved.  The two
				     * checks below should be essentially
				     * equivalent. */
				    if (unlikely((lru->qid != LRU_ENTRY_L1) ||
						 (! refcnt))) {
					    workdone++; /* but count it */
					    /* qlane LOCKED */
					    continue;
				    }

				    /* Move entry to MRU of L2 */
				    q = &qlane->L1;
				    glist_del(&lru->q);
				    --(q->size);
				    lru->qid = LRU_ENTRY_L2;
				    q = &qlane->L2;
				    glist_add(&q->q, &lru->q);
				    ++(q->size);

				    ++workdone;
			    } /* for_each_safe lru */
		    } /* while (workdone < per-lane work) */

		    QUNLOCK(qlane);
		    LogDebug(COMPONENT_CACHE_INODE_LRU,
			     "Actually processed %zd entries on lane %zd "
			     "closing %zd descriptors",
			     workdone,
			     lane,
			     closed);
		    workpass += workdone;
	       } /* foreach lane */
	       totalwork += workpass;
	  } while (extremis &&
		   (workpass >= lru_state.per_lane_work) &&
		   (totalwork < lru_state.biggest_window));

	  currentopen = open_fd_count;
	  if (extremis &&
	      ((currentopen > formeropen) ||
	       (formeropen - currentopen <
		(((formeropen - lru_state.fds_hiwat) *
		  nfs_param.cache_param.required_progress) /
		 100)))) {
	       if (++lru_state.futility >
		   nfs_param.cache_param.futility_count) {
		    LogCrit(COMPONENT_CACHE_INODE_LRU,
			    "Futility count exceeded.  The LRU thread is "
			    "unable to make progress in reclaiming FDs."
			    "Disabling FD cache.");
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

     fdnorm = (fdratepersec + fds_avg)/fds_avg;
     fddelta = (currentopen > lru_state.fds_lowat) ?
	  (currentopen - lru_state.fds_lowat) : 0;
     fdmulti = (fddelta*10)/fds_avg;
     fdmulti = fdmulti ? fdmulti : 1;
     fdwait_ratio = lru_state.fds_hiwat /
	  ((lru_state.fds_hiwat + fdmulti * fddelta) * fdnorm);
     fridgethr_setwait(ctx, threadwait * fdwait_ratio);

     LogDebug(COMPONENT_CACHE_INODE_LRU,
	      "After work, open_fd_count:%zd  count:%"PRIu64" fdrate:%u "
	      "threadwait=%"PRIu64"\n",
	      open_fd_count, count - totalwork, fdratepersec,
	      threadwait);
     LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		  "currentopen=%zd futility=%d totalwork=%zd "
		  "biggest_window=%d extremis=%d lanes=%d "
		  "fds_lowat=%d ",
		  currentopen, lru_state.futility,
		  totalwork, lru_state.biggest_window, extremis,
		  LRU_N_Q_LANES, lru_state.fds_lowat);

     /* Process LRU cleanup queue */
     n_finalized = cache_inode_lru_cleanup();

     LogDebug(COMPONENT_CACHE_INODE_LRU,
	      "LRU cleanup, reclaimed %d entries",
	      n_finalized);
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
     frp.thread_delay = nfs_param.cache_param.lru_run_interval;
     frp.flavor = fridgethr_flavor_looper;

     open_fd_count = 0;

     /* Repurpose some GC policy */
     lru_state.flags = LRU_STATE_NONE;

     /* Set high and low watermark for cache entries.  This seems a
        bit fishy, so come back and revisit this. */
     lru_state.entries_hiwat
          = nfs_param.cache_param.entries_hwmark;
     lru_state.entries_lowat
          = nfs_param.cache_param.entries_lwmark;

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
                       "Attempting to increase soft limit from %"PRIu64" "
                       "to hard limit of %"PRIu64"",
                       (uint64_t) rlim.rlim_cur, (uint64_t) rlim.rlim_max);
               rlim.rlim_cur = rlim.rlim_max;
               if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
                    code = errno;
                    LogWarn(COMPONENT_CACHE_INODE_LRU,
                            "Attempt to raise soft FD limit to hard FD limit "
                            "failed with error %d.  Sticking to soft limit.",
                            code);
                    rlim.rlim_cur = old_soft;
               }
          }
          if (rlim.rlim_cur == RLIM_INFINITY) {
               FILE *const nr_open = fopen("/proc/sys/fs/nr_open",
                                           "r");
               if (!(nr_open &&
                     (fscanf(nr_open,
                             "%"SCNu32"\n",
                             &lru_state.fds_system_imposed) == 1) &&
                     (fclose(nr_open) == 0))) {
                    code = errno;
                    LogMajor(COMPONENT_CACHE_INODE_LRU,
                             "The rlimit on open file descriptors is infinite "
                             "and the attempt to find the system maximum "
                             "failed with error %d.  "
                             "Assigning the default fallback of %d which is "
                             "almost certainly too small.  If you are on a "
                             "Linux system, this should never happen.  If "
                             "you are running some other system, please set "
                             "an rlimit on file descriptors (for example, "
                             "with ulimit) for this process and consider "
                             "editing " __FILE__ "to add support for finding "
                             "your system's maximum.", code,
                             FD_FALLBACK_LIMIT);
                    lru_state.fds_system_imposed = FD_FALLBACK_LIMIT;
               }
          } else {
               lru_state.fds_system_imposed = rlim.rlim_cur;
          }
          LogInfo(COMPONENT_CACHE_INODE_LRU,
                  "Setting the system-imposed limit on FDs to %d.",
                  lru_state.fds_system_imposed);
     }


     lru_state.fds_hard_limit = (nfs_param.cache_param.fd_limit_percent *
                                 lru_state.fds_system_imposed) / 100;
     lru_state.fds_hiwat = (nfs_param.cache_param.fd_hwmark_percent *
                            lru_state.fds_system_imposed) / 100;
     lru_state.fds_lowat = (nfs_param.cache_param.fd_lwmark_percent *
                            lru_state.fds_system_imposed) / 100;
     lru_state.futility = 0;

     lru_state.per_lane_work
          = (nfs_param.cache_param.reaper_work / LRU_N_Q_LANES);
     lru_state.biggest_window = (nfs_param.cache_param.biggest_window *
                                 lru_state.fds_system_imposed) / 100;

     lru_state.prev_fd_count = 0;

     lru_state.caching_fds = nfs_param.cache_param.use_fd_cache;

     /* init queue complex */
     lru_init_queues();

     /* spawn LRU background thread */
     code = fridgethr_init(&lru_fridge,
			   "LRU Thread",
			   &frp);
     if (code != 0) {
          LogMajor(COMPONENT_CACHE_INODE_LRU,
                   "Unable to initialize LRU fridge, error code %d.",
                   code);
	  return code;
     }

     code = fridgethr_submit(lru_fridge,
			     lru_run,
			     NULL);
     if (code != 0) {
	 LogMajor(COMPONENT_CACHE_INODE_LRU,
		  "Unable to start LRU thread, error code %d.",
		  code);
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
		   "Failed shutting down LRU thread: %d",
		   rc);
     }
     return rc;
}

cache_inode_status_t
alloc_cache_entry(cache_entry_t **entry)
{
	cache_inode_status_t status;
	cache_entry_t *nentry;
	int rc;

	nentry = pool_alloc(cache_inode_entry_pool, NULL);
	if(! nentry) {
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"can't allocate a new entry from cache pool");
		status = CACHE_INODE_MALLOC_ERROR;
		goto out;
	}
	  
	/* Initialize the entry locks */
	if (((rc = pthread_rwlock_init(&nentry->attr_lock, NULL)) != 0) ||
	    ((rc = pthread_rwlock_init(&nentry->content_lock, NULL)) != 0) ||
	    ((rc = pthread_rwlock_init(&nentry->state_lock, NULL)) != 0)) {
		/* Recycle */
		LogCrit(COMPONENT_CACHE_INODE,
			"pthread_rwlock_init returned %d (%s)",
			rc, strerror(rc));
		status = CACHE_INODE_INIT_ENTRY_FAILED;
		pool_free(cache_inode_entry_pool, nentry);
		nentry = NULL;
		goto out;
	}

	status = CACHE_INODE_SUCCESS;

out:
	*entry = nentry;
	return (status);
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
 * @param[in]  flags Flags governing call
 *
 * @return CACHE_INODE_SUCCESS or error.
 */
cache_inode_status_t
cache_inode_lru_get(cache_entry_t **entry,
                    uint32_t flags)
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
			  "Recycling entry at %p.",
			  nentry);
          cache_inode_lru_clean(nentry);
     } else {
	     /* alloc entry */
	     status = alloc_cache_entry(&nentry);
	     if(! nentry)
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
     return (status);
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
		return (CACHE_INODE_DEAD_ENTRY);
	}

	/* Pin if not pinned already */
	cond_pin_entry(entry, LRU_FLAG_NONE /* future */);

	/* take pin and ref counts */
	atomic_inc_int32_t(&entry->lru.refcnt);
	entry->lru.pin_refcnt++;

	QUNLOCK(qlane); /* !LOCKED (lane) */

	return (CACHE_INODE_SUCCESS);
}

/**
 * @brief Function to let the state layer rlease a pin
 *
 * This function moves the given entry out of the pinned queue
 * partition for its lane.  If the entry is not pinned, it is a
 * no-op.
 *
 * @param[in] entry  The entry to be moved
 *
 * @retval CACHE_INODE_SUCCESS if the entry was moved.
 */
cache_inode_status_t
cache_inode_dec_pin_ref(cache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
        cache_inode_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lane];

	/* Pin ref is infrequent, and never concurrent because SAL invariantly
	 * holds the state lock exclusive whenever it is called. */
	QLOCK(qlane);

	entry->lru.pin_refcnt--;
	if (unlikely(entry->lru.pin_refcnt == 0)) {
		/* remove from pinned */
		struct lru_q *q =  &qlane->pinned;
		glist_del(&lru->q);
		--(q->size);
		/* add to MRU of L1 */
		lru->qid = LRU_ENTRY_L1;
		q = &qlane->L1;
		glist_add_tail(&q->q, &lru->q);
		++(q->size);
	}

	QUNLOCK(qlane);

	/* Also release an LRU reference */
	atomic_dec_int32_t(&entry->lru.refcnt);

	return (CACHE_INODE_SUCCESS);
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
bool cache_inode_is_pinned(cache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
        int rc;

	QLOCK(qlane);
	rc = (entry->lru.pin_refcnt > 0);
	QUNLOCK(qlane);

	return (rc);
}

/**
 * @brief Get a reference
 *
 * This function acquires a reference on the given cache entry.
 *
 * @param[in] entry  The entry on which to get a reference
 * @param[in] flags  One of LRU_REQ_INITIAL, LRU_REQ_SCAN, else LRU_FLAG_NONE (0)
 *
 * A flags value of LRU_REQ_INITIAL or LRU_REQ_SCAN indicates an initial reference.
 * A non-initial reference is an "extra" reference in some call path, hence does
 * not influence LRU, and is lockless.
 *
 * A flags value of LRU_REQ_INITIAL indicates an ordinary initial reference, and
 * strongly influences LRU.  LRU_REQ_SCAN indicates a scan reference (currently,
 * READDIR) and weakly influences LRU.  Ascan reference should not be taken by call
 * paths which may open a file descriptor.  In both cases, the L1->L2 boundary is
 * sticky (scan resistence).
 *
 * @retval CACHE_INODE_SUCCESS if the reference was acquired
 */
void
cache_inode_lru_ref(cache_entry_t *entry, uint32_t flags)
{
	atomic_inc_int32_t(&entry->lru.refcnt);

	/* adjust LRU on initial refs */
	if (flags & (LRU_REQ_INITIAL|LRU_REQ_SCAN)) {

		cache_inode_lru_t *lru = &entry->lru;
		struct lru_q_lane *qlane = &LRU[lru->lane];
		struct lru_q *q;

		/* do it less */
                if ((atomic_inc_int32_t(&entry->lru.cf) % 3) != 0)
			goto out;

		QLOCK(qlane);

		switch (lru->qid) {
		case LRU_ENTRY_PINNED:
			/* do nothing */
			break;
		case LRU_ENTRY_L1:
			q = lru_queue_of(entry);
			if (flags & LRU_REQ_INITIAL) {
				/* advance entry to MRU (of L1) */
				glist_del(&lru->q);
				glist_add_tail(&q->q, &lru->q);
			} else {
				/* do not advance entry in L1 on LRU_REQ_SCAN
				 * (scan resistence) */                    
			}
			break;
		case LRU_ENTRY_L2:
			q = lru_queue_of(entry);
			if (flags & LRU_REQ_INITIAL) {
				/* move entry to LRU of L1 */
				glist_del(&lru->q);
				--(q->size);
				lru->qid = LRU_ENTRY_L1;
				q = &qlane->L1;
				glist_add(&q->q, &lru->q);
				++(q->size);
			} else {
				/* advance entry to MRU of L2 */
				glist_del(&lru->q);
				glist_add_tail(&q->q, &lru->q);
			}
			break;
		default:
			/* can't happen */
			abort();
			break;               
		} /* switch qid */
		QUNLOCK(qlane);
	} /* initial ref */
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
cache_inode_lru_unref(cache_entry_t *entry,
                      uint32_t flags)
{
	uint64_t refcnt;

	refcnt = atomic_dec_int32_t(&entry->lru.refcnt);
	if (unlikely(refcnt == 0)) {

		uint32_t lane = entry->lru.lane;
		struct lru_q_lane *qlane = &LRU[lane];
                bool qlocked = flags & LRU_UNREF_QLOCKED;
		struct lru_q *q;

		/* we MUST recheck that refcount is still 0 */
		if (! qlocked)
                    QLOCK(qlane);
		refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);

		if (unlikely(refcnt > 0)) {
			if(! qlocked)
				QUNLOCK(qlane);
			goto out;
		}

		/* Really zero.  Remove entry and mark it as dead. */
		q = lru_queue_of(entry);
		if (q) {
			/* as of now, entries leaving the cleanup queue
			 * are LRU_ENTRY_NONE */
			glist_del(&entry->lru.q);
			--(q->size);
		}

		/* XXX now just cleans (ahem) */
		cache_inode_lru_clean(entry);
		pool_free(cache_inode_entry_pool, entry);
		if (! qlocked)
			QUNLOCK(qlane);
	} /* refcnt == 0 */
out:
        return;
}

/**
 *
 * @brief Wake the LRU thread to free FDs.
 *
 * This function wakes the LRU reaper thread to free FDs and should be
 * called when we are over the high water mark.
 */

void lru_wake_thread(void)
{
	fridgethr_wake(lru_fridge);
}

/** @} */
