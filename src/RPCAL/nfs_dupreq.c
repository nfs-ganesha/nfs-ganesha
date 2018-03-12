/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * Portions Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
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
 * @file nfs_dupreq.c
 * @author Matt Benjamin <matt@linuxbox.com>
 * @brief NFS Duplicate Request Cache
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/* XXX prune: */
#include "log.h"
#include "nfs_proto_functions.h"

#include "nfs_dupreq.h"
#include "city.h"
#include "abstract_mem.h"
#include "gsh_intrinsic.h"
#include "gsh_wait_queue.h"

#define DUPREQ_NOCACHE   0x02
#define DUPREQ_MAX_RETRIES 5

#define NFS_pcp nfs_param.core_param
#define NFS_program NFS_pcp.program

pool_t *dupreq_pool;
pool_t *nfs_res_pool;
pool_t *tcp_drc_pool;		/* pool of per-connection DRC objects */

const char *dupreq_status_table[] = {
	"DUPREQ_SUCCESS",
	"DUPREQ_INSERT_MALLOC_ERROR",
	"DUPREQ_BEING_PROCESSED",
	"DUPREQ_EXISTS",
	"DUPREQ_ERROR",
};

const char *dupreq_state_table[] = {
	"DUPREQ_START",
	"DUPREQ_COMPLETE",
	"DUPREQ_DELETED",
};

/* drc_t holds the request/response cache. There is a single drc_t for
 * all udp connections. There is a drc_t for each tcp connection (aka
 * socket). Since a client could close a socket and reconnect, we would
 * like to use the same drc cache for the reconnection. For this reason,
 * we don't want to free the drc as soon as the tcp connection gets
 * closed, but rather keep them in a recycle list for sometime.
 *
 * The life of tcp drc: it gets allocated when we process the first
 * request on the connection. It is put into rbtree (tcp_drc_recycle_t).
 * drc cache maintains a ref count. Every request as well as the xprt
 * holds a ref count. Its ref count should go to zero when the
 * connection's xprt gets freed (all requests should be completed on the
 * xprt by this time). When the ref count goes to zero, it is also put
 * into a recycle queue (tcp_drc_recycle_q). When a reconnection
 * happens, we hope to find the same drc that was used before, and the
 * ref count goes up again. At the same time, the drc will be removed
 * from the recycle queue. Only drc's with ref count zero end up in the
 * recycle queue. If a reconnection doesn't happen in time, the drc gets
 * freed by drc_free_expired() after some period of inactivety.
 *
 * Most ref count methods assume that a ref count doesn't go up from
 * zero, so a thread that decrements the ref count to zero would be the
 * only one acting on it, and it could do so without any locks!  Since
 * the drc ref count could go up from zero, care must be taken. The
 * thread that decrements the ref count to zero will have to put the drc
 * into the recycle queue. It will do so only after dropping the lock in
 * the current implementation. If we let nfs_dupreq_get_drc() reuse the
 * drc before it gets into recycle queue, we could end up with multiple
 * threads that decrement the ref count to zero.
 */
struct drc_st {
	pthread_mutex_t mtx;
	drc_t udp_drc;		/* shared DRC */
	struct rbtree_x tcp_drc_recycle_t;
	 TAILQ_HEAD(drc_st_tailq, drc) tcp_drc_recycle_q;	/* fifo */
	int32_t tcp_drc_recycle_qlen;
	time_t last_expire_check;
	uint32_t expire_delta;
};

static struct drc_st *drc_st;

/**
 * @brief Comparison function for duplicate request entries.
 *
 * @param[in] lhs An integer
 * @param[in] rhs Another integer
 *
 * @return -1 if the left-hand is smaller than the right, 0 if they
 * are equal, and 1 if the left-hand is larger.
 */
static inline int uint32_cmpf(uint32_t lhs, uint32_t rhs)
{
	if (lhs < rhs)
		return -1;

	if (lhs == rhs)
		return 0;

	return 1;
}

/**
 * @brief Comparison function for duplicate request entries.
 *
 * @param[in] lhs An integer
 * @param[in] rhs Another integer
 *
 * @return -1 if the left-hand is smaller than the right, 0 if they
 * are equal, and 1 if the left-hand is larger.
 */
static inline int uint64_cmpf(uint64_t lhs, uint64_t rhs)
{
	if (lhs < rhs)
		return -1;

	if (lhs == rhs)
		return 0;

	return 1;
}

/**
 * @brief Comparison function for entries in a shared DRC
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int dupreq_shared_cmpf(const struct opr_rbtree_node *lhs,
				     const struct opr_rbtree_node *rhs)
{
	dupreq_entry_t *lk, *rk;

	lk = opr_containerof(lhs, dupreq_entry_t, rbt_k);
	rk = opr_containerof(rhs, dupreq_entry_t, rbt_k);

	switch (sockaddr_cmpf(&lk->hin.addr, &rk->hin.addr, false)) {
	case -1:
		return -1;
	case 0:
		switch (uint32_cmpf(lk->hin.tcp.rq_xid, rk->hin.tcp.rq_xid)) {
		case -1:
			return -1;
		case 0:
			return uint64_cmpf(lk->hk, rk->hk);
		default:
			break;
		}		/* xid */
		break;
	default:
		break;
	}			/* addr+port */

	return 1;
}

/**
 * @brief Comparison function for entries in a per-connection (TCP) DRC
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int dupreq_tcp_cmpf(const struct opr_rbtree_node *lhs,
				  const struct opr_rbtree_node *rhs)
{
	dupreq_entry_t *lk, *rk;

	LogDebug(COMPONENT_DUPREQ, "%s", __func__);

	lk = opr_containerof(lhs, dupreq_entry_t, rbt_k);
	rk = opr_containerof(rhs, dupreq_entry_t, rbt_k);

	if (lk->hin.tcp.rq_xid < rk->hin.tcp.rq_xid)
		return -1;

	if (lk->hin.tcp.rq_xid == rk->hin.tcp.rq_xid) {
		LogDebug(COMPONENT_DUPREQ,
			 "xids eq %" PRIu32 ", ck1 %" PRIu64 " ck2 %" PRIu64,
			 lk->hin.tcp.rq_xid, lk->hk, rk->hk);
		return uint64_cmpf(lk->hk, rk->hk);
	}

	return 1;
}

/**
 * @brief Comparison function for recycled per-connection (TCP) DRCs
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int drc_recycle_cmpf(const struct opr_rbtree_node *lhs,
				   const struct opr_rbtree_node *rhs)
{
	drc_t *lk, *rk;

	lk = opr_containerof(lhs, drc_t, d_u.tcp.recycle_k);
	rk = opr_containerof(rhs, drc_t, d_u.tcp.recycle_k);

	return sockaddr_cmpf(
		&lk->d_u.tcp.addr, &rk->d_u.tcp.addr, false);
}

/**
 * @brief Initialize a shared duplicate request cache
 */
static inline void init_shared_drc(void)
{
	drc_t *drc = &drc_st->udp_drc;
	int ix, code __attribute__ ((unused)) = 0;

	drc->type = DRC_UDP_V234;
	drc->refcnt = 0;
	drc->retwnd = 0;
	drc->d_u.tcp.recycle_time = 0;
	drc->maxsize = nfs_param.core_param.drc.udp.size;
	drc->cachesz = nfs_param.core_param.drc.udp.cachesz;
	drc->npart = nfs_param.core_param.drc.udp.npart;
	drc->hiwat = nfs_param.core_param.drc.udp.hiwat;

	gsh_mutex_init(&drc->mtx, NULL);

	/* init dict */
	code =
	    rbtx_init(&drc->xt, dupreq_shared_cmpf, drc->npart,
		      RBT_X_FLAG_ALLOC | RBT_X_FLAG_CACHE_WT);
	assert(!code);

	/* completed requests */
	TAILQ_INIT(&drc->dupreq_q);

	/* init closed-form "cache" partition */
	for (ix = 0; ix < drc->npart; ++ix) {
		struct rbtree_x_part *xp = &(drc->xt.tree[ix]);

		drc->xt.cachesz = drc->cachesz;
		xp->cache =
		    gsh_calloc(drc->cachesz, sizeof(struct opr_rbtree_node *));
	}
}

/**
 * @brief Initialize the DRC package.
 */
void dupreq2_pkginit(void)
{
	int code __attribute__ ((unused)) = 0;

	dupreq_pool =
	    pool_basic_init("Duplicate Request Pool", sizeof(dupreq_entry_t));

	nfs_res_pool = pool_basic_init("nfs_res_t pool", sizeof(nfs_res_t));

	tcp_drc_pool = pool_basic_init("TCP DRC Pool", sizeof(drc_t));

	drc_st = gsh_calloc(1, sizeof(struct drc_st));

	/* init shared statics */
	gsh_mutex_init(&drc_st->mtx, NULL);

	/* recycle_t */
	code =
	    rbtx_init(&drc_st->tcp_drc_recycle_t, drc_recycle_cmpf,
		      nfs_param.core_param.drc.tcp.recycle_npart,
		      RBT_X_FLAG_ALLOC);
	/* XXX error? */

	/* init recycle_q */
	TAILQ_INIT(&drc_st->tcp_drc_recycle_q);
	drc_st->tcp_drc_recycle_qlen = 0;
	drc_st->last_expire_check = time(NULL);
	drc_st->expire_delta = nfs_param.core_param.drc.tcp.recycle_expire_s;

	/* UDP DRC is global, shared */
	init_shared_drc();
}

/**
 * @brief Determine the protocol of the supplied TI-RPC SVCXPRT*
 *
 * @param[in] xprt  The SVCXPRT
 *
 * @return IPPROTO_UDP or IPPROTO_TCP.
 */
static inline unsigned int get_ipproto_by_xprt(SVCXPRT *xprt)
{
	switch (xprt->xp_type) {
	case XPRT_UDP:
	case XPRT_UDP_RENDEZVOUS:
		return IPPROTO_UDP;
	case XPRT_TCP:
	case XPRT_TCP_RENDEZVOUS:
		return IPPROTO_TCP;
	default:
		break;
	}
	return IPPROTO_IP;	/* Dummy output */
}

/**
 * @brief Determine the dupreq2 DRC type to handle the supplied svc_req
 *
 * @param[in] req The svc_req being processed
 *
 * @return a value of type enum_drc_type.
 */
static inline enum drc_type get_drc_type(struct svc_req *req)
{
	if (get_ipproto_by_xprt(req->rq_xprt) == IPPROTO_UDP)
		return DRC_UDP_V234;
	else {
		if (req->rq_msg.cb_vers == 4)
			return DRC_TCP_V4;
	}
	return DRC_TCP_V3;
}

/**
 * @brief Allocate a duplicate request cache
 *
 * @param[in] dtype   Style DRC to allocate (e.g., TCP, by enum drc_type)
 * @param[in] maxsz   Upper bound on requests to cache
 * @param[in] cachesz Number of entries in the closed hash partition
 * @param[in] flags   DRC flags
 *
 * @return the drc, if successfully allocated, else NULL.
 */
static inline drc_t *alloc_tcp_drc(enum drc_type dtype)
{
	drc_t *drc = pool_alloc(tcp_drc_pool);
	int ix, code __attribute__ ((unused)) = 0;

	drc->type = dtype;	/* DRC_TCP_V3 or DRC_TCP_V4 */
	drc->refcnt = 0;
	drc->retwnd = 0;
	drc->d_u.tcp.recycle_time = 0;
	drc->maxsize = nfs_param.core_param.drc.tcp.size;
	drc->cachesz = nfs_param.core_param.drc.tcp.cachesz;
	drc->npart = nfs_param.core_param.drc.tcp.npart;
	drc->hiwat = nfs_param.core_param.drc.tcp.hiwat;

	PTHREAD_MUTEX_init(&drc->mtx, NULL);

	/* init dict */
	code =
	    rbtx_init(&drc->xt, dupreq_tcp_cmpf, drc->npart,
		      RBT_X_FLAG_ALLOC | RBT_X_FLAG_CACHE_WT);
	assert(!code);

	/* completed requests */
	TAILQ_INIT(&drc->dupreq_q);

	/* recycling DRC */
	TAILQ_INIT_ENTRY(drc, d_u.tcp.recycle_q);

	/* init "cache" partition */
	for (ix = 0; ix < drc->npart; ++ix) {
		struct rbtree_x_part *xp = &(drc->xt.tree[ix]);

		drc->xt.cachesz = drc->cachesz;
		xp->cache =
		    gsh_calloc(drc->cachesz, sizeof(struct opr_rbtree_node *));
	}

	return drc;
}

/**
 * @brief Deep-free a per-connection (TCP) duplicate request cache
 *
 * @param[in] drc  The DRC to dispose
 *
 * Assumes that the DRC has been allocated from the tcp_drc_pool.
 */
static inline void free_tcp_drc(drc_t *drc)
{
	int ix;

	for (ix = 0; ix < drc->npart; ++ix) {
		if (drc->xt.tree[ix].cache)
			gsh_free(drc->xt.tree[ix].cache);
	}
	PTHREAD_MUTEX_destroy(&drc->mtx);
	LogFullDebug(COMPONENT_DUPREQ, "free TCP drc %p", drc);
	pool_free(tcp_drc_pool, drc);
}

/**
 * @brief Increment the reference count on a DRC
 *
 * @param[in] drc  The DRC to ref
 *
 * @return the new value of refcnt.
 */
static inline uint32_t nfs_dupreq_ref_drc(drc_t *drc)
{
	return ++(drc->refcnt); /* locked */
}

/**
 * @brief Decrement the reference count on a DRC
 *
 * @param[in] drc  The DRC to unref
 *
 * @return the new value of refcnt.
 */
static inline uint32_t nfs_dupreq_unref_drc(drc_t *drc)
{
	return --(drc->refcnt); /* locked */
}

#define DRC_ST_LOCK()				\
	PTHREAD_MUTEX_lock(&drc_st->mtx)

#define DRC_ST_UNLOCK()				\
	PTHREAD_MUTEX_unlock(&drc_st->mtx)

/**
 * @brief Check for expired TCP DRCs.
 */
static inline void drc_free_expired(void)
{
	drc_t *drc;
	time_t now = time(NULL);
	struct rbtree_x_part *t;
	struct opr_rbtree_node *odrc = NULL;

	DRC_ST_LOCK();

	if ((drc_st->tcp_drc_recycle_qlen < 1) ||
	    (now - drc_st->last_expire_check) < 600) /* 10m */
		goto unlock;

	do {
		drc = TAILQ_FIRST(&drc_st->tcp_drc_recycle_q);
		if (drc && (drc->d_u.tcp.recycle_time > 0)
		    && ((now - drc->d_u.tcp.recycle_time) >
			drc_st->expire_delta)) {

			assert(drc->refcnt == 0);

			LogFullDebug(COMPONENT_DUPREQ,
				     "remove expired drc %p from recycle queue",
				     drc);
			t = rbtx_partition_of_scalar(&drc_st->tcp_drc_recycle_t,
						     drc->d_u.tcp.hk);

			odrc =
			    opr_rbtree_lookup(&t->t, &drc->d_u.tcp.recycle_k);
			if (!odrc) {
				LogCrit(COMPONENT_DUPREQ,
					"BUG: asked to dequeue DRC not on queue");
			} else {
				(void)opr_rbtree_remove(
						&t->t, &drc->d_u.tcp.recycle_k);
			}
			TAILQ_REMOVE(&drc_st->tcp_drc_recycle_q, drc,
				     d_u.tcp.recycle_q);
			--(drc_st->tcp_drc_recycle_qlen);

			free_tcp_drc(drc);
		} else {
			LogFullDebug(COMPONENT_DUPREQ,
				     "unexpired drc %p in recycle queue expire check (nothing happens)",
				     drc);
			drc_st->last_expire_check = now;
			break;
		}

	} while (1);

 unlock:
	DRC_ST_UNLOCK();
}

/**
 * @brief Find and reference a DRC to process the supplied svc_req.
 *
 * @param[in] req  The svc_req being processed.
 *
 * @return The ref'd DRC if sucessfully located, else NULL.
 */
static /* inline */ drc_t *
nfs_dupreq_get_drc(struct svc_req *req)
{
	enum drc_type dtype = get_drc_type(req);
	drc_t *drc = NULL;
	bool drc_check_expired = false;

	switch (dtype) {
	case DRC_UDP_V234:
		LogFullDebug(COMPONENT_DUPREQ, "ref shared UDP DRC");
		drc = &(drc_st->udp_drc);
		DRC_ST_LOCK();
		(void)nfs_dupreq_ref_drc(drc);
		DRC_ST_UNLOCK();
		goto out;
retry:
	case DRC_TCP_V4:
	case DRC_TCP_V3:
		/* Idempotent address, no need for lock;
		 * xprt will be valid as long as svc_req.
		 */
		drc = (drc_t *)req->rq_xprt->xp_u2;
		if (drc) {
			/* found, no danger of removal */
			LogFullDebug(COMPONENT_DUPREQ, "ref DRC=%p for xprt=%p",
				     drc, req->rq_xprt);
			PTHREAD_MUTEX_lock(&drc->mtx);	/* LOCKED */
		} else {
			drc_t drc_k;
			struct rbtree_x_part *t = NULL;
			struct opr_rbtree_node *ndrc = NULL;
			drc_t *tdrc = NULL;

			memset(&drc_k, 0, sizeof(drc_k));
			drc_k.type = dtype;

			/* Since the drc can last longer than the xprt,
			 * copy the address. Read operation of constant data,
			 * no xprt lock required.
			 */
			(void)copy_xprt_addr(&drc_k.d_u.tcp.addr, req->rq_xprt);

			drc_k.d_u.tcp.hk =
			    CityHash64WithSeed((char *)&drc_k.d_u.tcp.addr,
					       sizeof(sockaddr_t), 911);
			{
				char str[SOCK_NAME_MAX];

				sprint_sockaddr(&drc_k.d_u.tcp.addr,
						str, sizeof(str));
				LogFullDebug(COMPONENT_DUPREQ,
					     "get drc for addr: %s", str);
			}

			t = rbtx_partition_of_scalar(&drc_st->tcp_drc_recycle_t,
						     drc_k.d_u.tcp.hk);
			DRC_ST_LOCK();

			/* Avoid double reference of drc,
			 * rechecking xp_u2 after DRC_ST_LOCK */
			if (req->rq_xprt->xp_u2) {
				DRC_ST_UNLOCK();
				goto retry;
			}

			ndrc =
			    opr_rbtree_lookup(&t->t, &drc_k.d_u.tcp.recycle_k);
			if (ndrc) {
				/* reuse old DRC */
				tdrc = opr_containerof(ndrc, drc_t,
						       d_u.tcp.recycle_k);
				PTHREAD_MUTEX_lock(&tdrc->mtx);	/* LOCKED */

				/* If the refcnt is zero and it is not
				 * in the recycle queue, wait for the
				 * other thread to put it in the queue.
				 */
				if (tdrc->refcnt == 0) {
					if (!(tdrc->flags & DRC_FLAG_RECYCLE)) {
						PTHREAD_MUTEX_unlock(
								&tdrc->mtx);
						DRC_ST_UNLOCK();
						goto retry;
					}
					TAILQ_REMOVE(&drc_st->tcp_drc_recycle_q,
						     tdrc, d_u.tcp.recycle_q);
					--(drc_st->tcp_drc_recycle_qlen);
					tdrc->flags &= ~DRC_FLAG_RECYCLE;
				}
				drc = tdrc;
				LogFullDebug(COMPONENT_DUPREQ,
					     "recycle TCP DRC=%p for xprt=%p",
					     tdrc, req->rq_xprt);
			}

			if (!drc) {
				drc = alloc_tcp_drc(dtype);
				LogFullDebug(COMPONENT_DUPREQ,
					     "alloc new TCP DRC=%p for xprt=%p",
					     drc, req->rq_xprt);
				/* assign addr */
				memcpy(&drc->d_u.tcp.addr, &drc_k.d_u.tcp.addr,
				       sizeof(sockaddr_t));
				/* assign already-computed hash */
				drc->d_u.tcp.hk = drc_k.d_u.tcp.hk;
				PTHREAD_MUTEX_lock(&drc->mtx);	/* LOCKED */
				/* insert dict */
				opr_rbtree_insert(&t->t,
						  &drc->d_u.tcp.recycle_k);
			}

			/* Avoid double reference of drc,
			 * setting xp_u2 under DRC_ST_LOCK */
			req->rq_xprt->xp_u2 = (void *)drc;
			(void)nfs_dupreq_ref_drc(drc);  /* xprt ref */

			DRC_ST_UNLOCK();
			drc->d_u.tcp.recycle_time = 0;

			/* try to expire unused DRCs somewhat in proportion to
			 * new connection arrivals */
			drc_check_expired = true;

			LogFullDebug(COMPONENT_DUPREQ,
				     "after ref drc %p refcnt==%u ", drc,
				     drc->refcnt);
		}
		break;
	default:
		/* XXX error */
		break;
	}

	/* call path ref */
	(void)nfs_dupreq_ref_drc(drc);
	PTHREAD_MUTEX_unlock(&drc->mtx);

	if (drc_check_expired)
		drc_free_expired();

out:
	return drc;
}

/**
 * @brief Release previously-ref'd DRC.
 *
 * Release previously-ref'd DRC.  If its refcnt drops to 0, the DRC
 * is queued for later recycling.
 *
 * @param[in] drc   The DRC
 * @param[in] flags Control flags
 */
void nfs_dupreq_put_drc(drc_t *drc, uint32_t flags)
{
	if (!(flags & DRC_FLAG_LOCKED))
		PTHREAD_MUTEX_lock(&drc->mtx);
	/* drc LOCKED */

	if (drc->refcnt == 0) {
		LogCrit(COMPONENT_DUPREQ,
			"drc %p refcnt will underrun refcnt=%u", drc,
			drc->refcnt);
	}

	nfs_dupreq_unref_drc(drc);

	LogFullDebug(COMPONENT_DUPREQ, "drc %p refcnt==%u", drc, drc->refcnt);

	switch (drc->type) {
	case DRC_UDP_V234:
		/* do nothing */
		break;
	case DRC_TCP_V4:
	case DRC_TCP_V3:
		if (drc->refcnt != 0) /* quick path */
			break;

		/* note t's lock order wrt drc->mtx is the opposite of
		 * drc->xt[*].lock. Drop and reacquire locks in correct
		 * order.
		 */
		PTHREAD_MUTEX_unlock(&drc->mtx);
		DRC_ST_LOCK();
		PTHREAD_MUTEX_lock(&drc->mtx);

		/* Since we dropped and reacquired the drc lock for the
		 * correct lock order, we need to recheck the drc fields
		 * again!
		 */
		if (drc->refcnt == 0 && !(drc->flags & DRC_FLAG_RECYCLE)) {
			drc->d_u.tcp.recycle_time = time(NULL);
			drc->flags |= DRC_FLAG_RECYCLE;
			TAILQ_INSERT_TAIL(&drc_st->tcp_drc_recycle_q,
					  drc, d_u.tcp.recycle_q);
			++(drc_st->tcp_drc_recycle_qlen);
			LogFullDebug(COMPONENT_DUPREQ,
				     "enqueue drc %p for recycle", drc);
		}
		DRC_ST_UNLOCK();
		break;

	default:
		break;
	};

	PTHREAD_MUTEX_unlock(&drc->mtx); /* !LOCKED */
}

/**
 * @brief Resolve indirect request function vector for the supplied DRC entry
 *
 * @param[in] dv The duplicate request entry.
 *
 * @return The function vector if successful, else NULL.
 */
static inline const nfs_function_desc_t *nfs_dupreq_func(dupreq_entry_t *dv)
{
	const nfs_function_desc_t *func = NULL;

	if (dv->hin.rq_prog == NFS_program[P_NFS]) {
		switch (dv->hin.rq_vers) {
#ifdef _USE_NFS3
		case NFS_V3:
			func = &nfs3_func_desc[dv->hin.rq_proc];
			break;
#endif /* _USE_NFS3 */
		case NFS_V4:
			func = &nfs4_func_desc[dv->hin.rq_proc];
			break;
		default:
			/* not reached */
			LogMajor(COMPONENT_DUPREQ,
				 "NFS Protocol version %" PRIu32 " unknown",
				 dv->hin.rq_vers);
		}
	} else if (dv->hin.rq_prog == NFS_program[P_MNT]) {
		switch (dv->hin.rq_vers) {
		case MOUNT_V1:
			func = &mnt1_func_desc[dv->hin.rq_proc];
			break;
		case MOUNT_V3:
			func = &mnt3_func_desc[dv->hin.rq_proc];
			break;
		default:
			/* not reached */
			LogMajor(COMPONENT_DUPREQ,
				 "MOUNT Protocol version %" PRIu32 " unknown",
				 dv->hin.rq_vers);
			break;
		}
#ifdef _USE_NLM
	} else if (dv->hin.rq_prog == NFS_program[P_NLM]) {
		switch (dv->hin.rq_vers) {
		case NLM4_VERS:
			func = &nlm4_func_desc[dv->hin.rq_proc];
			break;
		}
#endif /* _USE_NLM */
	} else if (dv->hin.rq_prog == NFS_program[P_RQUOTA]) {
		switch (dv->hin.rq_vers) {
		case RQUOTAVERS:
			func = &rquota1_func_desc[dv->hin.rq_proc];
			break;
		case EXT_RQUOTAVERS:
			func = &rquota2_func_desc[dv->hin.rq_proc];
			break;
		}
	} else {
		/* not reached */
		LogMajor(COMPONENT_DUPREQ,
			 "protocol %" PRIu32 " is not managed",
			 dv->hin.rq_prog);
	}

	return func;
}

/**
 * @brief Construct a duplicate request cache entry.
 *
 * Entries are allocated from the dupreq_pool.  Since dupre_entry_t
 * presently contains an expanded nfs_arg_t, zeroing of at least corresponding
 * value pointers is required for XDR allocation.
 *
 * @return The newly allocated dupreq entry or NULL.
 */
static inline dupreq_entry_t *alloc_dupreq(void)
{
	dupreq_entry_t *dv;

	dv = pool_alloc(dupreq_pool);
	gsh_mutex_init(&dv->mtx, NULL);
	TAILQ_INIT_ENTRY(dv, fifo_q);

	return dv;
}

/**
 * @brief Deep-free a duplicate request cache entry.
 *
 * If the entry has processed request data, the corresponding free
 * function is called on the result.  The cache entry is then returned
 * to the dupreq_pool.
 */
static inline void nfs_dupreq_free_dupreq(dupreq_entry_t *dv)
{
	const nfs_function_desc_t *func;

	assert(dv->refcnt == 0);

	LogDebug(COMPONENT_DUPREQ,
		 "freeing dupreq entry dv=%p, dv xid=%" PRIu32
		 " cksum %" PRIu64 " state=%s",
		 dv, dv->hin.tcp.rq_xid, dv->hk,
		 dupreq_state_table[dv->state]);
	if (dv->res) {
		func = nfs_dupreq_func(dv);
		func->free_function(dv->res);
		free_nfs_res(dv->res);
	}
	PTHREAD_MUTEX_destroy(&dv->mtx);
	pool_free(dupreq_pool, dv);
}

/**
 * @brief get a ref count on dupreq_entry_t
 */
static inline void dupreq_entry_get(dupreq_entry_t *dv)
{
	(void)atomic_inc_uint32_t(&dv->refcnt);
}

/**
 * @brief release a ref count on dupreq_entry_t
 *
 * The caller must not access dv any more after this call as it could be
 * freed here.
 */
static inline void dupreq_entry_put(dupreq_entry_t *dv)
{
	int32_t refcnt;

	refcnt = atomic_dec_uint32_t(&dv->refcnt);

	/* If ref count is zero, no one should be accessing it other
	 * than us.  so no lock is needed.
	 */
	if (refcnt == 0)
		nfs_dupreq_free_dupreq(dv);
}

/**
 * @page DRC_RETIRE DRC request retire heuristic.
 *
 * We add a new, per-drc semphore like counter, retwnd.  The value of
 * retwnd begins at 0, and is always >= 0.  The value of retwnd is increased
 * when a a duplicate req cache hit occurs.  If it was 0, it is increased by
 * some small constant, say, 16, otherwise, by 1.  And retwnd decreases by 1
 * when we successfully finish any request.  Likewise in finish, a cached
 * request may be retired iff we are above our water mark, and retwnd is 0.
 */

#define RETWND_START_BIAS 16

/**
 * @brief advance retwnd.
 *
 * If (drc)->retwnd is 0, advance its value to RETWND_START_BIAS, else
 * increase its value by 2 (corrects to 1) iff !full.
 *
 * @param[in] drc The duplicate request cache
 */
#define drc_inc_retwnd(drc)					\
	do {							\
		if ((drc)->retwnd == 0)				\
			(drc)->retwnd = RETWND_START_BIAS;	\
		else						\
			if ((drc)->retwnd < (drc)->maxsize)	\
				(drc)->retwnd += 2;		\
	} while (0)

/**
 * @brief conditionally decrement retwnd.
 *
 * If (drc)->retwnd > 0, decrease its value by 1.
 *
 * @param[in] drc The duplicate request cache
 */
#define drc_dec_retwnd(drc)			\
	do {					\
		if ((drc)->retwnd > 0)		\
			--((drc)->retwnd);	\
	} while (0)

/**
 * @brief retire request predicate.
 *
 * Calculate whether a request may be retired from the provided duplicate
 * request cache.
 *
 * @param[in] drc The duplicate request cache
 *
 * @return true if a request may be retired, else false.
 */
static inline bool drc_should_retire(drc_t *drc)
{
	/* do not exeed the hard bound on cache size */
	if (unlikely(drc->size > drc->maxsize))
		return true;

	/* otherwise, are we permitted to retire requests */
	if (unlikely(drc->retwnd > 0))
		return false;

	/* finally, retire if drc->size is above intended high water mark */
	if (unlikely(drc->size > drc->hiwat))
		return true;

	return false;
}

static inline bool nfs_dupreq_v4_cacheable(nfs_request_t *reqnfs)
{
	COMPOUND4args *arg_c4 = (COMPOUND4args *)&reqnfs->arg_nfs;

	if (arg_c4->minorversion > 0)
		return false;
	if ((reqnfs->lookahead.flags & (NFS_LOOKAHEAD_CREATE)))
		/* override OPEN4_CREATE */
		return true;
	if ((reqnfs->lookahead.flags &
	     (NFS_LOOKAHEAD_OPEN | /* all logical OPEN */
	      NFS_LOOKAHEAD_CLOSE | NFS_LOOKAHEAD_LOCK | /* includes LOCKU */
	      NFS_LOOKAHEAD_READ | /* because large, though idempotent */
	      NFS_LOOKAHEAD_READLINK |
	      NFS_LOOKAHEAD_READDIR)))
		return false;
	return true;
}

/**
 * @brief Start a duplicate request transaction
 *
 * Finds any matching request entry in the cache, if one exists, else
 * creates one in the START state.  On any non-error return, the refcnt
 * of the corresponding entry is incremented.
 *
 * @param[in] reqnfs  The NFS request data
 * @param[in] req     The request to be cached
 *
 * @retval DUPREQ_SUCCESS if successful.
 * @retval DUPREQ_INSERT_MALLOC_ERROR if an error occured during insertion.
 */
dupreq_status_t nfs_dupreq_start(nfs_request_t *reqnfs,
				 struct svc_req *req)
{
	dupreq_entry_t *dv = NULL, *dk = NULL;
	drc_t *drc;
	dupreq_status_t status = DUPREQ_SUCCESS;

	if (!(reqnfs->funcdesc->dispatch_behaviour & CAN_BE_DUP))
		goto no_cache;

	if (nfs_param.core_param.drc.disabled)
		goto no_cache;

	if (reqnfs->funcdesc->service_function == nfs4_Compound
	 && !nfs_dupreq_v4_cacheable(reqnfs)) {
		/* For such requests, we merely thread the request
		 * through for later cleanup.  All v41 caching is
		 * handled by the v41 slot reply cache.
		 */
		goto no_cache;
	}

	drc = nfs_dupreq_get_drc(req);
	dk = alloc_dupreq();
	dk->hin.drc = drc;	/* trans. call path ref to dv */

	switch (drc->type) {
	case DRC_TCP_V4:
	case DRC_TCP_V3:
		dk->hin.tcp.rq_xid = req->rq_msg.rm_xid;
		/* XXX needed? */
		dk->hin.rq_prog = req->rq_msg.cb_prog;
		dk->hin.rq_vers = req->rq_msg.cb_vers;
		dk->hin.rq_proc = req->rq_msg.cb_proc;
		break;
	case DRC_UDP_V234:
		dk->hin.tcp.rq_xid = req->rq_msg.rm_xid;
		if (unlikely(!copy_xprt_addr(&dk->hin.addr, req->rq_xprt))) {
			nfs_dupreq_put_drc(drc, DRC_FLAG_NONE);
			nfs_dupreq_free_dupreq(dk);
			return DUPREQ_INSERT_MALLOC_ERROR;
		}
		dk->hin.rq_prog = req->rq_msg.cb_prog;
		dk->hin.rq_vers = req->rq_msg.cb_vers;
		dk->hin.rq_proc = req->rq_msg.cb_proc;
		break;
	default:
		/* @todo: should this be an assert? */
		nfs_dupreq_put_drc(drc, DRC_FLAG_NONE);
		nfs_dupreq_free_dupreq(dk);
		return DUPREQ_INSERT_MALLOC_ERROR;
	}

	dk->hk = req->rq_cksum; /* TI-RPC computed checksum */
	dk->state = DUPREQ_START;
	dk->timestamp = time(NULL);

	{
		struct opr_rbtree_node *nv;
		struct rbtree_x_part *t =
		    rbtx_partition_of_scalar(&drc->xt, dk->hk);
		PTHREAD_MUTEX_lock(&t->mtx);	/* partition lock */
		nv = rbtree_x_cached_lookup(&drc->xt, t, &dk->rbt_k, dk->hk);
		if (nv) {
			/* cached request */
			nfs_dupreq_free_dupreq(dk);
			dv = opr_containerof(nv, dupreq_entry_t, rbt_k);
			PTHREAD_MUTEX_lock(&dv->mtx);
			if (unlikely(dv->state == DUPREQ_START)) {
				status = DUPREQ_BEING_PROCESSED;
			} else {
				/* satisfy req from the DRC, incref,
				   extend window */
				req->rq_u1 = dv;
				reqnfs->res_nfs = req->rq_u2 = dv->res;
				status = DUPREQ_EXISTS;
				dupreq_entry_get(dv);
			}
			PTHREAD_MUTEX_unlock(&dv->mtx);

			if (status == DUPREQ_EXISTS) {
				PTHREAD_MUTEX_lock(&drc->mtx);
				drc_inc_retwnd(drc);
				PTHREAD_MUTEX_unlock(&drc->mtx);
			}

			LogDebug(COMPONENT_DUPREQ,
				 "dupreq hit dv=%p, dv xid=%" PRIu32
				 " cksum %" PRIu64 " state=%s",
				 dv, dv->hin.tcp.rq_xid, dv->hk,
				 dupreq_state_table[dv->state]);
		} else {
			/* new request */
			req->rq_u1 = dk;
			dk->res = alloc_nfs_res();
			reqnfs->res_nfs = req->rq_u2 = dk->res;

			/* cache--can exceed drc->maxsize */
			(void)rbtree_x_cached_insert(&drc->xt, t,
						&dk->rbt_k, dk->hk);

			/* dupreq ref count starts with 2; one for the caller
			 * and another for staying in the hash table.
			 */
			dk->refcnt = 2;

			/* add to q tail */
			PTHREAD_MUTEX_lock(&drc->mtx);
			TAILQ_INSERT_TAIL(&drc->dupreq_q, dk, fifo_q);
			++(drc->size);
			PTHREAD_MUTEX_unlock(&drc->mtx);

			LogFullDebug(COMPONENT_DUPREQ,
				     "starting dk=%p xid=%" PRIu32
				     " on DRC=%p state=%s, status=%s, refcnt=%d, drc->size=%d",
				     dk, dk->hin.tcp.rq_xid, drc,
				     dupreq_state_table[dk->state],
				     dupreq_status_table[status],
				     dk->refcnt, drc->size);
		}
		PTHREAD_MUTEX_unlock(&t->mtx);
	}

	return status;

no_cache:
	req->rq_u1 = (void *)DUPREQ_NOCACHE;
	reqnfs->res_nfs = req->rq_u2 = alloc_nfs_res();
	return DUPREQ_SUCCESS;
}

/**
 * @brief Completes a request in the cache
 *
 * Completes a cache insertion operation begun in nfs_dupreq_start.
 * The refcnt of the corresponding duplicate request entry is unchanged
 * (ie, the caller must still call nfs_dupreq_rele).
 *
 * In contrast with the prior DRC implementation, completing a request
 * in the current implementation may under normal conditions cause one
 * or more cached requests to be retired.  Requests are retired in the
 * order they were inserted.  The primary retire algorithm is a high
 * water mark, and a windowing heuristic.  One or more requests will be
 * retired if the water mark/timeout is exceeded, and if a no duplicate
 * requests have been found in the cache in a configurable window of
 * immediately preceding requests.  A timeout may supplement the water mark,
 * in future.
 *
 * req->rq_u1 has either a magic value, or points to a duplicate request
 * cache entry allocated in nfs_dupreq_start.
 *
 * @param[in] req     The request
 * @param[in] res_nfs The response
 *
 * @return DUPREQ_SUCCESS if successful.
 * @return DUPREQ_INSERT_MALLOC_ERROR if an error occured.
 */
dupreq_status_t nfs_dupreq_finish(struct svc_req *req, nfs_res_t *res_nfs)
{
	dupreq_entry_t *ov = NULL, *dv = (dupreq_entry_t *)req->rq_u1;
	dupreq_status_t status = DUPREQ_SUCCESS;
	struct rbtree_x_part *t;
	drc_t *drc = NULL;
	int16_t cnt = 0;

	/* do nothing if req is marked no-cache */
	if (dv == (void *)DUPREQ_NOCACHE)
		goto out;

	PTHREAD_MUTEX_lock(&dv->mtx);
	dv->res = res_nfs;
	dv->timestamp = time(NULL);
	dv->state = DUPREQ_COMPLETE;
	drc = dv->hin.drc;
	PTHREAD_MUTEX_unlock(&dv->mtx);

	/* cond. remove from q head */
	PTHREAD_MUTEX_lock(&drc->mtx);

	LogFullDebug(COMPONENT_DUPREQ,
		     "completing dv=%p xid=%" PRIu32
		     " on DRC=%p state=%s, status=%s, refcnt=%d, drc->size=%d",
		dv, dv->hin.tcp.rq_xid, drc,
		dupreq_state_table[dv->state], dupreq_status_table[status],
		dv->refcnt, drc->size);

	/* (all) finished requests count against retwnd */
	drc_dec_retwnd(drc);

	/* conditionally retire entries */
dq_again:
	if (drc_should_retire(drc)) {
		ov = TAILQ_FIRST(&drc->dupreq_q);
		if (likely(ov)) {
			/* remove dict entry */
			t = rbtx_partition_of_scalar(&drc->xt, ov->hk);
			uint64_t ov_hk = ov->hk;

			/* Need to acquire partition lock, but the lock
			 * order is partition lock followed by drc lock.
			 * Drop drc lock and reacquire it!
			 */
			PTHREAD_MUTEX_unlock(&drc->mtx);
			PTHREAD_MUTEX_lock(&t->mtx);	/* partition lock */
			PTHREAD_MUTEX_lock(&drc->mtx);

			/* Since we dropped drc lock and reacquired it,
			 * the drc dupreq list may have changed. Get the
			 * dupreq entry from the list again.
			 */
			ov = TAILQ_FIRST(&drc->dupreq_q);

			/* Make sure that we are removing the entry we
			 * expected (imperfect, but harmless).
			 */
			if (ov == NULL || ov->hk != ov_hk) {
				PTHREAD_MUTEX_unlock(&t->mtx);
				goto unlock;
			}

			/* remove q entry */
			TAILQ_REMOVE(&drc->dupreq_q, ov, fifo_q);
			--(drc->size);
			/* release dv's ref */
			nfs_dupreq_put_drc(drc, DRC_FLAG_LOCKED);
			/* drc->mtx gets unlocked in the above call! */

			rbtree_x_cached_remove(&drc->xt, t, &ov->rbt_k, ov->hk);

			PTHREAD_MUTEX_unlock(&t->mtx);

			LogDebug(COMPONENT_DUPREQ,
				 "retiring ov=%p xid=%" PRIu32
				 " on DRC=%p state=%s, status=%s, refcnt=%d",
				 ov, ov->hin.tcp.rq_xid,
				 ov->hin.drc, dupreq_state_table[dv->state],
				 dupreq_status_table[status], ov->refcnt);

			/* release hashtable ref count */
			dupreq_entry_put(ov);

			/* conditionally retire another */
			if (cnt++ < DUPREQ_MAX_RETRIES) {
				PTHREAD_MUTEX_lock(&drc->mtx);
				goto dq_again; /* calls drc_should_retire() */
			}
			goto out;
		}
	}

 unlock:
	PTHREAD_MUTEX_unlock(&drc->mtx);

 out:
	return status;
}

/**
 *
 * @brief Remove an entry (request) from a duplicate request cache.
 *
 * The expected pattern is that nfs_rpc_process_request shall delete requests
 * only in error conditions.  The refcnt of the corresponding duplicate request
 * entry is unchanged (ie., the caller must still call nfs_dupreq_rele).
 *
 * We assert req->rq_u1 now points to the corresonding duplicate request
 * cache entry.
 *
 * @param[in] req The svc_req structure.
 *
 * @return DUPREQ_SUCCESS if successful.
 *
 */
dupreq_status_t nfs_dupreq_delete(struct svc_req *req)
{
	dupreq_entry_t *dv = (dupreq_entry_t *)req->rq_u1;
	dupreq_status_t status = DUPREQ_SUCCESS;
	struct rbtree_x_part *t;
	drc_t *drc;

	/* do nothing if req is marked no-cache */
	if (dv == (void *)DUPREQ_NOCACHE)
		goto out;

	PTHREAD_MUTEX_lock(&dv->mtx);
	drc = dv->hin.drc;
	dv->state = DUPREQ_DELETED;
	PTHREAD_MUTEX_unlock(&dv->mtx);

	LogFullDebug(COMPONENT_DUPREQ,
		     "deleting dv=%p xid=%" PRIu32
		     " on DRC=%p state=%s, status=%s, refcnt=%d",
		     dv, dv->hin.tcp.rq_xid, drc,
		     dupreq_state_table[dv->state], dupreq_status_table[status],
		     dv->refcnt);

	/* XXX dv holds a ref on drc */
	t = rbtx_partition_of_scalar(&drc->xt, dv->hk);

	PTHREAD_MUTEX_lock(&t->mtx);
	rbtree_x_cached_remove(&drc->xt, t, &dv->rbt_k, dv->hk);
	PTHREAD_MUTEX_unlock(&t->mtx);

	PTHREAD_MUTEX_lock(&drc->mtx);

	TAILQ_REMOVE(&drc->dupreq_q, dv, fifo_q);
	--(drc->size);

	/* release dv's ref on drc and unlock */
	nfs_dupreq_put_drc(drc, DRC_FLAG_LOCKED);
	/* !LOCKED */

	/* we removed the dupreq from hashtable, release a ref */
	dupreq_entry_put(dv);

 out:
	return status;
}

/**
 * @brief Decrement the call path refcnt on a cache entry.
 *
 * We assert req->rq_u1 now points to the corresonding duplicate request
 * cache entry (dv).
 *
 * @param[in] req  The svc_req structure.
 * @param[in] func The function descriptor for this request type
 */
void nfs_dupreq_rele(struct svc_req *req, const nfs_function_desc_t *func)
{
	dupreq_entry_t *dv = (dupreq_entry_t *) req->rq_u1;

	/* no-cache cleanup */
	if (dv == (void *)DUPREQ_NOCACHE) {
		LogFullDebug(COMPONENT_DUPREQ, "releasing no-cache res %p",
			     req->rq_u2);
		func->free_function(req->rq_u2);
		free_nfs_res(req->rq_u2);
		goto out;
	}

	LogFullDebug(COMPONENT_DUPREQ,
		     "releasing dv=%p xid=%" PRIu32
		     " on DRC=%p state=%s, refcnt=%d",
		     dv, dv->hin.tcp.rq_xid, dv->hin.drc,
		     dupreq_state_table[dv->state], dv->refcnt);

	dupreq_entry_put(dv);

 out:
	/* dispose RPC header */
	if (req->rq_auth)
		SVCAUTH_RELEASE(req);
}

/**
 * @brief Shutdown the dupreq2 package.
 */
void dupreq2_pkgshutdown(void)
{
	/* XXX do nothing */
}
