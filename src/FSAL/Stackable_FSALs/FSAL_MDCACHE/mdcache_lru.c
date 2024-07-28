// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "nfs_init.h"
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
#include "atomic_utils.h"
#include "gsh_intrinsic.h"
#include "sal_functions.h"
#include "nfs_exports.h"
#include "sys_resource.h"

#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/mdcache.h"
#endif

/**
 *
 * @file mdcache_lru.c
 * @author Matt Benjamin <matt@linuxbox.com>
 * @brief Constant-time MDCACHE cache management implementation
 */

/**
 * @page LRUOverview LRU Overview
 *
 * This module implements a constant-time cache management strategy
 * based on LRU.  Some ideas are taken from 2Q [Johnson and Shasha 1994]
 * and MQ [Zhou, Chen, Li 2004].  In this system, cache management does
 * interact with cache entry lifecycle, but the lru queue is not a garbage
 * collector. Most importantly, cache management operations execute in constant
 * time, as expected with LRU (and MQ).
 *
 * Cache entries in use by a currently-active protocol request (or other
 * operation) have a positive refcount, and therefore should not be present
 * at the cold end of an lru queue if the cache is well-sized.
 *
 * As noted below, initial references to cache entries may only be granted
 * under the MDCACHE hash table latch.  Likewise, entries must first be
 * made unreachable to the MDCACHE hash table, then independently reach
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
 * A single queue lane, holding all entries.
 */

struct lru_q_lane {
	struct lru_q L1;
	struct lru_q L2;
	struct lru_q cleanup;	/* deferred cleanup */
	struct lru_q ACTIVE;	/* active references */
	pthread_mutex_t ql_mtx;

	CACHE_PAD(0);
};

/* The queue lock and the partition lock interact.  The partition lock must
 * always be taken before the queue lock to avoid deadlock */
#ifdef USE_LTTNG
#define QLOCK(qlane) \
	do { \
		PTHREAD_MUTEX_lock(&(qlane)->ql_mtx); \
		GSH_UNIQUE_AUTO_TRACEPOINT(mdcache, qlock, TRACE_DEBUG, \
			"QLOCK. qlane: {}", qlane); \
	} while (0)

#define QUNLOCK(qlane) \
	do { \
		GSH_UNIQUE_AUTO_TRACEPOINT(mdcache, qunlock, TRACE_DEBUG, \
			"QUNLOCK. qlane: {}", qlane); \
		PTHREAD_MUTEX_unlock(&(qlane)->ql_mtx); \
	} while (0)
#else
#define QLOCK(qlane) \
	PTHREAD_MUTEX_lock(&(qlane)->ql_mtx)
#define QUNLOCK(qlane) \
	PTHREAD_MUTEX_unlock(&(qlane)->ql_mtx)
#endif

/**
 * A multi-level LRU algorithm inspired by MQ [Zhou].  Transition from
 * L1 to L2 implies various checks (open files, etc) have been
 * performed, so ensures they are performed only once.  A
 * correspondence to the "scan resistance" property of 2Q and MQ is
 * accomplished by recycling/clean loads onto the LRU of L1.  Async
 * processing onto L2 constrains oscillation in this algorithm.
 */

static struct lru_q_lane LRU[LRU_N_Q_LANES];
static struct lru_q_lane CHUNK_LRU[LRU_N_Q_LANES];

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

static const uint32_t FD_FALLBACK_LIMIT = 0x400;

/* Some helper macros */
#define LRU_NEXT(n) \
	(atomic_inc_uint32_t(&(n)) % LRU_N_Q_LANES)

/* Delete lru, use iif the current thread is not the LRU
 * thread.  The node being removed is lru, glist a pointer to L1's q,
 * qlane its lane. */
#define LRU_DQ(lru, q) \
	do { \
		glist_del(&(lru)->q); \
		--((q)->size); \
	} while (0)

#define CHUNK_LRU_DQ(lru, qq) \
	do { \
		glist_del(&(lru)->q); \
		--((qq)->size); \
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
 * This function initializes a single queue partition (L1, L2,
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
		struct lru_q_lane *qlane;

		/* Initialize mdcache_entry_t LRU */
		qlane = &LRU[ix];

		/* one mutex per lane */
		PTHREAD_MUTEX_init(&qlane->ql_mtx, NULL);

		/* init lane queues */
		lru_init_queue(&LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&LRU[ix].cleanup, LRU_ENTRY_CLEANUP);
		lru_init_queue(&LRU[ix].ACTIVE, LRU_ENTRY_ACTIVE);

		/* Initialize dir_chunk LRU */
		qlane = &CHUNK_LRU[ix];

		/* one mutex per lane */
		PTHREAD_MUTEX_init(&qlane->ql_mtx, NULL);

		/* init lane queues */
		lru_init_queue(&CHUNK_LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&CHUNK_LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&CHUNK_LRU[ix].cleanup, LRU_ENTRY_CLEANUP);
	}
}

static inline void
lru_destroy_queues(void)
{
	int ix;

	for (ix = 0; ix < LRU_N_Q_LANES; ++ix) {
		struct lru_q_lane *qlane;

		/* Destroy mdcache_entry_t LRU */
		qlane = &LRU[ix];
		PTHREAD_MUTEX_destroy(&qlane->ql_mtx);

		/* Destroy dir_chunk LRU */
		qlane = &CHUNK_LRU[ix];
		PTHREAD_MUTEX_destroy(&qlane->ql_mtx);
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
	case LRU_ENTRY_L1:
		q = &LRU[(entry->lru.lane)].L1;
		break;
	case LRU_ENTRY_L2:
		q = &LRU[(entry->lru.lane)].L2;
		break;
	case LRU_ENTRY_CLEANUP:
		q = &LRU[(entry->lru.lane)].cleanup;
		break;
	case LRU_ENTRY_ACTIVE:
		q = &LRU[(entry->lru.lane)].ACTIVE;
		break;
	default:
		/* LRU_NO_LANE */
		q = NULL;
		break;
	}			/* switch */

	return q;
}

/**
 * @brief Return a pointer to the current queue of chunk
 *
 * This function returns a pointer to the queue on which a chunk is linked,
 * or NULL if chunk is not on any queue.
 *
 * @note The caller @a MUST hold the lane lock
 *
 * @param[in] chunk  The chunk.
 *
 * @return A pointer to chunk's current queue, NULL if none.
 */
static inline struct lru_q *
chunk_lru_queue_of(struct dir_chunk *chunk)
{
	struct lru_q *q;

	switch (chunk->chunk_lru.qid) {
	case LRU_ENTRY_L1:
		q = &CHUNK_LRU[(chunk->chunk_lru.lane)].L1;
		break;
	case LRU_ENTRY_L2:
		q = &CHUNK_LRU[(chunk->chunk_lru.lane)].L2;
		break;
	case LRU_ENTRY_CLEANUP:
		q = &CHUNK_LRU[(chunk->chunk_lru.lane)].cleanup;
		break;
	case LRU_ENTRY_ACTIVE:
		/* Should never happen... */
		q = &CHUNK_LRU[(chunk->chunk_lru.lane)].ACTIVE;
		break;
	default:
		/* LRU_NO_LANE */
		q = NULL;
		break;
	}			/* switch */

	return q;
}

/**
 * @brief Get the appropriate lane for a LRU chunk or entry
 *
 * This function gets the LRU lane by taking the modulus of the
 * supplied pointer.
 *
 * @param[in] entry  A pointer to a LRU chunk or entry
 *
 * @return The LRU lane in which that entry should be stored.
 */
static inline uint32_t
lru_lane_of(void *entry)
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
 */
static inline void
lru_insert(mdcache_lru_t *lru, struct lru_q *q)
{
	lru->qid = q->id;	/* initial */
	if (lru->qid == LRU_ENTRY_CLEANUP) {
		atomic_set_uint32_t_bits(&lru->flags, LRU_CLEANUP);
		/* Add to tail of cleanup queue */
		glist_add_tail(&q->q, &lru->q);
	} else {
		glist_add(&q->q, &lru->q);
	}
	++(q->size);
}

/**
 * @brief Insert a chunk into the specified queue and lane with locking
 *
 * This function determines the queue corresponding to the supplied
 * lane and flags, inserts the chunk into that queue, and updates the
 * chunk to hold the flags and lane.
 *
 * @note The caller MUST NOT hold a lock on the queue lane.
 *
 * @param[in] lru    The LRU chunk to insert
 * @param[in] q      The queue to insert on
 */
static inline void
lru_insert_chunk(struct dir_chunk *chunk, struct lru_q *q)
{
	mdcache_lru_t *lru = &chunk->chunk_lru;
	struct lru_q_lane *qlane = &CHUNK_LRU[lru->lane];

	QLOCK(qlane);

	lru_insert(lru, q);

	QUNLOCK(qlane);
}

/*
 * @brief Move an entry from LRU L1 or L2 queues to the ACTIVE queue.
 *
 * @param [in] entry  Entry to adjust.
 */
static inline void
make_active_lru(mdcache_entry_t *entry)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	struct lru_q *q;

	QLOCK(qlane);
	switch (lru->qid) {
	case LRU_ENTRY_L1:
		q = lru_queue_of(entry);
		/* move entry to MRU of ACTIVE */
		LRU_DQ(lru, q);
		q = &qlane->ACTIVE;
		lru_insert(lru, q);
		break;
	case LRU_ENTRY_L2:
		q = lru_queue_of(entry);
		/* move entry to MRU of ACTIVE */
		LRU_DQ(lru, q);
		q = &qlane->ACTIVE;
		lru_insert(lru, q);
		break;
	case LRU_ENTRY_ACTIVE:
		q = lru_queue_of(entry);
		/* advance entry to MRU (of ACTIVE) */
		LRU_DQ(lru, q);
		lru_insert(lru, q);
		break;
	default:
		/* do nothing */
		break;
	}	/* switch qid */
	QUNLOCK(qlane);
}

/*
 * @brief Move an active entry from ACTIVE queue to MRU of L1 or L2.
 *
 * If the entry is in the cleanup queue, do nothing.
 *
 * Assumes qlane lock is held.
 *
 * @param [in] entry  Entry to adjust.
 */
static inline void
make_inactive_lru(mdcache_entry_t *entry)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	struct lru_q *q;

	switch (lru->qid) {
	case LRU_ENTRY_L1:
	case LRU_ENTRY_L2:
		assert(false);
		break;
	case LRU_ENTRY_ACTIVE:
		/* Move entry to MRU of L1 or L2 */
		q = lru_queue_of(entry);
		LRU_DQ(&entry->lru, q);

		if (atomic_fetch_uint32_t(&entry->lru.flags)
		    & LRU_EVER_PROMOTED) {
			/* If entry was ever promoted, insert into L1. */
			q = &qlane->L1;
		} else {
			/* Entry was never promoted, only ever used in a
			 * directory scan, return to L2.
			 */
			q = &qlane->L2;
		}

		lru_insert(&entry->lru, q);
		break;
	case LRU_ENTRY_CLEANUP:
	default:
		/* do nothing */
		break;
	}	/* switch qid */
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

	/* Free SubFSAL resources */
	if (entry->sub_handle) {
		/* There are four basic paths to get here.
		 *
		 * One path is that this cache entry is being reaped. In that
		 * case, if an unexport is in progress removing the last export
		 * this entry was mapped to, in the process of being completely
		 * detached from an export, it also became unreapable (placed on
		 * the LRU_ENTRY_CLEANUP queue not L1 or L2). Therefore, if we
		 * get here with a reaped entry, it MUST still be attached to
		 * an export.
		 *
		 * Another path to get here is the export is still valid, and
		 * this entry is being killed. In that case, all the export
		 * stuff is fine.
		 *
		 * Another path is that we have removed the final export, and
		 * unexport is releasing the last reference. In that case,
		 * the unexport process has the export in question in the op_ctx
		 * so we are fine.
		 *
		 * The final case is that this entry was referenced by a thread
		 * other than the unexport, and the operational thread is the
		 * one releasing the last LRU reference. In that case, the
		 * caller's op_ctx must have the correct export.
		 *
		 * This is true even for operations that require two handles.
		 * NFS v3 checks for xdev before converting from a handle to an
		 * LRU reference. NFS v4 holds an LRU reference for the saved FH
		 * so the last reference can only be dropped when the saved FH
		 * is cleaned up, which will be done with the correct op_ctx. 9P
		 * also assures that LRU references are released with the proper
		 * op_ctx.
		 *
		 * So in all cases, we can either trust the current export, or
		 * we can use the first_export_id to get a valid export for
		 * a reaping case.
		 */
		struct req_op_context op_context;
		bool used_ctx = false;
		int32_t export_id;
		struct gsh_export *export;

		/* Find the first export id. */
		export_id = atomic_fetch_int32_t(&entry->first_export_id);

		/* Check if we have a valid op_ctx */
		if (export_id >= 0 && (op_ctx == NULL ||
		    op_ctx->ctx_export == NULL ||
		    op_ctx->ctx_export->export_id != export_id)) {
			/* If the entry's first_export_id is valid and does not
			 * match the current op_ctx, set up a new context
			 * using first_export_id to ensure the op_ctx export is
			 * valid for the entry.
			 *
			 * Get a reference to the first_export_id.
			 */
			export = get_gsh_export(export_id);

			if (export == NULL) {
				/* This really should not happen, if an unexport
				 * is in progress, the export_id is now not
				 * removed until after mdcache has detached all
				 * entries from the export. An entry that is
				 * actually in the process of being detached has
				 * an LRU reference which prevents it from being
				 * reaped, so there is no path to get into
				 * mdcache_lru_clean without the export still
				 * being valid.
				 */
				LogFatal(COMPONENT_MDCACHE,
					 "An entry (%p) having an unmappable export_id (%"
					 PRIi32") is unexpected",
					 entry, export_id);
			}

			LogFullDebug(COMPONENT_MDCACHE,
				     "Creating a new context with export id%"
				     PRIi32,
				     export_id);

			init_op_context_simple(&op_context, export,
					       export->fsal_export);
			used_ctx = true;
		} else {
			/* We MUST have a valid op_ctx based on the conditions
			 * we could get here. first_export_id coild be -1 or it
			 * could match the current op_ctx export. In either case
			 * we will trust the current op_ctx.
			 */
			assert(op_ctx);
			assert(op_ctx->ctx_export);
			LogFullDebug(COMPONENT_MDCACHE,
				     "Trusting op_ctx export id %"PRIu16,
				     op_ctx->ctx_export->export_id);
		}

		/* Make sure any FSAL global file descriptor is closed.
		 * Don't bother with the content_lock since we have exclusive
		 * ownership of this entry.
		 */
		status = fsal_close(&entry->obj_handle);

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_MDCACHE_LRU,
				"Error closing file in cleanup: %s",
				fsal_err_txt(status));
		}

		subcall(
			entry->sub_handle->obj_ops->release(entry->sub_handle)
		       );
		entry->sub_handle = NULL;

		if (used_ctx) {
			/* We had to use our own op_ctx, clean it up and revert
			 * to the saved op_ctx.
			 */
			release_op_context();
		}
	}

	/* Done with the attrs */
	fsal_release_attrs(&entry->attrs);

	/* Clean out the export mapping before deconstruction */
	mdc_clean_entry(entry);

	/* Clean our handle */
	fsal_obj_handle_fini(&entry->obj_handle, true);

	/* Finalize last bits of the cache entry, delete the key if any and
	 * destroy the rw locks.
	 */
	mdcache_key_delete(&entry->fh_hk.key);
	PTHREAD_RWLOCK_destroy(&entry->content_lock);
	PTHREAD_RWLOCK_destroy(&entry->attr_lock);

	state_hdl_cleanup(entry->obj_handle.state_hdl, entry->obj_handle.type);

	if (entry->obj_handle.type == DIRECTORY)
		PTHREAD_SPIN_destroy(&entry->fsobj.fsdir.fsd_spin);
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
 * @return Available entry if found, NULL otherwisem the reference held on
 * the object is a LRU_TEMP_REF.
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
		if (!lru) {
			QUNLOCK(qlane);
			continue;
		}
#if 0
		/* effectively do... this compiled out to help cross reference
		 */
		mdcache_lru_ref(entry, LRU_TEMP_REF);
#endif
		refcnt = atomic_inc_int32_t(&lru->refcnt);
		const int32_t active_refcnt =
			atomic_fetch_int32_t(&entry->lru.active_refcnt);
		entry = container_of(lru, mdcache_entry_t, lru);
		GSH_UNIQUE_AUTO_TRACEPOINT(mdcache, mdc_lru_ref, TRACE_DEBUG,
			"lru ref. handle: {}, sub handle: {}, refcnt: {}, active_refcnt: {}",
			&entry->obj_handle, entry->sub_handle, refcnt,
			active_refcnt);

		QUNLOCK(qlane);

		if (unlikely(refcnt != (LRU_SENTINEL_REFCOUNT + 1))) {
			/* can't use it. */
			mdcache_lru_unref(entry, LRU_TEMP_REF);
			continue;
		}
		/* potentially reclaimable */
		/* entry must be unreachable from CIH when recycled */
		cih_latch_entry(&entry->fh_hk.key, &latch, __func__, __LINE__);
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

			GSH_AUTO_TRACEPOINT(mdcache, mdc_lru_reap,
				TRACE_DEBUG,
				"lru unref. handle: {}, refcnt: {}",
				&entry->obj_handle, entry->lru.refcnt);

			LRU_DQ(lru, q);
			entry->lru.qid = LRU_ENTRY_NONE;
			QUNLOCK(qlane);
			cih_remove_latched(entry, &latch,
					   CIH_REMOVE_UNLOCK);
			/* Note, we're not releasing our ref here.
			 * cih_remove_latched() called
			 * mdcache_lru_unref(), which released the
			 * sentinel ref, leaving just the one ref we
			 * took earlier.  Returning this as is leaves it
			 * with a ref of 1 (ie, just the temp ref)
			 * */
			goto out;
		}
		cih_hash_release(&latch);
		QUNLOCK(qlane);
		/* return the ref we took above--unref deals
		 * correctly with reclaim case */
		mdcache_lru_unref(entry, LRU_TEMP_REF);
	}			/* foreach lane */

	/* ! reclaimable */
	lru = NULL;
 out:
	return lru;
}

static inline mdcache_lru_t *
lru_try_reap_entry(uint32_t flags)
{
	mdcache_lru_t *lru;

	if (atomic_fetch_uint64_t(&lru_state.entries_used) <
	    lru_state.entries_hiwat)
		return NULL;

	/* XXX dang why not start with the cleanup list? */
	lru = lru_reap_impl(LRU_ENTRY_L2);
	if (!lru)
		lru = lru_reap_impl(LRU_ENTRY_L1);

	return lru;
}

/**
 * @brief Try to pull an chunk off the queue
 *
 * This function examines the end of the specified queue and if the
 * chunk found there can be re-used.  Otherwise, it returns NULL.
 *
 * This function follows the locking discipline detailed above.  It
 * returns an lru object removed from the queue system and which we are
 * permitted to dispose or recycle.
 *
 * This function can reap a chunk from the directory a chunk is requested
 * for. In that case, since the content_lock is already held, we can
 * proceed somewhat easier.
 *
 * @note The caller @a MUST @a NOT hold the lane lock
 *
 * @param[in] qid        Queue to reap
 * @param[in] parent     The directory we desire a chunk for
 *
 * @return Available chunk if found, NULL otherwise
 */

static uint32_t chunk_reap_lane;

static inline mdcache_lru_t *
lru_reap_chunk_impl(enum lru_q_id qid, mdcache_entry_t *parent)
{
	uint32_t lane;
	struct lru_q_lane *qlane;
	struct lru_q *lq;
	mdcache_lru_t *lru;
	mdcache_entry_t *entry;
	struct dir_chunk *chunk;
	int ix;
	int32_t refcnt;

	lane = LRU_NEXT(chunk_reap_lane);

	for (ix = 0;
	     ix < LRU_N_Q_LANES;
	     ++ix, lane = LRU_NEXT(chunk_reap_lane)) {

		qlane = &CHUNK_LRU[lane];
		lq = (qid == LRU_ENTRY_L1) ? &qlane->L1 : &qlane->L2;

		QLOCK(qlane);
		lru = glist_first_entry(&lq->q, mdcache_lru_t, q);

		if (!lru) {
			QUNLOCK(qlane);
			continue;
		}

		refcnt = atomic_fetch_int32_t(&lru->refcnt);
		assert(refcnt);
		if (refcnt != (LRU_SENTINEL_REFCOUNT)) {
			/* We can't reap a chunk with a ref */
			QUNLOCK(qlane);
			continue;
		}

		/* Get the chunk and parent entry that owns the chunk, all of
		 * this is valid because we hold the QLANE lock, the chunk was
		 * in the LRU, and thus the chunk is not yet being destroyed,
		 * and thus the parent entry must still also be valid.
		 */
		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		entry = chunk->parent;

		/* We need entry's content_lock to clean this chunk.
		 * The usual lock order is content_lock followed by
		 * chunk QLANE lock. Here we already have chunk QLANE
		 * lock, so we try to acquire content_lock. If we fail
		 * to acquire it, we just look for another chunk to
		 * reap!
		 *
		 * Note that the entry is valid but it could be in the
		 * process of getting destroyed (refcnt could be 0)! If
		 * we do get the content_lock, it should continue to be
		 * valid until we release the lock!
		 *
		 * If the entry is same as parent, we should already
		 * have the content lock.
		 */
		if (entry == parent ||
		    pthread_rwlock_trywrlock(&entry->content_lock) == 0) {
			/* Dequeue the chunk so it won't show up anymore */
			CHUNK_LRU_DQ(lru, lq);
			chunk->chunk_lru.qid = LRU_ENTRY_NONE;

			GSH_AUTO_TRACEPOINT(mdcache, mdc_lru_reap_chunk,
				TRACE_DEBUG,
				"lru unref. handle: {}, chunk: {}",
				&entry->obj_handle, chunk);

			/* Clean the chunk out and indicate the directory
			 * is no longer completely populated.  We don't
			 * need to hold a ref on the entry as we hold its
			 * content_lock and the chunk is valid under QLANE
			 * lock.
			 */
			mdcache_clean_dirent_chunk(chunk);
			atomic_clear_uint32_t_bits(&entry->mde_flags,
						   MDCACHE_DIR_POPULATED);

			if (entry != parent) {
				/* And now we're done with the parent of the
				 * chunk if it wasn't the directory we are
				 * acquiring a new chunk for.
				 */
				PTHREAD_RWLOCK_unlock(&entry->content_lock);
			}
			QUNLOCK(qlane);
			return lru;
		}

		/* Couldn't get the content_lock, the parent is busy
		 * doing something with dirents... This chunk is not
		 * eligible for reaping. Try the next lane...
		 */
		QUNLOCK(qlane);
	}	/* foreach lane */

	/* ! reclaimable */
	return NULL;
}

/**
 * @brief Re-use or allocate a chunk
 *
 * This function repurposes a resident chunk in the LRU system if the system is
 * above the high-water mark, and allocates a new one otherwise.  The resulting
 * chunk is inserted into the chunk list.
 *
 * @note The caller must hold the content_lock of the parent for write.
 *
 * @param[in] parent     The parent directory we desire a chunk for
 * @param[in] prev_chunk If non-NULL, the previous chunk in this directory
 * @param[in] whence	 If @a prev_chunk is NULL, the starting whence of chunk
 *
 * @return reused or allocated chunk with a ref taken for the caller
 */
struct dir_chunk *mdcache_get_chunk(mdcache_entry_t *parent,
				    struct dir_chunk *prev_chunk,
				    fsal_cookie_t whence)
{
	mdcache_lru_t *lru = NULL;
	struct dir_chunk *chunk = NULL;

	/* Get a ref on prev_chunk, so that it's not reaped */
	if (prev_chunk)
		mdcache_lru_ref_chunk(prev_chunk);

	if (lru_state.chunks_used >= lru_state.chunks_hiwat) {
		lru = lru_reap_chunk_impl(LRU_ENTRY_L2, parent);
		if (!lru)
			lru = lru_reap_chunk_impl(
					LRU_ENTRY_L1, parent);
	}

	if (lru) {
		/* we uniquely hold chunk, it has already been cleaned up.
		 * The dirents list is effectively properly initialized.
		 */
		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		LogFullDebug(COMPONENT_MDCACHE,
			     "Recycling chunk at %p.", chunk);
	} else {
		/* alloc chunk (if fails, aborts) */
		chunk = gsh_calloc(1, sizeof(struct dir_chunk));
		glist_init(&chunk->dirents);
		LogFullDebug(COMPONENT_MDCACHE,
			     "New chunk %p.", chunk);
		(void) atomic_inc_int64_t(&lru_state.chunks_used);
	}

	/* Set the chunk's parent and insert */
	chunk->parent = parent;
	glist_add_tail(&chunk->parent->fsobj.fsdir.chunks, &chunk->chunks);
	if (prev_chunk) {
		chunk->reload_ck = glist_last_entry(&prev_chunk->dirents,
						    mdcache_dir_entry_t,
						    chunk_list)->ck;
		/* unref prev_chunk as we had got a ref on prev_chunk
		 * at the beginning of this function
		 */
		mdcache_lru_unref_chunk(prev_chunk);
	} else {
		chunk->reload_ck = whence;
	}

	chunk->chunk_lru.refcnt = 2;
	chunk->chunk_lru.cf = 0;
	chunk->chunk_lru.lane = lru_lane_of(chunk);

	/* Enqueue into MRU of L2.
	 *
	 * NOTE: A newly allocated and filled chunk will be promoted to L1 LRU
	 *       when readdir_chunked starts passing entries up to the caller.
	 *       This gets us the expected positioning for a new chunk that is
	 *       utilized to form a readdir response.
	 *
	 *       The benefit of this mechanism comes when the FSAL supports
	 *       readahead. In that case, the chunks that are readahead will
	 *       be left in L2 MRU. This helps keep the chunks associated with
	 *       a particular FSAL readdir call including readahead from being
	 *       immediate candidates for reaping, thus keeping the readahead
	 *       from cannibalizing itself. Of course if the L2 queue is
	 *       empty due to activity, and the readahead is significant, it
	 *       is possible to cannibalize the chunks.
	 */
	lru_insert_chunk(chunk, &CHUNK_LRU[chunk->chunk_lru.lane].L2);

	return chunk;
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

	if (lru->qid != LRU_ENTRY_CLEANUP) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ(lru, q);

		/* in with the new */
		q = &qlane->cleanup;
		lru_insert(lru, q);
	}

	QUNLOCK(qlane);
}

/**
 * @brief Push an entry to the cleanup queue that may be unexported
 * for out-of-line cleanup
 *
 * This routine is used to try pushing a cache entry into the cleanup
 * queue. If the entry ends up with another LRU reference before this
 * is accomplished, then don't push it to cleanup.
 *
 * This will be used when unexporting an export. Any cache entry
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

	cih_latch_entry(&entry->fh_hk.key, &latch, __func__, __LINE__);
	QLOCK(qlane);

	/* Take the attr lock, so we can check that this entry is still
	 * not in any export */
	PTHREAD_RWLOCK_rdlock(&entry->attr_lock);

	/* Make sure that the entry is not reaped by the time this
	 * thread got the QLOCK
	 */
	if (glist_empty(&entry->export_list) &&
	    entry->lru.qid != LRU_ENTRY_NONE) {
		/* it worked */
		struct lru_q *q = lru_queue_of(entry);

		LRU_DQ(lru, q);
		entry->lru.qid = LRU_ENTRY_CLEANUP;
		atomic_set_uint32_t_bits(&entry->lru.flags,
					 LRU_CLEANUP);
		/* Note: we didn't take a ref here, so the only ref left
		 * in this callpath is the one owned by
		 * mdcache_unexport().  When it unref's, that may free
		 * this object; otherwise, it will be freed when the
		 * last op is done, as it's unreachable. */

		/* It's safe to drop the attr lock here, as we have the
		 * latch, so no one can look up the entry */
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		QUNLOCK(qlane);
		/* Drop the sentinel reference */
		cih_remove_latched(entry, &latch, CIH_REMOVE_NONE);
	} else {
		PTHREAD_RWLOCK_unlock(&entry->attr_lock);
		QUNLOCK(qlane);
	}

	cih_hash_release(&latch);
}

/**
 * @brief Function that executes in the lru thread to process one lane
 *
 * @param[in]     lane          The lane to process
 *
 * @returns the number of files worked on (workdone)
 *
 */

static inline int lru_run_lane(int lane)
{
	struct lru_q *q;
	/* The amount of work done on this lane on this pass. */
	size_t workdone = 0;
	/* Current queue lane */
	struct lru_q_lane *qlane = &LRU[lane];
	struct glist_head *glist, *glistn;

	q = &qlane->L1;

	LogDebug(COMPONENT_MDCACHE_LRU,
		 "Reaping up to %d entries from lane %d",
		 lru_state.per_lane_work, lane);

	/* ACTIVE */
	QLOCK(qlane);

	glist_for_each_safe(glist, glistn, &q->q) {
		/* The entry being examined */
		mdcache_lru_t *lru = NULL;
		/* a cache entry */
		mdcache_entry_t *entry;
		/* entry refcnt */
		uint32_t refcnt;

		/* check per-lane work */
		if (workdone >= lru_state.per_lane_work)
			break;

		lru = glist_entry(glist, mdcache_lru_t, q);

		/* get entry early.  This is safe without a ref, because we have
		 * the QLANE lock */
		entry = container_of(lru, mdcache_entry_t, lru);

		/* Get refcount of the entry now */
		refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
		const int32_t active_refcnt =
			atomic_fetch_int32_t(&entry->lru.active_refcnt);
		GSH_UNIQUE_AUTO_TRACEPOINT(mdcache, mdc_lru_ref, TRACE_DEBUG,
			"lru ref. handle: {}, sub handle: {}, refcnt: {}, active_refcnt: {}",
			&entry->obj_handle, entry->sub_handle, refcnt,
			active_refcnt);

		/* check refcnt in range */
		if (unlikely(refcnt == 1)) {
			struct lru_q *q;

			/* Move entry to MRU of L2 */
			q = &qlane->L1;
			LRU_DQ(lru, q);
			q = &qlane->L2;
			lru_insert(lru, q);
			++workdone;
		}
	} /* for_each_safe lru */

	QUNLOCK(qlane);
	LogDebug(COMPONENT_MDCACHE_LRU,
		 "Actually processed %zd entries on lane %d",
		 workdone, lane);

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
	/** @todo FSF - this could use some additional effort on finding a
	 *              new scheduling algorithm and tracking progress.
	 */

	/* Index */
	int lane = 0;
	time_t threadwait = mdcache_param.lru_run_interval;
	/* Total work done in all passes so far. */
	int totalwork = 0;
	static bool first_time = TRUE;
	time_t curr_time;

	if (first_time) {
		/* Wait for NFS server to properly initialize */
		nfs_init_wait();
		first_time = FALSE;
	}

	SetNameFunction("cache_lru");

	LogFullDebug(COMPONENT_MDCACHE_LRU, "LRU awakes.");

	LogFullDebug(COMPONENT_MDCACHE_LRU, "lru entries: %" PRIu64,
		     atomic_fetch_uint64_t(&lru_state.entries_used));

	curr_time = time(NULL);

	if ((curr_time >= lru_state.prev_time) &&
	    (curr_time - lru_state.prev_time < fridgethr_getwait(ctx)))
		threadwait = curr_time - lru_state.prev_time;

	/* Loop over all lanes to perform L1 to L2 demotion. Track the work
	 * done for logging.
	 */
	for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
		LogDebug(COMPONENT_MDCACHE_LRU,
			 "Demoting up to %d entries from lane %d",
			 lru_state.per_lane_work, lane);

		LogFullDebug(COMPONENT_MDCACHE_LRU,
			     "totalwork=%d", totalwork);

		totalwork += lru_run_lane(lane);
	}

	/* We're trying to release the entry cache if the amount
	 * used is higher than the water level. every time we can
	 * try best to release the number of entries until entries
	 * cache below the high water mark. the max number of entries
	 * released per time is entries_release_size.
	 */
	if (lru_state.entries_release_size > 0) {
		if (atomic_fetch_uint64_t(&lru_state.entries_used) >
		    lru_state.entries_hiwat) {
			size_t released = 0;

			LogFullDebug(COMPONENT_MDCACHE_LRU,
				"Entries used is %" PRIu64
				" and above water mark, LRU want release %d entries",
				atomic_fetch_uint64_t(&lru_state.entries_used),
				lru_state.entries_release_size);

			released = mdcache_lru_release_entries(
					lru_state.entries_release_size);
			LogFullDebug(COMPONENT_MDCACHE_LRU,
				"Actually release %zd entries", released);
		} else {
			LogFullDebug(COMPONENT_MDCACHE_LRU,
				"Entries used is %" PRIu64
				" and low water mark: not releasing",
				atomic_fetch_uint64_t(&lru_state.entries_used));
		}
	}

	if (atomic_fetch_uint64_t(&lru_state.entries_used) >
	    lru_state.entries_hiwat) {
		/* If we are still over the high water mark, try and reap
		 * sooner.
		 */
		threadwait = threadwait / 2;
	}

	fridgethr_setwait(ctx, threadwait);

	LogDebug(COMPONENT_MDCACHE_LRU,
		 "After work, count:%" PRIu64
		 " new_thread_wait=%" PRIu64,
		 atomic_fetch_uint64_t(&lru_state.entries_used),
		 ((uint64_t) threadwait));
	LogFullDebug(COMPONENT_MDCACHE_LRU,
		     "totalwork=%d lanes=%d",
		     totalwork, LRU_N_Q_LANES);
}

/**
 * @brief Function that executes in the lru thread to process one lane
 *
 * This function really just demotes chunks from L1 to L2, so very simple.
 *
 * @param[in]     lane          The lane to process
 *
 * @returns the number of chunks worked on (workdone)
 *
 */

static inline size_t chunk_lru_run_lane(size_t lane)
{
	struct lru_q *q;
	/* The amount of work done on this lane on this pass. */
	size_t workdone = 0;
	/* The lru object being examined */
	mdcache_lru_t *lru = NULL;
	/* Current queue lane */
	struct lru_q_lane *qlane = &CHUNK_LRU[lane];
	struct dir_chunk *chunk;
	uint32_t refcnt;
	struct glist_head *glist, *glistn;

	q = &qlane->L1;

	LogFullDebug(COMPONENT_MDCACHE_LRU,
		 "Reaping up to %d chunks from lane %zd",
		 lru_state.per_lane_work, lane);

	/* ACTIVE */
	QLOCK(qlane);

	glist_for_each_safe(glist, glistn, &q->q) {
		struct lru_q *q;

		/* check per-lane work */
		if (workdone >= lru_state.per_lane_work)
			break;

		lru = glist_entry(glist, mdcache_lru_t, q);

		chunk = container_of(lru, struct dir_chunk, chunk_lru);

		refcnt = atomic_fetch_int32_t(&chunk->chunk_lru.refcnt);
		assert(refcnt);
		if (unlikely(refcnt > LRU_SENTINEL_REFCOUNT)) {
			workdone++;
			continue;
		}

		/* Move lru object to MRU of L2 */
		q = &qlane->L1;
		CHUNK_LRU_DQ(lru, q);
		q = &qlane->L2;
		lru_insert(lru, q);
	} /* for_each_safe lru */

	QUNLOCK(qlane);
	LogFullDebug(COMPONENT_MDCACHE_LRU,
		 "Actually processed %zd chunks on lane %zd",
		 workdone, lane);

	return workdone;
}

/**
 * @brief Function that executes in the lru thread
 *
 * This function reorganizes the L1 and L2 queues, demoting least recently
 * used L1 chunks to L2.
 *
 * This function uses the lock discipline for functions accessing LRU
 * entries through a queue partition.
 *
 * @param[in] ctx Fridge context
 */

static void chunk_lru_run(struct fridgethr_context *ctx)
{
	/* Index */
	size_t lane;
	/* A ratio computed to adjust wait time based on how close to high
	 * water mark for number of chunks we are.
	 */
	float wait_ratio;
	/* The computed wait time. */
	time_t new_thread_wait;
	/* Total work done (number of chunks demoted) across all lanes. */
	size_t totalwork = 0;
	static bool first_time = true;
	size_t target_release = 0, actual_release = 0;

	if (first_time) {
		/* Wait for NFS server to properly initialize */
		nfs_init_wait();
		first_time = false;
	}

	SetNameFunction("chunk_lru");

	LogFullDebug(COMPONENT_MDCACHE_LRU,
		     "LRU awakes, lru chunks used: %" PRIu64,
		     lru_state.chunks_used);

	/* Total chunks demoted to L2 between all lanes and all current runs. */
	for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
		LogFullDebug(COMPONENT_MDCACHE_LRU,
			 "Reaping up to %d chunks from lane %zd totalwork=%zd",
			 lru_state.per_lane_work, lane, totalwork);

		totalwork += chunk_lru_run_lane(lane);
	}

	if (lru_state.chunks_used > lru_state.chunks_hiwat) {
		/* If chunks are over high water mark, target to reap 1% of the
		 * chunks in use.
		 */
		target_release += lru_state.chunks_used / 100;
	}

	if (atomic_fetch_uint64_t(&lru_state.entries_used) >
	    lru_state.entries_hiwat) {
		/* If the inode cache is over high water mark, target to reap an
		 * additional 1% of the chunks in use.
		 */
		target_release += lru_state.chunks_used / 100;
	}

	/* We may need to reap chunks */
	if (lru_state.chunks_used > lru_state.chunks_lowat) {
		/* If chunks are over low water mark, target to reap an
		 * additional 1% of the chunks in use. Minimum of 1.
		 */
		target_release += lru_state.chunks_used / 100;

		if (target_release == 0)
			target_release = 1;
	}

	while (actual_release < target_release) {
		mdcache_lru_t *lru = NULL;
		struct dir_chunk *chunk;

		lru = lru_reap_chunk_impl(LRU_ENTRY_L2, NULL);

		if (lru == NULL)
			lru = lru_reap_chunk_impl(LRU_ENTRY_L1, NULL);

		if (lru == NULL) {
			/* No more progress possible. */
			break;
		}

		actual_release++;

		/* we uniquely hold chunk, it has already been cleaned up.
		 * The dirents list is effectively properly initialized.
		 */
		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		LogFullDebug(COMPONENT_MDCACHE,
			     "Releasing chunk at %p.", chunk);
		mdcache_lru_unref_chunk(chunk);
	}

	/* Run more frequently the closer to max number of chunks we are. */
	wait_ratio = 1.0 - (lru_state.chunks_used / lru_state.chunks_hiwat);

	if (wait_ratio < 0.1) {
		/* wait_ratio could even be negative if chunks_used is greater
		 * than chunks_hiwat. Never have an interval shorter than 10%
		 * of the lru_run_interval.
		 */
		wait_ratio = 0.1;
	}

	if (actual_release < (target_release / 2)) {
		/* We wanted to release chunks and did not release enough. */
		wait_ratio = wait_ratio / 2;
	}

	new_thread_wait = mdcache_param.lru_run_interval * wait_ratio;

	/* if new_thread_wait is 0, chunk_lru_run would not be scheduled (just
	 * in case the lru_run_interval is really small we want to make sure to
	 * run at least every second).
	 */
	if (new_thread_wait == 0)
		new_thread_wait = 1;

	fridgethr_setwait(ctx, new_thread_wait);

	LogDebug(COMPONENT_MDCACHE_LRU,
		 "After work, threadwait=%" PRIu64
		 " totalwork=%zd target_release = %zd actual_release = %zd",
		 ((uint64_t) new_thread_wait), totalwork,
		 target_release, actual_release);
}

/* @brief Release reapable entries until we are below the high-water mark
 *
 * If something refs a lot of entries at the same time, this can put the number
 * of entries above the high water mark. Every time we want try best to release
 * the number of entries, the max number is want_release.
 *
 * Normally, want_release equals Entries_Relesase_Size, If it is set to -1
 * or negative, this going to be a big hammer, that will clean up anything it
 * can until either it can't anymore, or we're back below the high water mark.
 *
 * @param[in] want_release Maximum number of entries released. Note that if set
 *	      negative number, it indicates release all until can't release
 * @return Return the number of really released
 */
size_t mdcache_lru_release_entries(int32_t want_release)
{
	mdcache_lru_t *lru;
	mdcache_entry_t *entry = NULL;
	size_t released = 0;

	/*release nothing*/
	if (want_release == 0)
		return released;

	while ((lru = lru_try_reap_entry(LRU_TEMP_REF))) {
		entry = container_of(lru, mdcache_entry_t, lru);
		/* Release the reference taken by lru_try_reap_entry. The
		 * entry has already been unhashed and the sentinel reference
		 * released.
		 */
		mdcache_lru_unref(entry, LRU_TEMP_REF);
		++released;

		if (want_release > 0 && released >= want_release)
			break;
	}

	return released;
}

/* Public functions */

void init_fds_limit(void)
{
	struct fd_lru_parameter fd_lru_parameter;

	fd_lru_parameter.lru_run_interval = mdcache_param.lru_run_interval;
	fd_lru_parameter.Cache_FDs = mdcache_param.Cache_FDs;
	fd_lru_parameter.fd_limit_percent = mdcache_param.fd_limit_percent;
	fd_lru_parameter.fd_hwmark_percent = mdcache_param.fd_hwmark_percent;
	fd_lru_parameter.fd_lwmark_percent = mdcache_param.fd_lwmark_percent;
	fd_lru_parameter.reaper_work = mdcache_param.reaper_work;
	fd_lru_parameter.reaper_work_per_lane =
					mdcache_param.reaper_work_per_lane;
	fd_lru_parameter.biggest_window = mdcache_param.biggest_window;
	fd_lru_parameter.required_progress = mdcache_param.required_progress;
	fd_lru_parameter.futility_count = mdcache_param.futility_count;
	fd_lru_parameter.fd_fallback_limit = FD_FALLBACK_LIMIT;

	fsal_init_fds_limit(&fd_lru_parameter);
}

/**
 * Initialize subsystem
 */
fsal_status_t
mdcache_lru_pkginit(void)
{
	/* Return code from system calls */
	int code = 0;
	struct fridgethr_params frp;
	fsal_status_t status;
	struct fd_lru_parameter fd_lru_parameter;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 2;
	frp.thr_min = 2;
	frp.thread_delay = mdcache_param.lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	if (mdcache_param.reaper_work) {
		/* Backwards compatibility */
		lru_state.per_lane_work = (mdcache_param.reaper_work +
					   LRU_N_Q_LANES - 1) / LRU_N_Q_LANES;
	} else {
		/* New parameter */
		lru_state.per_lane_work = mdcache_param.reaper_work_per_lane;
	}

	/* Set high watermark for cache entries. */
	lru_state.entries_hiwat = mdcache_param.entries_hwmark;
	lru_state.entries_used = 0;

	/* set lru release entries size */
	lru_state.entries_release_size = mdcache_param.entries_release_size;

	/* Set high and low watermark for chunks. */
	lru_state.chunks_hiwat = mdcache_param.chunks_hwmark;
	lru_state.chunks_lowat = mdcache_param.chunks_lwmark;
	lru_state.chunks_used = 0;

	/* init queue complex */
	lru_init_queues();

	/* spawn LRU background thread */
	code = fridgethr_init(&lru_fridge, "LRU_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Unable to initialize LRU fridge, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	code = fridgethr_submit(lru_fridge, lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Unable to start Entry LRU thread, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	code = fridgethr_submit(lru_fridge, chunk_lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Unable to start Chunk LRU thread, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	fd_lru_parameter.lru_run_interval = mdcache_param.lru_run_interval;
	fd_lru_parameter.Cache_FDs = mdcache_param.Cache_FDs;
	fd_lru_parameter.fd_limit_percent = mdcache_param.fd_limit_percent;
	fd_lru_parameter.fd_hwmark_percent = mdcache_param.fd_hwmark_percent;
	fd_lru_parameter.fd_lwmark_percent = mdcache_param.fd_lwmark_percent;
	fd_lru_parameter.reaper_work = mdcache_param.reaper_work;
	fd_lru_parameter.reaper_work_per_lane =
					mdcache_param.reaper_work_per_lane;
	fd_lru_parameter.biggest_window = mdcache_param.biggest_window;
	fd_lru_parameter.required_progress = mdcache_param.required_progress;
	fd_lru_parameter.futility_count = mdcache_param.futility_count;
	fd_lru_parameter.fd_fallback_limit = FD_FALLBACK_LIMIT;

	status = fd_lru_pkginit(&fd_lru_parameter);

	return status;
}

/**
 * Shutdown subsystem
 *
 * @return 0 on success, POSIX errors on failure.
 */
fsal_status_t
mdcache_lru_pkgshutdown(void)
{
	fsal_status_t status;
	int rc = fridgethr_sync_command(lru_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(lru_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Failed shutting down LRU thread: %d", rc);
	}

	if (rc == 0)
		status = fd_lru_pkgshutdown();
	else
		status = fsalstat(posix2fsal_error(rc), rc);

	lru_destroy_queues();

	return status;
}

static inline void init_rw_locks(mdcache_entry_t *entry)
{
	/* Initialize the entry locks */
	PTHREAD_RWLOCK_init(&entry->attr_lock, NULL);
	PTHREAD_RWLOCK_init(&entry->content_lock, NULL);
}

mdcache_entry_t *alloc_cache_entry(void)
{
	mdcache_entry_t *nentry;

	nentry = pool_alloc(mdcache_entry_pool);

	/* Initialize the entry locks */
	init_rw_locks(nentry);

	(void) atomic_inc_int64_t(&lru_state.entries_used);

	return nentry;
}

/**
 * @brief Re-use or allocate an entry
 *
 * This function repurposes a resident entry in the LRU system if the system is
 * above the high-water mark, and allocates a new one otherwise.  On success,
 * this function always returns an entry with two references (one for the
 * sentinel and an active one for the caller's use).
 *
 * The caller MUST call mdcache_lru_insert with the same flags when the entry is
 * sufficiently constructed.
 *
 * @param[in] sub_handle  The underlying FSAL's fsal_obj_handle
 * @param[in] flags       The flags for the caller's initial reference, MUST
 *                        include LRU_ACTIVE_REF
 *
 * @return a usable entry or NULL if unexport is in progress.
 */
mdcache_entry_t *mdcache_lru_get(struct fsal_obj_handle *sub_handle,
				 uint32_t flags)
{
	mdcache_lru_t *lru;
	mdcache_entry_t *nentry = NULL;

	assert(flags & LRU_ACTIVE_REF);

	lru = lru_try_reap_entry(LRU_TEMP_REF);
	if (lru) {
		/* we uniquely hold entry with a temp ref that we will
		 * discard (with no negative consequence) below when we remake
		 * the entry.
		 */
		nentry = container_of(lru, mdcache_entry_t, lru);
		mdcache_lru_clean(nentry);
		memset(&nentry->attrs, 0, sizeof(nentry->attrs));
		init_rw_locks(nentry);
	} else {
		/* alloc entry (if fails, aborts) */
		nentry = alloc_cache_entry();
	}

	nentry->attr_generation = 0;

	/* Since the entry isn't in a queue, nobody can bump refcnt. Set both
	 * the sentinel reference and the active reference. The caller is
	 * responsible for inserting the entry into the LRU queue.
	 */
	nentry->lru.refcnt = 2;
	nentry->lru.active_refcnt = 1;
	nentry->lru.cf = 0;
	nentry->lru.lane = lru_lane_of(nentry);
	nentry->lru.flags = LRU_SENTINEL_HELD;
	nentry->sub_handle = sub_handle;

	if (flags & LRU_PROMOTE) {
		/* If entry is ever promoted, remember that. */
		nentry->lru.flags |= LRU_EVER_PROMOTED;
	}

	GSH_AUTO_TRACEPOINT(mdcache, mdc_lru_get, TRACE_DEBUG,
		"lru unref. handle: {}, sub handle: {}, refcnt: {}",
		&nentry->obj_handle, sub_handle,
		nentry->lru.refcnt);
	return nentry;
}

/**
 * @brief Insert a new entry into the LRU in the ACTIVE queue.
 *
 * @param [in] entry  Entry to insert.
 */
void mdcache_lru_insert_active(mdcache_entry_t *entry)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];

	QLOCK(qlane);

	/* Enqueue. */
	lru_insert(lru, &LRU[entry->lru.lane].ACTIVE);

	QUNLOCK(qlane);
}

/**
 * @brief Get a reference
 *
 * This function acquires a reference on the given cache entry.
 *
 * @param[in] entry  The entry on which to get a reference
 * @param[in] flags  One of LRU_PROMOTE, LRU_FLAG_NONE, or LRU_ACTIVE_REF
 *
 * A flags value of LRU_PROMOTE indicates an initial
 * reference.  A non-initial reference is an "extra" reference in some call
 * path, hence does not influence LRU, and is lockless.
 *
 * A flags value of LRU_PROMOTE indicates an ordinary initial reference,
 * and strongly influences LRU.  Essentially, the first ref during a callpath
 * should take an LRU_PROMOTE ref, and all subsequent callpaths should take
 * LRU_FLAG_NONE refs.
 */
void _mdcache_lru_ref(mdcache_entry_t *entry, uint32_t flags, const char *func,
		      int line)
{
	int32_t refcnt, active_refcnt;

	/* Always take a normal reference so unref to 0 works right */
	refcnt = atomic_inc_int32_t(&entry->lru.refcnt);

	if (flags & LRU_ACTIVE_REF) {
		/* Each active reference is in addition to a normal
		 * reference. This allows the possibility of the final reference
		 * to an entry being a active reference such that when that
		 * active reference is dropped, cleanup will occur.
		 */
		active_refcnt = atomic_inc_int32_t(&entry->lru.active_refcnt);
	}

	GSH_UNIQUE_AUTO_TRACEPOINT(mdcache, mdc_lru_ref, TRACE_DEBUG,
			"lru ref. handle: {}, sub handle: {}, refcnt: {}, active_refcnt: {}",
			&entry->obj_handle, entry->sub_handle, refcnt,
			active_refcnt);

	if (flags & LRU_PROMOTE) {
		/* If entry is ever promoted, remember that. */
		atomic_set_uint32_t_bits(&entry->lru.flags, LRU_EVER_PROMOTED);
		assert(flags & LRU_ACTIVE_REF);
	}

	if (flags & LRU_ACTIVE_REF) {
		/* Move into ACTIVE queue or adjust to MRU */
		make_active_lru(entry);
	}
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
 *                   NOTE: LRU_PROMOTE is ignored so safe to be set
 * @return true if entry freed, false otherwise
 */
bool
_mdcache_lru_unref(mdcache_entry_t *entry, uint32_t flags, const char *func,
		   int line)
{
	bool do_cleanup = false;
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	bool other_lock_held = entry->fsobj.hdl.no_cleanup;
	bool freed = false;

	if (!other_lock_held) {
		/* pre-check about qid to avoid LOCK every time */
		if (entry->lru.qid == LRU_ENTRY_CLEANUP) {
			QLOCK(qlane);
			/* Locked, check again with lock */
			if (((atomic_fetch_uint32_t(&entry->lru.flags)
			      & LRU_CLEANED) == 0) &&
			    (entry->lru.qid == LRU_ENTRY_CLEANUP)) {
				do_cleanup = true;
				atomic_set_uint32_t_bits(&entry->lru.flags,
							 LRU_CLEANED);
			}
			QUNLOCK(qlane);
		}

		if (do_cleanup) {
			LogDebug(COMPONENT_MDCACHE,
				 "LRU_ENTRY_CLEANUP of entry %p",
				 entry);
			state_wipe_file(&entry->obj_handle);
		}
	}

	if (flags & LRU_FLAG_SENTINEL) {
		/* Caller is intending to release the sentinel reference */
		if ((atomic_fetch_uint32_t(&entry->lru.flags)
		     & LRU_SENTINEL_HELD) == 0) {
			/* oops... */
			LogFatal(COMPONENT_MDCACHE,
				 "Sentinel reference already released");
		}

		atomic_clear_uint32_t_bits(&entry->lru.flags,
					   LRU_SENTINEL_HELD);
	}

	const int32_t refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);
	const int32_t active_refcnt =
		atomic_fetch_int32_t(&entry->lru.active_refcnt);
	GSH_AUTO_TRACEPOINT(mdcache, mdc_lru_ref, TRACE_DEBUG,
		"lru unref. handle: {}, sub handle: {}, refcnt: {}, active_refcnt: {}",
		&entry->obj_handle, entry->sub_handle, refcnt, active_refcnt);

	/* Each active reference is in addition to a normal reference. This
	 * allows the possibility of the final reference to an entry being an
	 * active reference such that when that active reference is dropped,
	 * cleanup will occur.
	 */

	/* Handle active unref first */
	if (flags & LRU_ACTIVE_REF &&
	    PTHREAD_MUTEX_dec_int32_t_and_lock(&entry->lru.active_refcnt,
					       &qlane->ql_mtx)) {
		/* active_refcnt is zero and we hold the QLOCK. */
#if 0
		/* For clarity... */
		QLOCK(qlane);
#endif
		/* Move entry to MRU of L1 or L2 or leave in cleanup queue. */
		make_inactive_lru(entry);

		QUNLOCK(qlane);
	}

	/* Handle normal unref next for all unrefs. */
	if (PTHREAD_MUTEX_dec_int32_t_and_lock(&entry->lru.refcnt,
					       &qlane->ql_mtx)) {
		struct lru_q *q;

		/* refcnt is zero and we hold the QLOCK. */
#if 0
		/* For clarity... */
		QLOCK(qlane);
#endif
		/*
		 * The cih table holds a non-weak reference, so entry should no
		 * longer be in it.
		 */
		assert(!entry->fh_hk.inavl);

		/* Remove entry and mark it as dead. */
		q = lru_queue_of(entry);

		if (q) {
			/* as of now, entries leaving the cleanup queue
			 * are LRU_ENTRY_NONE */
			LRU_DQ(&entry->lru, q);
		}

		QUNLOCK(qlane);

		mdcache_lru_clean(entry);
		pool_free(mdcache_entry_pool, entry);
		freed = true;

		(void) atomic_dec_int64_t(&lru_state.entries_used);
	}

	return freed;
}

/**
 * @brief Remove a chunk from LRU, and clean it
 *
 * QLane must be locked
 *
 * @param[in] chunk  The chunk to be removed from LRU
 */
static void lru_clean_chunk(struct dir_chunk *chunk)
{
	struct lru_q *lq;

	LogFullDebug(COMPONENT_MDCACHE, "Removing chunk %p", chunk);

	/* Remove chunk and mark it as dead. */
	lq = chunk_lru_queue_of(chunk);

	if (lq) {
		/* dequeue the chunk */
		CHUNK_LRU_DQ(&chunk->chunk_lru, lq);
	}

	(void) atomic_dec_int64_t(&lru_state.chunks_used);

	/* Then do the actual cleaning work. */
	mdcache_clean_dirent_chunk(chunk);
}

void _mdcache_lru_ref_chunk(struct dir_chunk *chunk, const char *func, int line)
{
	atomic_inc_int32_t(&chunk->chunk_lru.refcnt);
}

/**
 * @brief Unref a dirent chunk
 * Should be called with content_lock held in write mode.
 * @param [in] chunk	The chunk to unref
 */
void _mdcache_lru_unref_chunk(struct dir_chunk *chunk, const char *func,
			      int line)
{
	int refcnt;
	uint32_t lane;
	struct lru_q_lane *qlane;

	if (!chunk)
		return;

	lane = chunk->chunk_lru.lane;
	qlane = &CHUNK_LRU[lane];
	QLOCK(qlane);

	refcnt = atomic_dec_int32_t(&chunk->chunk_lru.refcnt);

	assert(refcnt >= 0);

	if (refcnt == 0) {
		lru_clean_chunk(chunk);

		/* And now we can free the chunk. */
		LogFullDebug(COMPONENT_MDCACHE, "Freeing chunk %p", chunk);
		gsh_free(chunk);
	}
	QUNLOCK(qlane);
}

/**
 * @brief Indicate that a chunk is being used, bump it up in the LRU
 *
 */
void lru_bump_chunk(struct dir_chunk *chunk)
{
	mdcache_lru_t *lru = &chunk->chunk_lru;
	struct lru_q_lane *qlane = &CHUNK_LRU[lru->lane];
	struct lru_q *q;

	QLOCK(qlane);
	q = chunk_lru_queue_of(chunk);

	switch (lru->qid) {
	case LRU_ENTRY_L1:
		/* advance chunk to MRU (of L1) */
		CHUNK_LRU_DQ(lru, q);
		lru_insert(lru, q);
		break;
	case LRU_ENTRY_L2:
		/* move chunk to MRU of L1 */
		CHUNK_LRU_DQ(lru, q);
		q = &qlane->L1;
		lru_insert(lru, q);
		break;
	default:
		/* do nothing */
		break;
	}

	QUNLOCK(qlane);
}

static inline void mdc_lru_dirmap_add(struct mdcache_fsal_export *exp,
				      mdcache_dmap_entry_t *dmap)
{
	avltree_insert(&dmap->node, &exp->dirent_map.map);
	/* MRU is the tail; Mdd to MRU of list */
	glist_add_tail(&exp->dirent_map.lru, &dmap->lru_entry);
	exp->dirent_map.count++;
}

static inline void mdc_lru_dirmap_del(struct mdcache_fsal_export *exp,
				      mdcache_dmap_entry_t *dmap)
{
	glist_del(&dmap->lru_entry);
	avltree_remove(&dmap->node, &exp->dirent_map.map);
	exp->dirent_map.count--;
}

/**
 * @brief Add a dirent to the dirmap
 *
 * Add this dirent to the dirmap.  The dirmap is a mapping of cookies to names
 * that allows whence-is-name to restart where it left off if the chunk was
 * reaped, instead of reloading the whole directory to find the cookie.
 *
 * @param[in] dirent	Dirent to add
 */
void mdc_lru_map_dirent(mdcache_dir_entry_t *dirent)
{
	struct mdcache_fsal_export *exp = mdc_cur_export();
	mdcache_dmap_entry_t key, *dmap;
	struct avltree_node *node;

	PTHREAD_MUTEX_lock(&exp->dirent_map.dm_mtx);

	key.ck = dirent->ck;
	node = avltree_lookup(&key.node, &exp->dirent_map.map);
	if (node) {
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Already map for %s -> %" PRIx64,
			     dirent->name, dirent->ck);
		/* Move to MRU */
		dmap = avltree_container_of(node, mdcache_dmap_entry_t, node);
		now(&dmap->timestamp);
		glist_move_tail(&exp->dirent_map.lru, &dmap->lru_entry);
		PTHREAD_MUTEX_unlock(&exp->dirent_map.dm_mtx);
		return;
	}

	if (exp->dirent_map.count > mdcache_param.dirmap_hwmark) {
		/* LRU end is the head; grab the LRU entry */
		dmap = glist_first_entry(&exp->dirent_map.lru,
					 mdcache_dmap_entry_t, lru_entry);
		mdc_lru_dirmap_del(exp, dmap);
		/* Free name */
		gsh_free(dmap->name);
	} else {
		dmap = gsh_malloc(sizeof(*dmap));
	}

	dmap->ck = dirent->ck;
	dmap->name = gsh_strdup(dirent->name);
	now(&dmap->timestamp);
	LogFullDebug(COMPONENT_NFS_READDIR, "Mapping %s -> %" PRIx64 " %p:%d",
		     dmap->name, dmap->ck, exp, exp->dirent_map.count);

	mdc_lru_dirmap_add(exp, dmap);

	PTHREAD_MUTEX_unlock(&exp->dirent_map.dm_mtx);
}

/**
 * @brief Look up and remove an entry from the dirmap
 *
 * This looks up the cookie in the dirmap, and returns the associated name, if
 * it's in the cache.  The entry is removed from the cache and freed, and the
 * name is returned.
 *
 * @note the returned name must be freed by the caller
 *
 * @param[in] ck	Cookie to look up
 * @return Name, if found, or NULL otherwise
 */
fsal_cookie_t *mdc_lru_unmap_dirent(uint64_t ck)
{
	struct mdcache_fsal_export *exp = mdc_cur_export();
	struct avltree_node *node;
	mdcache_dmap_entry_t key, *dmap;
	char *name;

	PTHREAD_MUTEX_lock(&exp->dirent_map.dm_mtx);

	key.ck = ck;
	node = avltree_lookup(&key.node, &exp->dirent_map.map);
	if (!node) {
		LogFullDebug(COMPONENT_NFS_READDIR, "No map for %" PRIx64, ck);
		PTHREAD_MUTEX_unlock(&exp->dirent_map.dm_mtx);
		return NULL;
	}

	dmap = avltree_container_of(node, mdcache_dmap_entry_t, node);
	mdc_lru_dirmap_del(exp, dmap);

	PTHREAD_MUTEX_unlock(&exp->dirent_map.dm_mtx);

	name = dmap->name;

	LogFullDebug(COMPONENT_NFS_READDIR, "Unmapping %s -> %" PRIx64,
		     dmap->name, dmap->ck);

	/* Don't free name, we're passing it back to the caller */
	gsh_free(dmap);

	return (fsal_cookie_t *)name;
}

#define DIRMAP_MAX_PER_SCAN 1000
#define DIRMAP_KEEP_NS (60 * NS_PER_SEC)

static void dirmap_lru_run(struct fridgethr_context *ctx)
{
	struct mdcache_fsal_export *exp = ctx->arg;
	mdcache_dmap_entry_t *cur, *next;
	int i;
	struct timespec curtime;
	nsecs_elapsed_t age;
	static bool first_time = true;

	/* XXX dang this needs to be here or this will hijack another thread,
	 * causing that one to never run again. */
	if (first_time) {
		/* Wait for NFS server to properly initialize */
		nfs_init_wait();
		first_time = false;
	}

	PTHREAD_MUTEX_lock(&exp->dirent_map.dm_mtx);

	now(&curtime);

	cur = glist_last_entry(&exp->dirent_map.lru, mdcache_dmap_entry_t,
			       lru_entry);
	for (i = 0; i < DIRMAP_MAX_PER_SCAN && cur != NULL; ++i) {
		next = glist_prev_entry(&exp->dirent_map.lru,
					mdcache_dmap_entry_t,
					lru_entry, &cur->lru_entry);
		age = timespec_diff(&cur->timestamp, &curtime);
		if (age < DIRMAP_KEEP_NS) {
			/* LRU is in timestamp order; done */
			goto out;
		}
		mdc_lru_dirmap_del(exp, cur);
		gsh_free(cur->name);
		gsh_free(cur);
		cur = next;
	}

out:
	PTHREAD_MUTEX_unlock(&exp->dirent_map.dm_mtx);
	fridgethr_setwait(ctx, mdcache_param.lru_run_interval);
}


fsal_status_t dirmap_lru_init(struct mdcache_fsal_export *exp)
{
	struct fridgethr_params frp;
	int rc;

	if (!exp->mfe_exp.exp_ops.fs_supports(&exp->mfe_exp,
					      fso_whence_is_name)) {
		LogDebug(COMPONENT_NFS_READDIR, "Skipping dirmap %s",
			 exp->name);
		return fsalstat(0, 0);
	}

	avltree_init(&exp->dirent_map.map, avl_dmap_ck_cmpf, 0 /* flags */);
	glist_init(&exp->dirent_map.lru);

	PTHREAD_MUTEX_init(&exp->dirent_map.dm_mtx, NULL);

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = mdcache_param.lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	rc = fridgethr_init(&exp->dirmap_fridge, exp->name, &frp);
	if (rc != 0) {
		LogMajor(COMPONENT_NFS_READDIR,
			 "Unable to initialize %s dirmap fridge, error code %d.",
			 exp->name, rc);
		return posix2fsal_status(rc);
	}

	rc = fridgethr_submit(exp->dirmap_fridge, dirmap_lru_run, exp);
	if (rc != 0) {
		LogMajor(COMPONENT_NFS_READDIR,
			 "Unable to start %s dirmap thread, error code %d.",
			 exp->name, rc);
		return posix2fsal_status(rc);
	}

	LogDebug(COMPONENT_NFS_READDIR, "started dirmap %s", exp->name);

	return fsalstat(0, 0);
}

void dirmap_lru_stop(struct mdcache_fsal_export *exp)
{
	if (!exp->dirmap_fridge) {
		/* Wasn't running */
		return;
	}

	int rc = fridgethr_sync_command(exp->dirmap_fridge,
					fridgethr_comm_stop,
					10);

	if (rc == ETIMEDOUT) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(exp->dirmap_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_NFS_READDIR,
			 "Failed shutting down LRU thread: %d", rc);
	}

	fridgethr_destroy(exp->dirmap_fridge);

	LogDebug(COMPONENT_NFS_READDIR, "stopped dirmap %s", exp->name);
}

/** @} */
