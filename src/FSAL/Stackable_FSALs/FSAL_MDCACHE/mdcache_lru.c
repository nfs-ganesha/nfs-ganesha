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
#include "gsh_intrinsic.h"
#include "sal_functions.h"
#include "nfs_exports.h"
#ifdef USE_LTTNG
#include "gsh_lttng/mdcache.h"
#endif

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
 * A single queue lane, holding all entries.
 */

struct lru_q_lane {
	struct lru_q L1;
	struct lru_q L2;
	struct lru_q cleanup;	/* deferred cleanup */
	pthread_mutex_t mtx;
	/* LRU thread scan position */
	struct {
		bool active;
		struct glist_head *glist;
		struct glist_head *glistn;
	} iter;
	 CACHE_PAD(0);
};

/* The queue lock and the partition lock interact.  The partition lock must
 * always be taken before the queue lock to avoid deadlock */
#ifdef USE_LTTNG
#define QLOCK(qlane) \
	do { \
		PTHREAD_MUTEX_lock(&(qlane)->mtx); \
		tracepoint(mdcache, qlock, __func__, __LINE__, qlane); \
	} while (0)

#define QUNLOCK(qlane) \
	do { \
		PTHREAD_MUTEX_unlock(&(qlane)->mtx); \
		tracepoint(mdcache, qunlock, __func__, __LINE__, qlane); \
	} while (0)
#else
#define QLOCK(qlane) \
	PTHREAD_MUTEX_lock(&(qlane)->mtx)
#define QUNLOCK(qlane) \
	PTHREAD_MUTEX_unlock(&(qlane)->mtx)
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

#define CHUNK_LRU_DQ_SAFE(lru, qq) \
	do { \
		if ((lru)->qid == LRU_ENTRY_L1) { \
			struct lru_q_lane *qlane = &CHUNK_LRU[(lru)->lane]; \
			if (unlikely((qlane->iter.active) && \
				     ((&(lru)->q) == qlane->iter.glistn))) { \
				qlane->iter.glistn = (lru)->q.next; \
			} \
		} \
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
		PTHREAD_MUTEX_init(&qlane->mtx, NULL);

		/* init iterator */
		qlane->iter.active = false;

		/* init lane queues */
		lru_init_queue(&LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&LRU[ix].cleanup, LRU_ENTRY_CLEANUP);

		/* Initialize dir_chunk LRU */
		qlane = &CHUNK_LRU[ix];

		/* one mutex per lane */
		PTHREAD_MUTEX_init(&qlane->mtx, NULL);

		/* init iterator */
		qlane->iter.active = false;

		/* init lane queues */
		lru_init_queue(&CHUNK_LRU[ix].L1, LRU_ENTRY_L1);
		lru_init_queue(&CHUNK_LRU[ix].L2, LRU_ENTRY_L2);
		lru_init_queue(&CHUNK_LRU[ix].cleanup, LRU_ENTRY_CLEANUP);
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
 * @param[in] edge   One of LRU_LRU or LRU_MRU
 */
static inline void
lru_insert_chunk(struct dir_chunk *chunk, struct lru_q *q, enum lru_edge edge)
{
	mdcache_lru_t *lru = &chunk->chunk_lru;
	struct lru_q_lane *qlane = &CHUNK_LRU[lru->lane];

	QLOCK(qlane);

	lru_insert(lru, q, edge);

	QUNLOCK(qlane);
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
		struct root_op_context ctx;
		struct req_op_context *saved_ctx = op_ctx;
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
				LogFatal(COMPONENT_CACHE_INODE,
					 "An entry (%p) having an unmappable export_id (%"
					 PRIi32") is unexpected",
					 entry, export_id);
			}

			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Creating a new context with export id%"
				     PRIi32,
				     export_id);

			init_root_op_context(&ctx, export, export->fsal_export,
					     0, 0, UNKNOWN_REQUEST);
		} else {
			/* We MUST have a valid op_ctx based on the conditions
			 * we could get here. first_export_id coild be -1 or it
			 * could match the current op_ctx export. In either case
			 * we will trust the current op_ctx.
			 */
			assert(op_ctx);
			assert(op_ctx->ctx_export);
			LogFullDebug(COMPONENT_CACHE_INODE,
				     "Trusting op_ctx export id %"PRIu16,
				     op_ctx->ctx_export->export_id);
		}

		/* Make sure any FSAL global file descriptor is closed.
		 * Don't bother with the content_lock since we have exclusive
		 * ownership of this entry.
		 */
		status = fsal_close(&entry->obj_handle);

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_CACHE_INODE_LRU,
				"Error closing file in cleanup: %s",
				fsal_err_txt(status));
		}

		subcall(
			entry->sub_handle->obj_ops->release(entry->sub_handle)
		       );
		entry->sub_handle = NULL;

		if (op_ctx != saved_ctx) {
			/* We had to use our own op_ctx, clean it up and revert
			 * to the saved op_ctx.
			 */
			put_gsh_export(op_ctx->ctx_export);
			op_ctx = saved_ctx;
		}
	}

	/* Done with the attrs */
	fsal_release_attrs(&entry->attrs);

	/* Clean out the export mapping before deconstruction */
	mdc_clean_entry(entry);

	/* Clean our handle */
	fsal_obj_handle_fini(&entry->obj_handle);

	/* Finalize last bits of the cache entry, delete the key if any and
	 * destroy the rw locks.
	 */
	mdcache_key_delete(&entry->fh_hk.key);
	PTHREAD_RWLOCK_destroy(&entry->content_lock);
	PTHREAD_RWLOCK_destroy(&entry->attr_lock);

	state_hdl_cleanup(entry->obj_handle.state_hdl);

	if (entry->obj_handle.type == DIRECTORY)
		pthread_spin_destroy(&entry->fsobj.fsdir.spin);
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
		if (!lru) {
			QUNLOCK(qlane);
			continue;
		}
		refcnt = atomic_inc_int32_t(&lru->refcnt);
		entry = container_of(lru, mdcache_entry_t, lru);
#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_lru_ref,
		   __func__, __LINE__, &entry->obj_handle, entry->sub_handle,
		   refcnt);
#endif
		QUNLOCK(qlane);

		if (unlikely(refcnt != (LRU_SENTINEL_REFCOUNT + 1))) {
			/* cant use it. */
			mdcache_put(entry);
			continue;
		}
		/* potentially reclaimable */
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

#ifdef USE_LTTNG
				tracepoint(mdcache, mdc_lru_reap, __func__,
					   __LINE__, &entry->obj_handle,
					   entry->lru.refcnt);
#endif
				LRU_DQ_SAFE(lru, q);
				entry->lru.qid = LRU_ENTRY_NONE;
				QUNLOCK(qlane);
				cih_remove_latched(entry, &latch,
						   CIH_REMOVE_UNLOCK);
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
			QUNLOCK(qlane);
			/* return the ref we took above--unref deals
			 * correctly with reclaim case */
			mdcache_lru_unref(entry);
		} else {
			/* ! QLOCKED but needs to be Unref'ed */
			mdcache_lru_unref(entry);
			continue;
		}
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

		refcnt = atomic_inc_int32_t(&lru->refcnt);

		/* Get the chunk and parent entry that owns the chunk, all of
		 * this is valid because we hold the QLANE lock, the chunk was
		 * in the LRU, and thus the chunk is not yet being destroyed,
		 * and thus the parent entry must still also be valid.
		 */
		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		entry = chunk->parent;

		if (refcnt != (LRU_SENTINEL_REFCOUNT + 1)) {
			/* We can't reap a chunk with a ref */
			QUNLOCK(qlane);
			mdcache_lru_unref_chunk(chunk);
			continue;
		}

		/* If this chunk belongs to the parent seeking another chunk,
		 * or if we can get the content_lock for the chunk's parent,
		 * we can reap this chunk.
		 */
		if (entry == parent ||
		    pthread_rwlock_trywrlock(&entry->content_lock) == 0) {
			/* This chunk is eligible for reaping, we can proceed.
			 */
			if (entry != parent) {
				/* We need an LRU ref on parent entry to protect
				 * it while we do work on it's chunk.
				 */
				(void) atomic_inc_int32_t(&entry->lru.refcnt);
			}

			/* Dequeue the chunk so it won't show up anymore */
			CHUNK_LRU_DQ_SAFE(lru, lq);
			chunk->chunk_lru.qid = LRU_ENTRY_NONE;

			/* Drop the lane lock, we can now safely clean up the
			 * chunk. We hold the content_lock on the parent of
			 * the chunk (even if the chunk belonged to the
			 * directory a new chunk is requested for).
			 */
			QUNLOCK(qlane);

#ifdef USE_LTTNG
				tracepoint(mdcache, mdc_lru_reap_chunk,
					   __func__, __LINE__,
					   &entry->obj_handle, chunk);
#endif

			/* Clean the chunk out and indicate the directory is no
			 * longer completely populated.
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
				mdcache_put(entry);
			}
			mdcache_lru_unref_chunk(chunk);
			return lru;
		}

		/* Couldn't get the content_lock, the parent is busy
		 * doing something with dirents... This chunk is not
		 * eligible for reaping. Try the next lane...
		 */
		QUNLOCK(qlane);
		mdcache_lru_unref_chunk(chunk);
	}			/* foreach lane */

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
 * @return reused or allocated chunk
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

	if (prev_chunk)
		mdcache_lru_unref_chunk(prev_chunk);

	if (lru) {
		/* we uniquely hold chunk, it has already been cleaned up.
		 * The dirents list is effectively properly initialized.
		 */
		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		LogFullDebug(COMPONENT_CACHE_INODE,
			     "Recycling chunk at %p.", chunk);
	} else {
		/* alloc chunk (if fails, aborts) */
		chunk = gsh_calloc(1, sizeof(struct dir_chunk));
		glist_init(&chunk->dirents);
		LogFullDebug(COMPONENT_CACHE_INODE,
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
	} else {
		chunk->reload_ck = whence;
	}

	chunk->chunk_lru.refcnt = 1;
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
	lru_insert_chunk(chunk, &CHUNK_LRU[chunk->chunk_lru.lane].L2, LRU_MRU);

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

	if (!(lru->qid == LRU_ENTRY_CLEANUP)) {
		struct lru_q *q;

		/* out with the old queue */
		q = lru_queue_of(entry);
		LRU_DQ_SAFE(lru, q);

		/* in with the new */
		q = &qlane->cleanup;
		lru_insert(lru, q, LRU_LRU);
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

			LRU_DQ_SAFE(lru, q);
			entry->lru.qid = LRU_ENTRY_CLEANUP;
			atomic_set_uint32_t_bits(&entry->lru.flags,
						 LRU_CLEANUP);
			/* Note: we didn't take a ref here, so the only ref left
			 * is the one owned by mdcache_unexport().  When it
			 * unref's, that will free this object. */

			/* Now we can safely clean out the first_export_id to
			 * indicate this entry is unmapped.
			 */
			atomic_store_int32_t(&entry->first_export_id, -1);

			QUNLOCK(qlane);
			cih_remove_latched(entry, &latch, CIH_REMOVE_NONE);
		} else {
			QUNLOCK(qlane);
		}

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
		struct root_op_context ctx;
		struct req_op_context *saved_ctx = op_ctx;
		int32_t export_id;
		struct gsh_export *export;

		/* check per-lane work */
		if (workdone >= lru_state.per_lane_work)
			goto next_lane;

		lru = glist_entry(qlane->iter.glist, mdcache_lru_t, q);

		/* get entry early.  This is safe without a ref, because we have
		 * the QLANE lock */
		entry = container_of(lru, mdcache_entry_t, lru);

		/* Get a reference to the first export and build an op context
		 * with it. By holding the QLANE lock while we get the export
		 * reference we assure that the entry doesn't get detached from
		 * the export before we get an export reference, which
		 * guarantees the export is good for the length of time we need
		 * it to perform sub_fsal operations.
		 */
		export_id = atomic_fetch_int32_t(&entry->first_export_id);

		if (export_id < 0) {
			/* This entry is part of an export that's going away.
			 * Just skip it. */
			continue;
		}

		export = get_gsh_export(export_id);

		if (export == NULL) {
			/* Creating the root object of an export and inserting
			 * the export are not atomic.  That is, we create the
			 * root object (and it's inserted in the LRU), and then
			 * we insert the export, to make it reachable.  This
			 * creates a tiny window the root object is in the LRU
			 * (and therefore visible in this function) but the
			 * export is not yet inserted, and so the above lookup
			 * will fail.  Skip such entries, as this is a
			 * self-correcting situation.
			 */
			continue;
		}

		/* Get a ref on the entry now */
		refcnt = atomic_inc_int32_t(&entry->lru.refcnt);
#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_lru_ref,
		   __func__, __LINE__, &entry->obj_handle, entry->sub_handle,
		   refcnt);
#endif

		init_root_op_context(&ctx, export, export->fsal_export, 0, 0,
				     UNKNOWN_REQUEST);

		/* check refcnt in range */
		if (unlikely(refcnt > 2)) {
			/* This unref is ok to be done without a valid op_ctx
			 * because we always map a new entry to an export before
			 * we could possibly release references in
			 * mdcache_new_entry.
			 */
			QUNLOCK(qlane);
			mdcache_lru_unref(entry);
			goto next_lru;
		}

		/* Move entry to MRU of L2 */
		q = &qlane->L1;
		LRU_DQ_SAFE(lru, q);
		lru->qid = LRU_ENTRY_L2;
		q = &qlane->L2;
		lru_insert(lru, q, LRU_MRU);

		/* Drop the lane lock while performing (slow) operations on
		 * entry */
		QUNLOCK(qlane);

		/* Make sure any FSAL global file descriptor is closed. */
		status = fsal_close(&entry->obj_handle);

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_CACHE_INODE_LRU,
				"Error closing file in LRU thread.");
		} else {
			++(*totalclosed);
			++closed;
		}

		mdcache_lru_unref(entry);

next_lru:
		QLOCK(qlane); /* QLOCKED */
		put_gsh_export(export);
		op_ctx = saved_ctx;

		++workdone;
	} /* for_each_safe lru */

next_lane:
	qlane->iter.active = false; /* !ACTIVE */
	QUNLOCK(qlane);
	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "Actually processed %zd entries on lane %zd closing %zd descriptors",
		 workdone, lane, closed);

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
	time_t threadwait = mdcache_param.lru_run_interval;
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
	static bool first_time = TRUE;

	if (first_time) {
		/* Wait for NFS server to properly initialize */
		nfs_init_wait();
		first_time = FALSE;
	}

	SetNameFunction("cache_lru");

	fds_avg = (lru_state.fds_hiwat - lru_state.fds_lowat) / 2;

	extremis = atomic_fetch_size_t(&open_fd_count) > lru_state.fds_hiwat;

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "LRU awakes.");

	if (!woke) {
		/* If we make it all the way through a timed sleep
		   without being woken, we assume we aren't racing
		   against the impossible. */
		if (lru_state.futility >= mdcache_param.futility_count)
			LogInfo(COMPONENT_CACHE_INODE_LRU,
				"Leaving FD futility mode.");

		lru_state.futility = 0;
	}

	LogFullDebug(COMPONENT_CACHE_INODE_LRU, "lru entries: %" PRIu64,
		     lru_state.entries_used);

	/* Reap file descriptors.  This is a preliminary example of the
	   L2 functionality rather than something we expect to be
	   permanent.  (It will have to adapt heavily to the new FSAL
	   API, for example.) */

	currentopen = atomic_fetch_size_t(&open_fd_count);

	if (currentopen < lru_state.fds_lowat) {
		LogDebug(COMPONENT_CACHE_INODE_LRU,
			 "FD count is %zd and low water mark is %d: not reaping.",
			 atomic_fetch_size_t(&open_fd_count),
			 lru_state.fds_lowat);
		if (atomic_fetch_uint32_t(&lru_state.fd_state) > FD_LOW) {
			LogEvent(COMPONENT_CACHE_INODE_LRU,
				 "Return to normal fd reaping.");
			atomic_store_uint32_t(&lru_state.fd_state, FD_LOW);
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

		if (currentopen < lru_state.fds_hiwat &&
		    atomic_fetch_uint32_t(&lru_state.fd_state) == FD_LIMIT) {
			LogEvent(COMPONENT_CACHE_INODE_LRU,
				 "Count of fd is below high water mark.");
			atomic_store_uint32_t(&lru_state.fd_state, FD_MIDDLE);
		}

		if ((curr_time >= lru_state.prev_time) &&
		    (curr_time - lru_state.prev_time < fridgethr_getwait(ctx)))
			threadwait = curr_time - lru_state.prev_time;

		fdratepersec = ((curr_time <= lru_state.prev_time) ||
				(formeropen < lru_state.prev_fd_count))
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
			if (++lru_state.futility ==
			    mdcache_param.futility_count) {
				LogWarn(COMPONENT_CACHE_INODE_LRU,
					"Futility count exceeded.  Client load is opening FDs faster than the LRU thread can close them.");
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
		 " fdrate:%u new_thread_wait=%" PRIu64,
		 atomic_fetch_size_t(&open_fd_count),
		 lru_state.entries_used, fdratepersec,
		 ((uint64_t) new_thread_wait));
	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		     "currentopen=%zd futility=%d totalwork=%zd biggest_window=%d extremis=%d lanes=%d fds_lowat=%d ",
		     currentopen, lru_state.futility, totalwork,
		     lru_state.biggest_window, extremis, LRU_N_Q_LANES,
		     lru_state.fds_lowat);
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

	q = &qlane->L1;

	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		 "Reaping up to %d chunks from lane %zd",
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

		chunk = container_of(lru, struct dir_chunk, chunk_lru);
		/* Get a ref on the chunk now */
		refcnt = atomic_inc_int32_t(&chunk->chunk_lru.refcnt);

		if (unlikely(refcnt > 2)) {
			QUNLOCK(qlane);
			mdcache_lru_unref_chunk(chunk);
			goto next_lru;
		}

		/* Move lru object to MRU of L2 */
		q = &qlane->L1;
		CHUNK_LRU_DQ_SAFE(lru, q);
		lru->qid = LRU_ENTRY_L2;
		q = &qlane->L2;
		lru_insert(lru, q, LRU_MRU);

		QUNLOCK(qlane);
		mdcache_lru_unref_chunk(chunk);

next_lru:
		QLOCK(qlane);
		++workdone;
	} /* for_each_safe lru */

next_lane:
	qlane->iter.active = false; /* !ACTIVE */
	QUNLOCK(qlane);
	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
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

	SetNameFunction("chunk_lru");

	LogFullDebug(COMPONENT_CACHE_INODE_LRU,
		     "LRU awakes, lru chunks used: %" PRIu64,
		     lru_state.chunks_used);

	/* Total chunks demoted to L2 between all lanes and all current runs. */
	for (lane = 0; lane < LRU_N_Q_LANES; ++lane) {
		LogFullDebug(COMPONENT_CACHE_INODE_LRU,
			 "Reaping up to %d chunks from lane %zd totalwork=%zd",
			 lru_state.per_lane_work, lane, totalwork);

		totalwork += chunk_lru_run_lane(lane);
	}

	/* Run more frequently the closer to max number of chunks we are. */
	wait_ratio = 1.0 - (lru_state.chunks_used / lru_state.chunks_hiwat);

	new_thread_wait = mdcache_param.lru_run_interval * wait_ratio;

	if (new_thread_wait < mdcache_param.lru_run_interval / 10)
		new_thread_wait = mdcache_param.lru_run_interval / 10;

	fridgethr_setwait(ctx, new_thread_wait);

	LogDebug(COMPONENT_CACHE_INODE_LRU,
		 "After work, threadwait=%" PRIu64 " totalwork=%zd",
		 ((uint64_t) new_thread_wait), totalwork);
}

/**
 * @brief Remove reapable entries until we are below the high-water mark
 *
 * If something refs a lot of entries at the same time, this can put the number
 * of entries above the high water mark.  They will slowly fall, as entries are
 * actually freed, but this may take a very long time.
 *
 * This is a big hammer, that will clean up anything it can until either it
 * can't anymore, or we're back below the high water mark.
 *
 * @param[in] parm     Parameter description
 * @return Return description
 */
void lru_cleanup_entries(void)
{
	mdcache_lru_t *lru;
	mdcache_entry_t *entry = NULL;

	while ((lru = lru_try_reap_entry())) {
		if (lru) {
			entry = container_of(lru, mdcache_entry_t, lru);
			mdcache_lru_unref(entry);
		}
	}
}

void init_fds_limit(void)
{
	int code = 0;
	/* Rlimit for open file descriptors */
	struct rlimit rlim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};

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

	if (mdcache_param.reaper_work) {
		/* Backwards compatibility */
		lru_state.per_lane_work = (mdcache_param.reaper_work +
					   LRU_N_Q_LANES - 1) / LRU_N_Q_LANES;
	} else {
		/* New parameter */
		lru_state.per_lane_work = mdcache_param.reaper_work_per_lane;
	}

	lru_state.biggest_window =
	    (mdcache_param.biggest_window *
	     lru_state.fds_system_imposed) / 100;
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
	struct fridgethr_params frp;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 2;
	frp.thr_min = 2;
	frp.thread_delay = mdcache_param.lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	atomic_store_size_t(&open_fd_count, 0);
	lru_state.prev_fd_count = 0;
	atomic_store_uint32_t(&lru_state.fd_state, FD_LOW);
	init_fds_limit();

	/* Set high and low watermark for cache entries.  XXX This seems a
	   bit fishy, so come back and revisit this. */
	lru_state.entries_hiwat = mdcache_param.entries_hwmark;
	lru_state.entries_used = 0;

	/* Set high and low watermark for chunks.  XXX This seems a
	   bit fishy, so come back and revisit this. */
	lru_state.chunks_hiwat = mdcache_param.chunks_hwmark;
	lru_state.chunks_used = 0;


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
			 "Unable to start Entry LRU thread, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	code = fridgethr_submit(lru_fridge, chunk_lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_CACHE_INODE_LRU,
			 "Unable to start Chunk LRU thread, error code %d.",
			 code);
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
 * sentinel, one to allow the caller's use.)
 *
 * The caller MUST call mdcache_lru_insert when the entry is sufficiently
 * constructed.
 *
 * @return a usable entry or NULL if unexport is in progress.
 */
mdcache_entry_t *mdcache_lru_get(struct fsal_obj_handle *sub_handle)
{
	mdcache_lru_t *lru;
	mdcache_entry_t *nentry = NULL;

	lru = lru_try_reap_entry();
	if (lru) {
		/* we uniquely hold entry */
		nentry = container_of(lru, mdcache_entry_t, lru);
		mdcache_lru_clean(nentry);
		memset(&nentry->attrs, 0, sizeof(nentry->attrs));
		init_rw_locks(nentry);
	} else {
		/* alloc entry (if fails, aborts) */
		nentry = alloc_cache_entry();
	}

	/* Since the entry isn't in a queue, nobody can bump refcnt. */
	nentry->lru.refcnt = 2;
	nentry->lru.cf = 0;
	nentry->lru.lane = lru_lane_of(nentry);
	nentry->sub_handle = sub_handle;

#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_lru_get,
		  __func__, __LINE__, &nentry->obj_handle, sub_handle,
		  nentry->lru.refcnt);
#endif
	return nentry;
}

/**
 * @brief Insert a new entry into the LRU.
 *
 * Entry is inserted the LRU.  For scans, insert into the MRU of L2, to avoid
 * having entries recycled before they're used during readdir.  For everything
 * else, insert into LRU of L1, so that a single ref promotes to the MRU of L1.
 *
 * @param [in] entry  Entry to insert.
 * @param [in] reason Reason we're inserting
 */
void mdcache_lru_insert(mdcache_entry_t *entry, mdc_reason_t reason)
{
	/* Enqueue. */
	switch (reason) {
	case MDC_REASON_DEFAULT:
		lru_insert_entry(entry, &LRU[entry->lru.lane].L1, LRU_LRU);
		break;
	case MDC_REASON_SCAN:
		lru_insert_entry(entry, &LRU[entry->lru.lane].L2, LRU_MRU);
		break;
	}
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
_mdcache_lru_ref(mdcache_entry_t *entry, uint32_t flags, const char *func,
		 int line)
{
	mdcache_lru_t *lru = &entry->lru;
	struct lru_q_lane *qlane = &LRU[lru->lane];
	struct lru_q *q;
#ifdef USE_LTTNG
	int32_t refcnt =
#endif
		atomic_inc_int32_t(&entry->lru.refcnt);

#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_lru_ref,
		   func, line, &entry->obj_handle, entry->sub_handle, refcnt);
#endif

	/* adjust LRU on initial refs */
	if (flags & LRU_REQ_INITIAL) {

		QLOCK(qlane);

		switch (lru->qid) {
		case LRU_ENTRY_L1:
			q = lru_queue_of(entry);
			/* advance entry to MRU (of L1) */
			LRU_DQ_SAFE(lru, q);
			lru_insert(lru, q, LRU_MRU);
			break;
		case LRU_ENTRY_L2:
			q = lru_queue_of(entry);
			/* move entry to LRU of L1 */
			glist_del(&lru->q);	/* skip L1 fixups */
			--(q->size);
			q = &qlane->L1;
			lru_insert(lru, q, LRU_LRU);
			break;
		default:
			/* do nothing */
			break;
		}		/* switch qid */
		QUNLOCK(qlane);
	}			/* initial ref */

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
_mdcache_lru_unref(mdcache_entry_t *entry, uint32_t flags, const char *func,
		   int line)
{
	int32_t refcnt;
	bool do_cleanup = false;
	uint32_t lane = entry->lru.lane;
	struct lru_q_lane *qlane = &LRU[lane];
	bool other_lock_held = entry->fsobj.hdl.no_cleanup;
	bool freed = false;

	if (!other_lock_held) {
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

#ifdef USE_LTTNG
	tracepoint(mdcache, mdc_lru_unref,
		   func, line, &entry->obj_handle, entry->sub_handle, refcnt);
#endif

	if (unlikely(refcnt == 0)) {

		struct lru_q *q;

		/* we MUST recheck that refcount is still 0 */
		QLOCK(qlane);
		refcnt = atomic_fetch_int32_t(&entry->lru.refcnt);

		if (unlikely(refcnt > 0)) {
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
 * @brief Remove a chunk from LRU, and clean it
 *
 * QLane must be locked
 *
 * @param[in] chunk  The chunk to be removed from LRU
 */
static void lru_clean_chunk(struct dir_chunk *chunk)
{
	struct lru_q *lq;

	LogFullDebug(COMPONENT_CACHE_INODE, "Removing chunk %p", chunk);

	/* Remove chunk and mark it as dead. */
	lq = chunk_lru_queue_of(chunk);

	if (lq) {
		/* dequeue the chunk */
		CHUNK_LRU_DQ_SAFE(&chunk->chunk_lru, lq);
	}

	(void) atomic_dec_int64_t(&lru_state.chunks_used);

	/* Then do the actual cleaning work. */
	mdcache_clean_dirent_chunk(chunk);
}

void mdcache_lru_ref_chunk(struct dir_chunk *chunk)
{
	atomic_inc_int32_t(&chunk->chunk_lru.refcnt);
}

void mdcache_lru_unref_chunk(struct dir_chunk *chunk)
{
	int refcnt;
	uint32_t lane = chunk->chunk_lru.lane;
	struct lru_q_lane *qlane;

	qlane = &CHUNK_LRU[lane];
	QLOCK(qlane);

	refcnt = atomic_dec_int32_t(&chunk->chunk_lru.refcnt);
	if (refcnt == 0) {
		lru_clean_chunk(chunk);

		/* And now we can free the chunk. */
		LogFullDebug(COMPONENT_CACHE_INODE, "Freeing chunk %p", chunk);
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
		CHUNK_LRU_DQ_SAFE(lru, q);
		lru_insert(lru, q, LRU_MRU);
		break;
	case LRU_ENTRY_L2:
		/* move chunk to LRU of L1 */
		glist_del(&lru->q);	/* skip L1 fixups */
		--(q->size);
		q = &qlane->L1;
		lru_insert(lru, q, LRU_LRU);
		break;
	default:
		/* do nothing */
		break;
	}

	QUNLOCK(qlane);
}

/**
 * @brief Check if FDs are available.
 *
 * This function checks if FDs are available to serve open
 * requests. This function also wakes the LRU thread if the
 * current FD count is above the high water mark.
 *
 * @return true if there are FDs available to serve open requests,
 * false otherwise.
 */
bool mdcache_lru_fds_available(void)
{
	if (atomic_fetch_size_t(&open_fd_count) >= lru_state.fds_hard_limit) {
		LogAtLevel(COMPONENT_CACHE_INODE_LRU,
			   atomic_fetch_uint32_t(&lru_state.fd_state)
								!= FD_LIMIT
				? NIV_CRIT
				: NIV_DEBUG,
			   "FD Hard Limit Exceeded, waking LRU thread.");
		atomic_store_uint32_t(&lru_state.fd_state, FD_LIMIT);
		fridgethr_wake(lru_fridge);
		return false;
	}

	if (atomic_fetch_size_t(&open_fd_count) >= lru_state.fds_hiwat) {
		LogAtLevel(COMPONENT_CACHE_INODE_LRU,
			   atomic_fetch_uint32_t(&lru_state.fd_state) == FD_LOW
				? NIV_INFO
				: NIV_DEBUG,
			   "FDs above high water mark, waking LRU thread.");
		atomic_store_uint32_t(&lru_state.fd_state, FD_HIGH);
		fridgethr_wake(lru_fridge);
	}

	return true;
}

/** @} */
