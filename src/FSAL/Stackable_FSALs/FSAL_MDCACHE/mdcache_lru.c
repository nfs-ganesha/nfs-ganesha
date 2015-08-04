/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @addtogroup FSAL_MDCACHE
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
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "nfs_core.h"
#include "log.h"
#include "mdcache_lru.h"
#include "mdcache_hash.h"
#include "abstract_atomic.h"
#include "gsh_intrinsic.h"
#include "sal_functions.h"
#include "nfs_exports.h"

/**
 *
 * @file mdcache_lru.c
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
 * under ordinary circumstances, so are kept on a separate lru_noscan
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
 * A single queue lane, holding both movable and noscan entries.
 */

struct lru_q_lane {
	struct lru_q L1;
	struct lru_q L2;
	struct lru_q noscan;	/* uncollectable, due to state */
	struct lru_q cleanup;	/* deferred cleanup */
	pthread_mutex_t mtx;
	/* LRU thread scan position */
	struct {
		bool active;
		struct glist_head *glist;
		struct glist_head *glistn;
	} iter;
#ifdef ENABLE_LOCKTRACE
	struct {
		char *func;
		uint32_t line;
	} locktrace;
#endif
	 CACHE_PAD(0);
};

#ifdef ENABLE_LOCKTRACE
#define QLOCK(qlane) \
	do { \
		PTHREAD_MUTEX_lock(&(qlane)->mtx); \
		(qlane)->locktrace.func = (char *) __func__; \
		(qlane)->locktrace.line = __LINE__; \
	} while (0)
#else
#define QLOCK(qlane) \
	PTHREAD_MUTEX_lock(&(qlane)->mtx)
#endif

#define QUNLOCK(qlane) \
	PTHREAD_MUTEX_unlock(&(qlane)->mtx)

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

static struct fridgethr *lru_fridge;

enum lru_edge {
	LRU_LRU,	/* LRU */
	LRU_MRU		/* MRU */
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
 * This function initializes a single queue partition (L1, L1 noscan, L2,
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

	for (ix = 0; ix < LRU_N_Q_LANES; ++ix) {
		struct lru_q_lane *qlane = &LRU[ix];

		/* one mutex per lane */
		PTHREAD_MUTEX_init(&qlane->mtx, NULL);

		/* init iterator */
		qlane->iter.active = false;

		/* init lane queues */
		lru_init_queue(&LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&LRU[ix].noscan, LRU_ENTRY_NOSCAN);
		lru_init_queue(&LRU[ix].cleanup, LRU_ENTRY_CLEANUP);
	}
}

/**
 * @brief Return a pointer to the current queue of entry
 *
 * This function returns a pointer to the queue on which entry is linked,
 * or NULL if entry is not on any queue.
 *
 * @note The caller @a MUST hold the lane lock
 *
 * @param[in] entry  The entry.
 *
 * @return A pointer to entry's current queue, NULL if none.
 */
static inline struct lru_q *
lru_queue_of(mdcache_entry_t *entry)
{
	struct lru_q *q;

	switch (entry->lru.qid) {
	case LRU_ENTRY_NOSCAN:
		q = &LRU[(entry->lru.lane)].noscan;
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
 * @brief Get the appropriate lane for a cache entry
 *
 * This function gets the LRU lane by taking the modulus of the
 * supplied pointer.
 *
 * @param[in] entry  A pointer to a cache entry
 *
 * @return The LRU lane in which that entry should be stored.
 */
static inline uint32_t
lru_lane_of_entry(mdcache_entry_t *entry)
{
	return (uint32_t) ((((uintptr_t) entry) / 2*sizeof(uintptr_t))
				% LRU_N_Q_LANES);
}

/**
 * @brief Insert an entry into the specified queue and lane
 *
 * This function determines the queue corresponding to the supplied
 * lane and flags, inserts the entry into that queue, and updates the
 * entry to hold the flags and lane.
 *
 * @note The caller MUST hold a lock on the queue lane.
 *
 * @param[in] lru    The LRU entry to insert
 * @param[in] q      The queue to insert on
 * @param[in] edge   One of LRU_LRU or LRU_MRU
 */
static inline void
lru_insert(mdcache_lru_t *lru, struct lru_q *q, enum lru_edge edge)
{
	lru->qid = q->id;	/* initial */
	if (lru->qid == LRU_ENTRY_CLEANUP)
		atomic_set_uint32_t_bits(&lru->flags, LRU_CLEANUP);

	switch (edge) {
	case LRU_LRU:
		glist_add(&q->q, &lru->q);
		break;
	case LRU_MRU:
	default:
		glist_add_tail(&q->q, &lru->q);
		break;
	}
	++(q->size);
}

/**
 * @brief Insert an entry into the specified queue and lane with locking
 *
 * This function determines the queue corresponding to the supplied
 * lane and flags, inserts the entry into that queue, and updates the
 * entry to hold the flags and lane.
 *
 * @note The caller MUST NOT hold a lock on the queue lane.
 *
 * @param[in] lru    The LRU entry to insert
 * @param[in] q      The queue to insert on
 * @param[in] edge   One of LRU_LRU or LRU_MRU
 */
static inline void
lru_insert_entry(mdcache_entry_t *entry, struct lru_q *q, enum lru_edge edge)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];

	QLOCK(qlane);

	lru_insert(lru, q, edge);

	QUNLOCK(qlane);
}

/**
 * @brief Mark an entry noscan
 *
 * @note The caller @a MUST hold the lane lock
 * @note The entry @a MUST @a NOT be on the CLEANUP queue
 *
 * @param[in] entry  The entry to mark noscan
 * @param[in] flags  (TBD)
 */
static inline void
cond_noscan_entry(mdcache_entry_t *entry, uint32_t flags)
{
	mdcache_lru_t *lru = &entry->lru;

	if (lru->flags & LRU_CLEANUP)
		return;

	if (!(lru->qid == LRU_ENTRY_NOSCAN)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ_SAFE(lru, q);

		/* in with the new */
		q = &LRU[(lru->lane)].noscan;
		lru_insert(lru, q, LRU_LRU);
		++(q->size);

	} /* ! NOSCAN */
}

/**
 * @brief Clean an entry for recycling.
 *
 * This function cleans an entry up before it's recycled or freed.
 *
 * @param[in] entry  The entry to clean
 */
static inline void
mdcache_lru_clean(mdcache_entry_t *entry)
{
	fsal_status_t status = {0, 0};

	/* Make sure any FSAL global file descriptor is closed. */
	status = fsal_close(&entry->obj_handle);

	if (FSAL_IS_ERROR(status)) {
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"Error closing file in cleanup: %s",
			fsal_err_txt(status));
	}

	/* Free SubFSAL resources */
	if (entry->sub_handle) {
		subcall(
			entry->sub_handle->obj_ops.release(entry->sub_handle)
		       );
		entry->sub_handle = NULL;
	}

	/* Done with the attrs */
	fsal_release_attrs(&entry->attrs);

	/* Clean our handle */
	fsal_obj_handle_fini(&entry->obj_handle);

	/* Clean out the export mapping before deconstruction */
	mdc_clean_entry(entry);

	/* Finalize last bits of the cache entry */
	mdcache_key_delete(&entry->fh_hk.key);
	PTHREAD_RWLOCK_destroy(&entry->content_lock);
	PTHREAD_RWLOCK_destroy(&entry->attr_lock);
}

/**
 * @brief Try to pull an entry off the queue
 *
 * This function examines the end of the specified queue and if the
 * entry found there can be re-used, it returns with the entry
 * locked.  Otherwise, it returns NULL.
 *
 * This function follows the locking discipline detailed above.  It
 * returns an lru entry removed from the queue system and which we are
 * permitted to dispose or recycle.
 *
 * @note The caller @a MUST @a NOT hold the lane lock
 *
 * @param[in] qid  Queue to reap
 * @return Available entry if found, NULL otherwise
 */

static uint32_t reap_lane;

static inline mdcache_lru_t *
lru_reap_impl(enum lru_q_id qid)
{
	uint32_t lane;
	struct lru_q_lane *qlane;
	struct lru_q *lq;
	mdcache_lru_t *lru;
	mdcache_entry_t *entry;
	uint32_t refcnt;
	cih_latch_t latch;
	int ix;

	lane = LRU_NEXT(reap_lane);
	for (ix = 0; ix < LRU_N_Q_LANES; ++ix, lane = LRU_NEXT(reap_lane)) {
		qlane = &LRU[lane];
		lq = (qid == LRU_ENTRY_L1) ? &qlane->L1 : &qlane->L2;

		QLOCK(qlane);
		lru = glist_first_entry(&lq->q, mdcache_lru_t, q);
		if (!lru)
			goto next_lane;
		refcnt = atomic_inc_int32_t(&lru->refcnt);
		entry = container_of(lru, mdcache_entry_t, lru);
		if (unlikely(refcnt != (LRU_SENTINEL_REFCOUNT + 1))) {
			/* cant use it. */
			mdcache_lru_unref(entry, LRU_UNREF_QLOCKED);
			goto next_lane;
		}
		/* potentially reclaimable */
		QUNLOCK(qlane);
		/* entry must be unreachable from CIH when recycled */
		if (cih_latch_entry(&entry->fh_hk.key, &latch, CIH_GET_WLOCK,
				    __func__, __LINE__)) {
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
				cih_hash_release(&latch);
				/* Note, we're not releasing our ref here.
				 * cih_remove_latched() called
				 * mdcache_lru_unref(), which released the
				 * sentinal ref, leaving just the one ref we
				 * took earlier.  Returning this as is leaves it
				 * with a ref of 1 (ie, just the sentinal ref)
				 * */
				goto out;
			}
			cih_hash_release(&latch);
			/* return the ref we took above--unref deals
			 * correctly with reclaim case */
			mdcache_lru_unref(entry, LRU_UNREF_QLOCKED);
		} else {
			/* ! QLOCKED but needs to be Unref'ed */
			mdcache_lru_unref(entry, LRU_FLAG_NONE);
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

static inline mdcache_lru_t *
lru_try_reap_entry(void)
{
	mdcache_lru_t *lru;

	if (lru_state.entries_used < lru_state.entries_hiwat)
		return NULL;

	/* XXX dang why not start with the cleanup list? */
	lru = lru_reap_impl(LRU_ENTRY_L2);
	if (!lru)
		lru = lru_reap_impl(LRU_ENTRY_L1);

	return lru;
}

/**
 * @brief Push a killed entry to the cleanup queue for out-of-line cleanup
 *
 * This function appends entry to the appropriate lane of the global cleanup
 * queue, and marks the entry.
 *
 * @param[in] entry  The entry to clean
 */
void
mdcache_lru_cleanup_push(mdcache_entry_t *entry)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];

	QLOCK(qlane);

	if (!(lru->qid == LRU_ENTRY_CLEANUP)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ_SAFE(lru, q);

		/* in with the new */
		q = &qlane->cleanup;
		lru_insert(lru, q, LRU_LRU);
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
 * mdcache_kill_entry is NOT suitable for this purpose).
 *
 * @param[in] entry  The entry to clean
 */
void
mdcache_lru_cleanup_try_push(mdcache_entry_t *entry)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	cih_latch_t latch;

	if (cih_latch_entry(&entry->fh_hk.key, &latch, CIH_GET_WLOCK,
			    __func__, __LINE__)) {
		uint32_t refcnt;

		QLOCK(qlane);

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
			atomic_set_uint32_t_bits(&entry->lru.flags,
						 LRU_CLEANUP);
			/* Note: we didn't take a ref here, so the only ref left
			 * is the one owned by mdcache_unexport().  When it
			 * unref's, that will free this object. */
		}

		QUNLOCK(qlane);

		cih_hash_release(&latch);
	}
}

/**
 * @brief Function that executes in the lru thread to process one lane
 *
 * @param[in]     lane          The lane to process
 * @param[in,out] totalclosed   Track the number of file closes
 *
 * @returns the number of files worked on (workdone)
 *
 */

static inline size_t lru_run_lane(size_t lane, uint64_t *const totalclosed)
{
	struct lru_q *q;
	/* The amount of work done on this lane on this pass. */
	size_t workdone = 0;
	/* The entry being examined */
	mdcache_lru_t *lru = NULL;
	/* Number of entries closed in this run. */
	size_t closed = 0;
	fsal_status_t status;
	/* a cache entry */
	mdcache_entry_t *entry;
	/* Current queue lane */
	struct lru_q_lane *qlane = &LRU[lane];
	/* entry refcnt */
	uint32_t refcnt;
	struct req_op_context ctx = {0};
	struct req_op_context *saved_ctx = op_ctx;
	struct mdcache_fsal_export *exp;
	struct fsal_export *export;
	bool not_support_ex;

	op_ctx = &ctx;

	q = &qlane->L1;

	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "Reaping up to %d entries from lane %zd",
		 lru_state.per_lane_work, lane);

	/* ACTIVE */
	QLOCK(qlane);
	qlane->iter.active = true;

	/* While for_each_safe per se is NOT MT-safe, the iteration can be made
	 * so by the convention that any competing thread which would invalidate
	 * the iteration also adjusts glist and (in particular) glistn */
	glist_for_each_safe(qlane->iter.glist, qlane->iter.glistn, &q->q) {
		struct lru_q *q;

		/* check per-lane work */
		if (workdone >= lru_state.per_lane_work)
			goto next_lane;

		lru = glist_entry(qlane->iter.glist, mdcache_lru_t, q);
		refcnt = atomic_inc_int32_t(&lru->refcnt);

		/* get entry early */
		entry = container_of(lru, mdcache_entry_t, lru);

		/* check refcnt in range */
		if (unlikely(refcnt > 2)) {
			mdcache_lru_unref(entry, LRU_UNREF_QLOCKED);
			/* but count it */
			workdone++;
			/* qlane LOCKED, lru refcnt is restored */
			continue;
		}

		/* Move entry to MRU of L2 */
		q = &qlane->L1;
		LRU_DQ_SAFE(lru, q);
		lru->qid = LRU_ENTRY_L2;
		q = &qlane->L2;
		lru_insert(lru, q, LRU_MRU);
		++(q->size);

		/* Drop the lane lock while performing (slow) operations on
		 * entry */
		QUNLOCK(qlane);

		/** @todo FSF: hmm, this looks hairy, we need a reference
		 *             to the export somehow?
		 */
		exp = atomic_fetch_voidptr(&entry->first_export);
		export = &exp->export;
		op_ctx->fsal_export = export;
		op_ctx->ctx_export = NULL;

		not_support_ex = !entry->obj_handle.fsal->m_ops.support_ex(
							&entry->obj_handle);

		if (not_support_ex) {
			/* Acquire the content lock first; we may need to look
			 * at fds and close it.
			 */
			PTHREAD_RWLOCK_wrlock(&entry->content_lock);
		}

		/* Make sure any FSAL global file descriptor is closed. */
		status = fsal_close(&entry->obj_handle);

		if (not_support_ex) {
			/* Release the content lock. */
			PTHREAD_RWLOCK_unlock(&entry->content_lock);
		}

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_CACHE_INODE_LRU,
				"Error closing file in LRU thread.");
		} else {
			++(*totalclosed);
			++closed;
		}

		QLOCK(qlane); /* QLOCKED */
		mdcache_lru_unref(entry, LRU_UNREF_QLOCKED);
		++workdone;
	} /* for_each_safe lru */

next_lane:
	qlane->iter.active = false; /* !ACTIVE */
	QUNLOCK(qlane);
	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "Actually processed %zd entries on lane %zd closing %zd descriptors",
		 workdone, lane, closed);

	op_ctx = saved_ctx;

	return workdone;
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
	time_t new_thread_wait;

	SetNameFunction("cache_lru");

	fds_avg = (lru_state.fds_hiwat - lru_state.fds_lowat) / 2;

	if (mdcache_param.use_fd_cache)
		extremis = (atomic_fetch_size_t(&open_fd_count) >
			    lru_state.fds_hiwat);

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "LRU awakes.");

	if (!woke) {
		/* If we make it all the way through a timed sleep
		   without being woken, we assume we aren't racing
		   against the impossible. */
		lru_state.futility = 0;
	}

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "lru entries: %" PRIu64,
		     lru_state.entries_used);

	/* Reap file descriptors.  This is a preliminary example of the
	   L2 functionality rather than something we expect to be
	   permanent.  (It will have to adapt heavily to the new FSAL
	   API, for example.) */

	currentopen = atomic_fetch_size_t(&open_fd_count);
	if ((currentopen < lru_state.fds_lowat)
	    && mdcache_param.use_fd_cache) {
		LogDebug(COMPONENT_CACHE_INODE_LRU,
			 "FD count is %zd and low water mark is %d: not reaping.",
			 atomic_fetch_size_t(&open_fd_count),
			 lru_state.fds_lowat);
		if (mdcache_param.use_fd_cache
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

		fdratepersec = (curr_time <= lru_state.prev_time)
			? 1 : (formeropen - lru_state.prev_fd_count) /
					(curr_time - lru_state.prev_time);

		LogFullDebug(COMPONENT_CACHE_INODE_LRU,
			     "fdrate:%u fdcount:%zd slept for %" PRIu64 " sec",
			     fdratepersec, formeropen,
			     ((uint64_t) (curr_time - lru_state.prev_time)));

		if (extremis) {
			LogDebug(COMPONENT_CACHE_INODE_LRU,
				 "Open FDs over high water mark, reapring aggressively.");
		}

		/* Total fds closed between all lanes and all current runs. */
		do {
			workpass = 0;
			for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
				LogDebug(COMPONENT_CACHE_INODE_LRU,
					 "Reaping up to %d entries from lane %zd",
					 lru_state.per_lane_work, lane);

				LogFullDebug(COMPONENT_CACHE_INODE_LRU,
					     "formeropen=%zd totalwork=%zd workpass=%zd totalclosed:%"
					     PRIu64, formeropen, totalwork,
					     workpass, totalclosed);

				workpass += lru_run_lane(lane, &totalclosed);
			}
			totalwork += workpass;
		} while (extremis && (workpass >= lru_state.per_lane_work)
			 && (totalwork < lru_state.biggest_window));

		currentopen = atomic_fetch_size_t(&open_fd_count);
		if (extremis
		    && ((currentopen > formeropen)
			|| (formeropen - currentopen <
			    (((formeropen -
			       lru_state.fds_hiwat) *
			      mdcache_param.required_progress) /
			     100)))) {
			if (++lru_state.futility >
			    mdcache_param.futility_count) {
				LogCrit(COMPONENT_CACHE_INODE_LRU,
					"Futility count exceeded.  The LRU thread is unable to make progress in reclaiming FDs.  Disabling FD cache.");
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
	fddelta = (currentopen > lru_state.fds_lowat)
			? (currentopen - lru_state.fds_lowat) : 0;
	fdmulti = (fddelta * 10) / fds_avg;
	fdmulti = fdmulti ? fdmulti : 1;
	fdwait_ratio = lru_state.fds_hiwat /
			((lru_state.fds_hiwat + fdmulti * fddelta) * fdnorm);

	new_thread_wait = threadwait * fdwait_ratio;

	if (new_thread_wait < mdcache_param.lru_run_interval / 10)
		new_thread_wait = mdcache_param.lru_run_interval / 10;

	fridgethr_setwait(ctx, new_thread_wait);

	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "After work, open_fd_count:%zd  count:%" PRIu64
		 " fdrate:%u threadwait=%" PRIu64,
		 atomic_fetch_size_t(&open_fd_count),
		 lru_state.entries_used, fdratepersec,
		 ((uint64_t) threadwait));
	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		     "currentopen=%zd futility=%d totalwork=%zd biggest_window=%d extremis=%d lanes=%d fds_lowat=%d ",
		     currentopen, lru_state.futility, totalwork,
		     lru_state.biggest_window, extremis, LRU_N_Q_LANES,
		     lru_state.fds_lowat);
}

/* Public functions */

/**
 * Initialize subsystem
 */
fsal_status_t
mdcache_lru_pkginit(void)
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
	frp.thread_delay = mdcache_param.lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	atomic_store_size_t(&open_fd_count, 0);

	/* Set high and low watermark for cache entries.  XXX This seems a
	   bit fishy, so come back and revisit this. */
	lru_state.entries_hiwat = mdcache_param.entries_hwmark;
	lru_state.entries_used = 0;

	/* Find out the system-imposed file descriptor limit */
	if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
		code = errno;
		LogCrit(COMPONENT_CACHE_INODE_LRU,
			"Call to getrlimit failed with error %d. This should not happen.  Assigning default of %d.",
			code, FD_FALLBACK_LIMIT);
		lru_state.fds_system_imposed = FD_FALLBACK_LIMIT;
	} else {
		if (rlim.rlim_cur < rlim.rlim_max) {
			/* Save the old soft value so we can fall back to it
			   if setrlimit fails. */
			rlim_t old_soft = rlim.rlim_cur;

			LogInfo(COMPONENT_CACHE_INODE_LRU,
				"Attempting to increase soft limit from %"
				PRIu64 " to hard limit of %" PRIu64,
				(uint64_t) rlim.rlim_cur,
				(uint64_t) rlim.rlim_max);
			rlim.rlim_cur = rlim.rlim_max;
			if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
				code = errno;
				LogWarn(COMPONENT_CACHE_INODE_LRU,
					"Attempt to raise soft FD limit to hard FD limit failed with error %d.  Sticking to soft limit.",
					code);
				rlim.rlim_cur = old_soft;
			}
		}
		if (rlim.rlim_cur == RLIM_INFINITY) {
			FILE *nr_open;

			nr_open = fopen("/proc/sys/fs/nr_open", "r");
			if (nr_open == NULL) {
				code = errno;
				LogWarn(COMPONENT_CACHE_INODE_LRU,
					"Attempt to open /proc/sys/fs/nr_open failed (%d)",
					code);
				goto err_open;
			}
			code = fscanf(nr_open, "%" SCNu32 "\n",
				      &lru_state.fds_system_imposed);
			if (code != 1) {
				code = errno;
				LogMajor(COMPONENT_CACHE_INODE_LRU,
					 "The rlimit on open file descriptors is infinite and the attempt to find the system maximum failed with error %d.",
					 code);
				LogMajor(COMPONENT_CACHE_INODE_LRU,
					 "Assigning the default fallback of %d which is almost certainly too small.",
					 FD_FALLBACK_LIMIT);
				LogMajor(COMPONENT_CACHE_INODE_LRU,
					 "If you are on a Linux system, this should never happen.");
				LogMajor(COMPONENT_CACHE_INODE_LRU,
					 "If you are running some other system, please set an rlimit on file descriptors (for example, with ulimit) for this process and consider editing "
					 __FILE__
					 "to add support for finding your system's maximum.");
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
	    (mdcache_param.fd_limit_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.fds_hiwat =
	    (mdcache_param.fd_hwmark_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.fds_lowat =
	    (mdcache_param.fd_lwmark_percent *
	     lru_state.fds_system_imposed) / 100;
	lru_state.futility = 0;

	lru_state.per_lane_work =
	    (mdcache_param.reaper_work / LRU_N_Q_LANES);
	lru_state.biggest_window =
	    (mdcache_param.biggest_window *
	     lru_state.fds_system_imposed) / 100;

	lru_state.prev_fd_count = 0;

	lru_state.caching_fds = mdcache_param.use_fd_cache;

	/* init queue complex */
	lru_init_queues();

	/* spawn LRU background thread */
	code = fridgethr_init(&lru_fridge, "LRU_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Unable to initialize LRU fridge, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	code = fridgethr_submit(lru_fridge, lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Unable to start LRU thread, error code %d.", code);
		return fsalstat(posix2fsal_error(code), code);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * Shutdown subsystem
 *
 * @return 0 on success, POSIX errors on failure.
 */
fsal_status_t
mdcache_lru_pkgshutdown(void)
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
	return fsalstat(posix2fsal_error(rc), rc);
}

static inline void init_rw_locks(mdcache_entry_t *entry)
{
	/* Initialize the entry locks */
	PTHREAD_RWLOCK_init(&entry->attr_lock, NULL);
	PTHREAD_RWLOCK_init(&entry->content_lock, NULL);
}

static fsal_status_t
alloc_cache_entry(mdcache_entry_t **entry)
{
	mdcache_entry_t *nentry;

	nentry = pool_alloc(mdcache_entry_pool);

	/* Initialize the entry locks */
	init_rw_locks(nentry);

	(void) atomic_inc_int64_t(&lru_state.entries_used);
	*entry = nentry;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Re-use or allocate an entry
 *
 * This function repurposes a resident entry in the LRU system if the system is
 * above the high-water mark, and allocates a new one otherwise.  On success,
 * this function always returns an entry with two references (one for the
 * sentinel, one to allow the caller's use.)
 *
 * @param[out] entry Returned status
 *
 * @return CACHE_INODE_SUCCESS or error.
 */
fsal_status_t
mdcache_lru_get(mdcache_entry_t **entry)
{
	mdcache_lru_t *lru;
	fsal_status_t status = {0, 0};
	mdcache_entry_t *nentry = NULL;

	lru = lru_try_reap_entry();
	if (lru) {
		/* we uniquely hold entry */
		nentry = container_of(lru, mdcache_entry_t, lru);
		LogFullDebug(COMPONENT_CACHE_INODE_LRU,
			     "Recycling entry at %p.", nentry);
		mdcache_lru_clean(nentry);
		memset(&nentry->attrs, 0, sizeof(nentry->attrs));
		init_rw_locks(nentry);
	} else {
		/* alloc entry */
		status = alloc_cache_entry(&nentry);
		if (!nentry)
			goto out;
	}

	/* Since the entry isn't in a queue, nobody can bump refcnt. */
	nentry->lru.refcnt = 2;
	nentry->lru.noscan_refcnt = 0;
	nentry->lru.cf = 0;
	nentry->lru.lane = lru_lane_of_entry(nentry);

	/* Enqueue. */
	lru_insert_entry(nentry, &LRU[nentry->lru.lane].L1, LRU_LRU);

 out:
	*entry = nentry;
	return status;
}

/**
 * @brief Function to let the state layer mark an entry noscan
 *
 * This function moves the given entry to the noscan queue partition
 * for its lane.  If the entry is already noscan, it is a no-op.
 *
 * @param[in] entry  The entry to be moved
 *
 * @return FSAL status
 */
fsal_status_t
mdcache_inc_noscan_ref(mdcache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];

	/* Pin ref is infrequent, and never concurrent because SAL invariantly
	 * holds the state lock exclusive whenever it is called. */
	if (entry->lru.flags & LRU_CLEANUP)
		return fsalstat(ERR_FSAL_STALE, 0);

	QLOCK(qlane);

	/* Mark noscan if not noscan already */
	cond_noscan_entry(entry, LRU_FLAG_NONE /* future */);

	/* take noscan ref count */
	entry->lru.noscan_refcnt++;

	QUNLOCK(qlane);		/* !LOCKED (lane) */

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Function to let the state layer rlease a noscan ref
 *
 * This function moves the given entry out of the noscan queue
 * partition for its lane.  If the entry is not noscan, it is a
 * no-op.
 *
 * @param[in] entry      The entry to be moved
 *
 */
void mdcache_dec_noscan_ref(mdcache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lane];

	/* Pin ref is infrequent, and never concurrent because SAL invariantly
	 * holds the state lock exclusive whenever it is called. */
	QLOCK(qlane);

	entry->lru.noscan_refcnt--;
	if (unlikely(entry->lru.noscan_refcnt == 0)) {

		/* entry could infrequently be on the cleanup queue */
		if (lru->qid == LRU_ENTRY_NOSCAN) {
			/* remove from noscan */
			struct lru_q *q = &qlane->noscan;
			/* XXX skip L1 iteration fixups */
			glist_del(&lru->q);
			--(q->size);
			/* add to MRU of L1 */
			q = &qlane->L1;
			lru_insert(lru, q, LRU_MRU);
			++(q->size);
		}
	}

	QUNLOCK(qlane);
}

/**
 * @brief Return true if a file is noscan.
 *
 * This function returns true if a file is noscan.
 *
 * @param[in] entry The file to be checked
 *
 * @return true if noscan, false otherwise.
 */
bool
mdcache_is_noscan(mdcache_entry_t *entry)
{
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	int rc;

	QLOCK(qlane);
	rc = (entry->lru.noscan_refcnt > 0);
	QUNLOCK(qlane);

	return rc;
}

/**
 * @brief Get a reference
 *
 * This function acquires a reference on the given cache entry.
 *
 * @param[in] entry  The entry on which to get a reference
 * @param[in] flags  One of LRU_REQ_INITIAL, or LRU_FLAG_NONE
 *
 * A flags value of LRU_REQ_INITIAL indicates an initial
 * reference.  A non-initial reference is an "extra" reference in some call
 * path, hence does not influence LRU, and is lockless.
 *
 * A flags value of LRU_REQ_INITIAL indicates an ordinary initial reference,
 * and strongly influences LRU.  Essentially, the first ref during a callpath
 * should take an LRU_REQ_INITIAL ref, and all subsequent callpaths should take
 * LRU_FLAG_NONE refs.
 *
 * @return FSAL status
 */
fsal_status_t
mdcache_lru_ref(mdcache_entry_t *entry, uint32_t flags)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	struct lru_q *q;

	if ((flags & LRU_REQ_INITIAL) == 0)
		if (lru->flags & LRU_CLEANUP)
			return fsalstat(ERR_FSAL_STALE, 0);

	(void) atomic_inc_int32_t(&entry->lru.refcnt);

	/* adjust LRU on initial refs */
	if (flags & LRU_REQ_INITIAL) {

		/* do it less */
		if ((atomic_inc_int32_t(&entry->lru.cf) % 3) != 0)
			goto out;

		QLOCK(qlane);

		switch (lru->qid) {
		case LRU_ENTRY_L1:
			q = lru_queue_of(entry);
			/* advance entry to MRU (of L1) */
			LRU_DQ_SAFE(lru, q);
			lru_insert(lru, q, LRU_MRU);
			++(q->size);
			break;
		case LRU_ENTRY_L2:
			q = lru_queue_of(entry);
			/* move entry to LRU of L1 */
			glist_del(&lru->q);	/* skip L1 fixups */
			--(q->size);
			q = &qlane->L1;
			lru_insert(lru, q, LRU_LRU);
			++(q->size);
			break;
		default:
			/* do nothing */
			break;
		}		/* switch qid */
		QUNLOCK(qlane);
	}			/* initial ref */
 out:
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
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
 * @return true if entry freed, false otherwise
 */
bool
mdcache_lru_unref(mdcache_entry_t *entry, uint32_t flags)
{
	int32_t refcnt;
	bool do_cleanup = false;
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	bool qlocked = flags & LRU_UNREF_QLOCKED;
	bool other_lock_held = entry->fsobj.hdl.no_cleanup;
	bool freed = false;

	if (!qlocked && !other_lock_held) {
		QLOCK(qlane);
		if (((entry->lru.flags & LRU_CLEANED) == 0) &&
		    (entry->lru.qid == LRU_ENTRY_CLEANUP)) {
			do_cleanup = true;
			atomic_set_uint32_t_bits(&entry->lru.flags,
						 LRU_CLEANED);
		}
		QUNLOCK(qlane);

		if (do_cleanup) {
			LogDebug(COMPONENT_CACHE_INODE,
				 "LRU_ENTRY_CLEANUP of entry %p",
				 entry);
			state_wipe_file(&entry->obj_handle);
		}
	}

	refcnt = atomic_dec_int32_t(&entry->lru.refcnt);

	if (unlikely(refcnt == 0)) {

		struct lru_q *q;

		/* we MUST recheck that refcount is still 0 */
		if (!qlocked) {
			QLOCK(qlane);
			refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
		}

		if (unlikely(refcnt > 0)) {
			if (!qlocked)
				QUNLOCK(qlane);
			goto out;
		}

		/* Really zero.  Remove entry and mark it as dead. */
		q = lru_queue_of(entry);
		if (q) {
			/* as of now, entries leaving the cleanup queue
			 * are LRU_ENTRY_NONE */
			LRU_DQ_SAFE(&entry->lru, q);
		}

		if (!qlocked)
			QUNLOCK(qlane);

		mdcache_lru_clean(entry);
		pool_free(mdcache_entry_pool, entry);
		freed = true;

		(void) atomic_dec_int64_t(&lru_state.entries_used);
	}			/* refcnt == 0 */
 out:
	return freed;
}

/**
 * @brief Put back a raced initial reference
 *
 * This function returns an entry previously returned from
 * @ref mdcache_lru_get, in the uncommon circumstance that it will not
 * be used.
 *
 * @param[in] entry  The entry on which to release a reference
 * @param[in] flags  Currently significant are and LRU_FLAG_LOCKED
 *                   (indicating that the caller holds the LRU mutex
 *                   lock for this entry.)
 */
void
mdcache_lru_putback(mdcache_entry_t *entry, uint32_t flags)
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
	pool_free(mdcache_entry_pool, entry);
	(void) atomic_dec_int64_t(&lru_state.entries_used);

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
