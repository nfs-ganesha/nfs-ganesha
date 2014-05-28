/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/* XXX prune: */
#include "log.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"

#include "nfs_dupreq.h"
#include "murmur3.h"
#include "abstract_mem.h"
#include "wait_queue.h"

#define DUPREQ_BAD_ADDR1 0x01 /* safe for marked pointers, etc */
#define DUPREQ_NOCACHE   0x02

pool_t *dupreq_pool;
pool_t *nfs_res_pool;
pool_t *tcp_drc_pool; /* pool of per-connection DRC objects */

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

struct drc_st
{
    pthread_mutex_t mtx;
    drc_t udp_drc; /* shared DRC */
    struct rbtree_x tcp_drc_recycle_t;
    TAILQ_HEAD(drc_st_tailq, drc) tcp_drc_recycle_q; /* fifo */
    int32_t tcp_drc_recycle_qlen;
    time_t last_expire_check;
    uint32_t expire_delta;
};

static struct drc_st *drc_st;

/**
 * @brief Comparison function for duplicate request entries.
 *
 * @return Nothing.
 */
static inline int
uint32_cmpf(uint32_t lhs, uint32_t rhs)
{
    if (lhs < rhs)
        return (-1);

    if (lhs == rhs)
        return 0;

    return (1);
}

/**
 * @brief Comparison function for entries in a shared DRC
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int
dupreq_shared_cmpf(const struct opr_rbtree_node *lhs,
                   const struct opr_rbtree_node *rhs)
{
    dupreq_entry_t *lk, *rk;

    lk = opr_containerof(lhs, dupreq_entry_t, rbt_k);
    rk = opr_containerof(rhs, dupreq_entry_t, rbt_k);

    switch (sockaddr_cmpf(&lk->hin.addr, &rk->hin.addr, CHECK_PORT)) {
    case -1:
        return -1;
        break;
    case 0:
        switch (uint32_cmpf(lk->hin.tcp.rq_xid, rk->hin.tcp.rq_xid)) {
        case -1:
            return (-1);
            break;
        case 0:
            if (lk->hin.drc->flags & DRC_FLAG_HASH) {
                return (memcmp((char *) lk->hk,
                               (char *) rk->hk,
                               sizeof(uint64_t) * 2));
            } else
                return (0);
            break;
        default:
            break;
        } /* xid */
        break;
    default:
            break;
    } /* addr+port */

    return (1);
}

/**
 * @brief Comparison function for entries in a per-connection (TCP) DRC
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int
dupreq_tcp_cmpf(const struct opr_rbtree_node *lhs,
                const struct opr_rbtree_node *rhs)
{
    dupreq_entry_t *lk, *rk;

    lk = opr_containerof(lhs, dupreq_entry_t, rbt_k);
    rk = opr_containerof(rhs, dupreq_entry_t, rbt_k);

    if (lk->hin.tcp.rq_xid < rk->hin.tcp.rq_xid)
        return (-1);

    if (lk->hin.tcp.rq_xid == rk->hin.tcp.rq_xid) {
        if (lk->hin.drc->flags & DRC_FLAG_HASH) {
            return (memcmp((char *) lk->hk,
                           (char *) rk->hk,
                           sizeof(uint64_t) * 2));
        } else
            return (0);
    }

    return (1);
}

/**
 * @brief Comparison function for recycled per-connection (TCP) DRCs
 *
 * @param[in] lhs  Left-hand-side
 * @param[in] rhs  Right-hand-side
 *
 * @return -1,0,1.
 */
static inline int
drc_recycle_cmpf(const struct opr_rbtree_node *lhs,
                 const struct opr_rbtree_node *rhs)
{
    drc_t *lk, *rk;

    lk = opr_containerof(lhs, drc_t, d_u.tcp.recycle_k);
    rk = opr_containerof(rhs, drc_t, d_u.tcp.recycle_k);

    return (sockaddr_cmpf(&lk->d_u.tcp.addr, &rk->d_u.tcp.addr,
			  CHECK_PORT));
}

/**
 * @brief Fill hash buffer from a received NFS request
 *
 * @param[in] nfs_req  The request
 * @param[in] hbuf  A buffer to fill
 * @param[in] size  Number of bytes to fill
 *
 * @return nothing
 */
static inline void
drc_fill_hbuf(nfs_request_data_t *nfs_req, char *hbuf, size_t *size)
{
    struct nfs_request_lookahead dummy_lookahead = {
        .flags = 0,
        .read = 0,
        .write = 0
    };
    XDR xdrs = {
        .x_public = &dummy_lookahead
    };
    xdrmem_create(&xdrs, hbuf, *size, XDR_ENCODE);
    nfs_req->funcdesc->xdr_decode_func(&xdrs, (caddr_t) &nfs_req->arg_nfs);
    *size = XDR_GETPOS(&xdrs);
    XDR_DESTROY(&xdrs);
}

/**
 * @brief Hash function for entries in a shared DRC
 *
 * @param[in] drc  DRC
 * @param[in] arg  The request arguments
 * @param[in] v    The duplicate request entry being hashed
 *
 * The checksum step is conditional on drc->flags.  Note that
 * Oracle DirectNFS and other clients are believed to produce
 * workloads that may fail without checksum support.
 *
 * @return the (definitive) 64-bit hash value as a uint64_t.
 */
static inline uint64_t
drc_shared_hash(drc_t *drc, nfs_request_data_t *nfs_req, dupreq_entry_t *v)
{
    char *hbuf = NULL;
    size_t size = 256;

    if (drc->flags & DRC_FLAG_CKSUM) {
        hbuf = alloca(size);
        drc_fill_hbuf(nfs_req, hbuf, &size);
        MurmurHash3_x64_128(hbuf, size, 911, v->hin.tcp.checksum);
        MurmurHash3_x64_128(&v->hin, sizeof(v->hin), 911, v->hk);
    } else
        MurmurHash3_x64_128(&v->hin,
                            sizeof(v->hin)-sizeof(v->hin.tcp.checksum),
                            911, v->hk);
    return (v->hk[0]);
}

/**
 * @brief Hash function for entries in a per-connection (TCP) DRC
 *
 * @param[in] drc  DRC
 * @param[in] arg  The request arguments
 * @param[in] v    The duplicate request entry being hashed
 *
 * The hash and checksum steps is conditional on drc->flags.  Note
 * that Oracle DirectNFS and other clients are believed to produce
 * workloads that may fail without checksum support.
 *
 * HOWEVER!  We might omit the address component of the hash here,
 * probably should for performance.
 *
 * @return the (not definitive) 64-bit hash value as a uint64_t.
 */
static inline uint64_t
drc_tcp_hash(drc_t *drc, nfs_request_data_t *nfs_req, dupreq_entry_t *v)
{
    char *hbuf = NULL;
    size_t size = 256;

    if (drc->flags & DRC_FLAG_HASH) {
        if (drc->flags & DRC_FLAG_CKSUM) {
            hbuf = alloca(size);
            drc_fill_hbuf(nfs_req, hbuf, &size);
            MurmurHash3_x64_128(hbuf, size, 911, v->hin.tcp.checksum);
            MurmurHash3_x64_128(&v->hin, sizeof(v->hin), 911, v->hk);
            LogFullDebug(COMPONENT_DUPREQ,
                         "hash={%"PRIx64",%"PRIx64"} xid=%u req=%p",
                         v->hk[0], v->hk[1],
                         v->hin.tcp.rq_xid,
                         nfs_req);
        } else
            MurmurHash3_x64_128(&v->hin, sizeof(v->hin), 911, v->hk);
    } else
        v->hk[0] = v->hin.tcp.rq_xid;
    return (v->hk[0]);
}

/**
 * @brief Initialize a shared duplicate request cache
 *
 * @param[in] drc  The cache
 * @param[in] npart Number of concurrent partitions (a number > 1 is suitable
 * for a shared DRC)
 * @param[in] cachesz Number of entries in the closed table (not really a
 * cache)
 *
 * @return Nothing.
 */
static inline void
init_shared_drc()
{
    drc_t *drc = &drc_st->udp_drc;
    int ix, code  __attribute__((unused)) = 0;

    drc->type = DRC_UDP_V234;
    drc->refcnt = 0;
    drc->retwnd = 0;
    drc->d_u.tcp.recycle_time = 0;
    drc->maxsize =  nfs_param.core_param.drc.udp.size;
    drc->cachesz = nfs_param.core_param.drc.udp.cachesz;
    drc->npart = nfs_param.core_param.drc.udp.npart;
    drc->flags = DRC_FLAG_HASH|DRC_FLAG_CKSUM; /* XXX parameterize? */

    gsh_mutex_init(&drc->mtx, NULL);

    /* init dict */
    code = rbtx_init(&drc->xt, dupreq_shared_cmpf, drc->npart,
                     RBT_X_FLAG_ALLOC|RBT_X_FLAG_CACHE_WT);
    assert(! code);

    /* completed requests */
    TAILQ_INIT(&drc->dupreq_q);

    /* init closed-form "cache" partition */
    for (ix = 0; ix < drc->npart; ++ix) {
        struct rbtree_x_part *xp = &(drc->xt.tree[ix]);
        drc->xt.cachesz = drc->cachesz;
        xp->cache = gsh_calloc(drc->cachesz, sizeof(struct opr_rbtree_node *));
        if (unlikely(! xp->cache)) {
            LogCrit(COMPONENT_DUPREQ, "UDP DRC hash partition allocation "
                    "failed (ix=%d)", ix);
            drc->cachesz = 0;
            break;
        }
    }

    return;
}

/**
 * @brief Initialize the DRC package.
 *
 * @return Nothing.
 */
void dupreq2_pkginit(void)
{
    int code __attribute__((unused)) = 0;

    dupreq_pool = pool_init("Duplicate Request Pool",
                            sizeof(dupreq_entry_t),
                            pool_basic_substrate,
                            NULL, NULL, NULL);
    if (unlikely(! (dupreq_pool))) {
        LogCrit(COMPONENT_INIT,
                "Error while allocating duplicate request pool");
        LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
        Fatal();
    }

    nfs_res_pool = pool_init("nfs_res_t pool",
                             sizeof(nfs_res_t),
                             pool_basic_substrate,
                             NULL, NULL, NULL);
    if (unlikely(! (nfs_res_pool))) {
        LogCrit(COMPONENT_INIT,
                "Error while allocating nfs_res_t pool");
        LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
        Fatal();
    }

    tcp_drc_pool = pool_init("TCP DRC Pool",
                             sizeof(drc_t),
                             pool_basic_substrate,
                             NULL, NULL, NULL);
    if (! (dupreq_pool)) {
        LogCrit(COMPONENT_INIT,
                "Error while allocating duplicate request pool");
        LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
        Fatal();
    }

    drc_st = gsh_calloc(1, sizeof(struct drc_st));

    /* init shared statics */
    gsh_mutex_init(&drc_st->mtx, NULL);

    /* recycle_t */
    code = rbtx_init(&drc_st->tcp_drc_recycle_t,
                     drc_recycle_cmpf,
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

extern nfs_function_desc_t nfs3_func_desc[];
extern nfs_function_desc_t nfs4_func_desc[];
extern nfs_function_desc_t mnt1_func_desc[];
extern nfs_function_desc_t mnt3_func_desc[];
extern nfs_function_desc_t nlm4_func_desc[];
extern nfs_function_desc_t rquota1_func_desc[];
extern nfs_function_desc_t rquota2_func_desc[];

/**
 * @brief Determine the protocol of the supplied TI-RPC SVCXPRT*
 *
 * @param[in] xprt  The SVCXPRT
 *
 * @return IPPROTO_UDP or IPPROTO_TCP.
 */
static inline unsigned int
get_ipproto_by_xprt(SVCXPRT *xprt) /* XXX correct, but inelegant */
{
   if( xprt->xp_p2 != NULL )
     return IPPROTO_UDP ;
   else if ( xprt->xp_p1 != NULL )
     return IPPROTO_TCP;
   else
     return IPPROTO_IP ; /* Dummy output */
}

/**
 * @brief Determine the dupreq2 DRC type to handle the supplied svc_req
 *
 * @param[in] req  The svc_req being processed
 *
 * @return a value of type enum_drc_type.
 */
static inline enum drc_type
get_drc_type(struct svc_req *req)
{
    if (get_ipproto_by_xprt(req->rq_xprt) == IPPROTO_UDP)
        return (DRC_UDP_V234);
    else {
        if (req->rq_vers == 4)
            return (DRC_TCP_V4);
    }
    return (DRC_TCP_V3);
}

/**
 * @brief Allocate a duplicate request cache
 *
 * @param[in] dtype  Style DRC to allocate (e.g., TCP, by enum drc_type)
 * @param[in] maxsz  Upper bound on requests to cache
 * @param[in] cachesz  Number of entries in the closed hash partition
 * @param[in] flags  DRC flags
 *
 * @return the drc, if successfully allocated, else NULL.
 */
static inline drc_t *
alloc_tcp_drc(enum drc_type dtype)
{
    drc_t *drc = pool_alloc(tcp_drc_pool, NULL);
    int ix, code  __attribute__((unused)) = 0;

    if (unlikely(! drc)) {
        LogCrit(COMPONENT_DUPREQ, "alloc TCP DRC failed");
        goto out;
    }

    drc->type = dtype; /* DRC_TCP_V3 or DRC_TCP_V4 */
    drc->refcnt = 0;
    drc->retwnd = 0;
    drc->d_u.tcp.recycle_time = 0;
    drc->maxsize =  nfs_param.core_param.drc.tcp.size;
    drc->cachesz = nfs_param.core_param.drc.tcp.cachesz;
    drc->npart = nfs_param.core_param.drc.tcp.npart;
    drc->flags = DRC_FLAG_HASH|DRC_FLAG_CKSUM; /* XXX parameterize */

    pthread_mutex_init(&drc->mtx, NULL);

    /* init dict */
    code = rbtx_init(&drc->xt, dupreq_tcp_cmpf, drc->npart,
                     RBT_X_FLAG_ALLOC|RBT_X_FLAG_CACHE_WT);
    assert(! code);

    /* completed requests */
    TAILQ_INIT(&drc->dupreq_q);

    /* recycling DRC */
    TAILQ_INIT_ENTRY(drc, d_u.tcp.recycle_q);

    /* init "cache" partition */
    for (ix = 0; ix < drc->npart; ++ix) {
        struct rbtree_x_part *xp = &(drc->xt.tree[ix]);
        drc->xt.cachesz = drc->cachesz;
        xp->cache = gsh_calloc(drc->cachesz, sizeof(struct opr_rbtree_node *));
        if (unlikely(! xp->cache)) {
            LogCrit(COMPONENT_DUPREQ, "TCP DRC hash partition allocation "
                    "failed (ix=%d)", ix);
            drc->cachesz = 0;
            break;
        }
    }

out:
    return (drc);
}

/**
 * @brief Deep-free a per-connection (TCP) duplicate request cache
 *
 * @param[in] drc  The DRC to dispose
 *
 * Assumes that the DRC has been allocated from the tcp_drc_pool.
 *
 * @return Nothing.
 */
static inline void
free_tcp_drc(drc_t *drc) {
    if (drc->xt.tree[0].cache)
        gsh_free(drc->xt.tree[0].cache);
    pthread_mutex_destroy(&drc->mtx);
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
static inline uint32_t
nfs_dupreq_ref_drc(drc_t *drc)
{
    return (++(drc->refcnt)); /* locked */
}

/**
 * @brief Decrement the reference count on a DRC
 *
 * @param[in] drc  The DRC to unref
 *
 * @return the new value of refcnt.
 */
static inline uint32_t
nfs_dupreq_unref_drc(drc_t *drc)
{
    return (--(drc->refcnt)); /* locked */
}

#define DRC_ST_LOCK() \
    pthread_mutex_lock(&drc_st->mtx);

#define DRC_ST_UNLOCK() \
    pthread_mutex_unlock(&drc_st->mtx);

/**
 * @brief Check for expired TCP DRCs.
 * *
 * @return Nothing.
 */
static inline void
drc_free_expired(void)
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
        if (drc &&
            (drc->d_u.tcp.recycle_time > 0) &&
            ((now - drc->d_u.tcp.recycle_time) > drc_st->expire_delta) &&
            (drc->refcnt == 0)) {
            LogFullDebug(COMPONENT_DUPREQ, "remove expired drc %p from "
                         "recycle queue",
                         drc);
            t = rbtx_partition_of_scalar(&drc_st->tcp_drc_recycle_t,
                                         drc->d_u.tcp.hk[0]);

            odrc = opr_rbtree_lookup(&t->t, &drc->d_u.tcp.recycle_k);
            if (! odrc) {
                LogCrit(COMPONENT_DUPREQ, "BUG: asked to dequeue DRC not on "
                        " queue");
            } else {
                (void) opr_rbtree_remove(&t->t, &drc->d_u.tcp.recycle_k);
            }
            TAILQ_REMOVE(&drc_st->tcp_drc_recycle_q, drc, d_u.tcp.recycle_q);
            --(drc_st->tcp_drc_recycle_qlen);
	    /* expect DRC to be reachable from some xprt(s) */
	    pthread_mutex_lock(&drc->mtx);
	    drc->flags &= ~DRC_FLAG_RECYCLE;
	    /* but if not, dispose it */
	    if (drc->refcnt == 0) {
	        pthread_mutex_unlock(&drc->mtx);
		free_tcp_drc(drc);
		continue;
	    }
	    pthread_mutex_unlock(&drc->mtx);
        } else {
            LogFullDebug(COMPONENT_DUPREQ, "unexpired drc %p in recycle queue "
                         "expire check (nothing happens)",
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
    gsh_xprt_private_t *xu = (gsh_xprt_private_t *) req->rq_xprt->xp_u1;
    drc_t *drc = NULL;
    bool drc_check_expired = false;

    switch (dtype) {
    case DRC_UDP_V234:
        LogFullDebug(COMPONENT_DUPREQ,
                     "ref shared UDP DRC");
        drc = &(drc_st->udp_drc);
        DRC_ST_LOCK();
        (void) nfs_dupreq_ref_drc(drc);
        DRC_ST_UNLOCK();
        goto out;
        break;
    case DRC_TCP_V4:
    case DRC_TCP_V3:
        pthread_mutex_lock(&req->rq_xprt->xp_lock);
        if (xu->drc) {
            drc = xu->drc;
            LogFullDebug(COMPONENT_DUPREQ,
                         "ref DRC=%p for xprt=%p", drc, req->rq_xprt);
            pthread_mutex_lock(&drc->mtx); /* LOCKED */
        } else {
            drc_t drc_k;
            struct rbtree_x_part *t = NULL;
            struct opr_rbtree_node *ndrc = NULL;
            drc_t *tdrc = NULL;

            memset(&drc_k, 0, sizeof(drc_k));

            drc_k.type = dtype;
            (void) copy_xprt_addr(&drc_k.d_u.tcp.addr, req->rq_xprt);
            MurmurHash3_x64_128(&drc_k.d_u.tcp.addr, sizeof(sockaddr_t),
                                911, &drc_k.d_u.tcp.hk);


	    {
		char str[512];
		sprint_sockaddr(&drc_k.d_u.tcp.addr, str, 512);
		LogFullDebug(COMPONENT_DUPREQ, "get drc for addr: %s",
			     str);
	    }

            t = rbtx_partition_of_scalar(&drc_st->tcp_drc_recycle_t,
                                         drc_k.d_u.tcp.hk[0]);
            DRC_ST_LOCK();
            ndrc = opr_rbtree_lookup(&t->t, &drc_k.d_u.tcp.recycle_k);
            if (ndrc) {
                /* reuse old DRC */
                tdrc = opr_containerof(ndrc, drc_t, d_u.tcp.recycle_k);
                pthread_mutex_lock(&tdrc->mtx); /* LOCKED */
		if (tdrc->flags & DRC_FLAG_RECYCLE) {
			TAILQ_REMOVE(&drc_st->tcp_drc_recycle_q, tdrc,
				     d_u.tcp.recycle_q);
			--(drc_st->tcp_drc_recycle_qlen);
			tdrc->flags &= ~DRC_FLAG_RECYCLE;
		}
                drc = tdrc;
                LogFullDebug(COMPONENT_DUPREQ,
                             "recycle TCP DRC=%p for xprt=%p", tdrc,
                             req->rq_xprt);
            }
            if (! drc) {
                drc = alloc_tcp_drc(dtype);
                LogFullDebug(COMPONENT_DUPREQ,
                             "alloc new TCP DRC=%p for xprt=%p", drc,
                             req->rq_xprt);
                /* assign addr */
                memcpy(&drc->d_u.tcp.addr, &drc_k.d_u.tcp.addr,
                       sizeof(sockaddr_t));
                /* assign already-computed hash */
                memcpy(drc->d_u.tcp.hk, drc_k.d_u.tcp.hk,
                       sizeof(uint64_t) * 2);
                pthread_mutex_lock(&drc->mtx); /* LOCKED */
		/* xprt ref */
		drc->refcnt = 1;
		/* insert dict */
		opr_rbtree_insert(&t->t, &drc->d_u.tcp.recycle_k);
            }
            DRC_ST_UNLOCK();
            drc->d_u.tcp.recycle_time = 0;
            /* xprt drc */
            (void) nfs_dupreq_ref_drc(drc); /* xu ref */

	    /* try to expire unused DRCs somewhat in proportion to
	     * new connection arrivals */
	    drc_check_expired = true;

            LogFullDebug(COMPONENT_DUPREQ, "after ref drc %p refcnt==%u ",
                         drc, drc->refcnt);

            xu->drc = drc;
        }
        pthread_mutex_unlock(&req->rq_xprt->xp_lock);
        break;
    default:
        /* XXX error */
        break;
    }

    /* call path ref */
    (void) nfs_dupreq_ref_drc(drc);
    pthread_mutex_unlock(&drc->mtx);

    if (drc_check_expired)
	    drc_free_expired();

out:
    return (drc);
}

/**
 * @brief Release previously-ref'd DRC.
 *
 * Release previously-ref'd DRC.  If its refcnt drops to 0, the DRC
 * is queued for later recycling.
 *
 * @param[in] xprt  The SVCXPRT associated with DRC, if applicable
 * @param[in] drc  The DRC.
 *
 * @return Nothing.
 */
void
nfs_dupreq_put_drc(SVCXPRT *xprt, drc_t *drc, uint32_t flags)
{
    if (! (flags & DRC_FLAG_LOCKED))
        pthread_mutex_lock(&drc->mtx);
    /* drc LOCKED */

    if (drc->refcnt == 0) {
        LogCrit(COMPONENT_DUPREQ, "drc %p refcnt will underrun "
                "refcnt=%u",
                drc, drc->refcnt);
    }

    nfs_dupreq_unref_drc(drc);

    LogFullDebug(COMPONENT_DUPREQ, "drc %p refcnt==%u",
                 drc, drc->refcnt);

    switch (drc->type) {
    case DRC_UDP_V234:
        /* do nothing */
        break;
    case DRC_TCP_V4:
    case DRC_TCP_V3:
        if (drc->refcnt == 0) {
	    if (! (drc->flags & DRC_FLAG_RECYCLE)) {
	         /* note t's lock order wrt drc->mtx is the opposite
		  * of drc->xt[*].lock */
		 drc->d_u.tcp.recycle_time = time(NULL);
		 DRC_ST_LOCK();
		 TAILQ_INSERT_TAIL(&drc_st->tcp_drc_recycle_q, drc,
				   d_u.tcp.recycle_q);
		 ++(drc_st->tcp_drc_recycle_qlen);
		 drc->flags |= DRC_FLAG_RECYCLE;
		 LogFullDebug(COMPONENT_DUPREQ, "enqueue drc %p for recycle",
			      drc);
		 DRC_ST_UNLOCK();
	    }
        }
    default:
        break;
    };

    pthread_mutex_unlock(&drc->mtx); /* !LOCKED */

    return;
}

/**
 * @brief Resolve an indirect request function vector for the supplied DRC entry
 *
 * @param[in] dv  The duplicate request entry.
 *
 * @return The function vector if successful, else NULL.
 */
static inline nfs_function_desc_t*
nfs_dupreq_func(dupreq_entry_t *dv)
{
    nfs_function_desc_t *func = NULL;

    if(dv->hin.rq_prog == nfs_param.core_param.program[P_NFS]) {
        switch (dv->hin.rq_vers) {
        case NFS_V3:
            func = &nfs3_func_desc[dv->hin.rq_proc];
            break;
        case NFS_V4:
            func = &nfs4_func_desc[dv->hin.rq_proc];
          break;
        default:
            /* not reached */
            LogMajor(COMPONENT_DUPREQ,
                     "NFS Protocol version %d unknown",
                     (int) dv->hin.rq_vers);
        }
    }
    else if(dv->hin.rq_prog == nfs_param.core_param.program[P_MNT]) {
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
                     "MOUNT Protocol version %d unknown",
                     (int) dv->hin.rq_vers);
          break;
        }
    }
#ifdef _USE_NLM
    else if(dv->hin.rq_prog == nfs_param.core_param.program[P_NLM]) {
        switch (dv->hin.rq_vers) {
        case NLM4_VERS:
            func = &nlm4_func_desc[dv->hin.rq_proc];
            break;
        }
    }
#endif
#ifdef _USE_RQUOTA
    else if(dv->hin.rq_prog == nfs_param.core_param.program[P_RQUOTA]) {
        switch (dv->hin.rq_vers) {
        case RQUOTAVERS:
            func = &rquota1_func_desc[dv->hin.rq_proc];
            break;
        case EXT_RQUOTAVERS:
            func = &rquota2_func_desc[dv->hin.rq_proc];
          break;
        }
    }
#endif
    else {
        /* not reached */
        LogMajor(COMPONENT_DUPREQ,
                 "protocol %d is not managed",
                 (int) dv->hin.rq_prog);
    }

    return (func);
}

/**
 * @brief Construct a duplicate request cache entry.
 *
 * Entries are allocated from the dupreq_pool.  Since dupre_entry_t
 * presently contains an expanded nfs_arg_t, zeroing of at least corresponding
 * value pointers is required for XDR allocation.
 *
 * @return Nothing.
 */
static inline dupreq_entry_t *
alloc_dupreq(void)
{
    dupreq_entry_t *dv;

    dv = pool_alloc(dupreq_pool, NULL);
    if (! dv) {
        LogCrit(COMPONENT_DUPREQ, "alloc dupreq_entry_t failed");
        goto out;
    }
    memset(dv, 0, sizeof(dupreq_entry_t)); /* XXX pool_zalloc */
    gsh_mutex_init(&dv->mtx, NULL);
    TAILQ_INIT_ENTRY(dv, fifo_q)
out:
    return (dv);
}

/**
 * @brief Deep-free a duplicate request cache entry.
 *
 * If the entry has processed request data, the corresponding free
 * function is called on the result.  The cache entry is then returned
 * to the dupreq_pool.
 *
 * @return Nothing.
 */
static inline void
nfs_dupreq_free_dupreq(dupreq_entry_t *dv)
{
    nfs_function_desc_t *func;

    if (dv->res) {
        func = nfs_dupreq_func(dv);
        func->free_function(dv->res);
        free_nfs_res(dv->res);
    }
    pthread_mutex_destroy(&dv->mtx);
    pool_free(dupreq_pool, dv);
}

/*
 * DRC request retire heuristic.
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
 * increase its value by 1.
 *
 * @param drc [IN] The duplicate request cache
 *
 * @return Nothing.
 */
#define drc_inc_retwnd(drc) \
    do { \
    if ((drc)->retwnd == 0) \
        (drc)->retwnd = RETWND_START_BIAS; \
    else \
        ++((drc)->retwnd); \
    } while (0);

/**
 * @brief conditionally decrement retwnd.
 *
 * If (drc)->retwnd > 0, decrease its value by 1.
 *
 * @param drc [IN] The duplicate request cache
 *
 * @return Nothing.
 */
#define drc_dec_retwnd(drc) \
    do { \
    if ((drc)->retwnd > 0) \
        --((drc)->retwnd); \
    } while (0);

/**
 * @brief retire request predicate.
 *
 * Calculate whether a request may be retired from the provided duplicate
 * request cache.
 *
 * @param drc [IN] The duplicate request cache
 *
 * @return true if a request may be retired, else false.
 */
static inline bool
drc_should_retire(drc_t *drc)
{
    /* do not exeed the hard bound on cache size */
    if (unlikely(drc->size > drc->maxsize))
        return (true);

    /* otherwise, are we permitted to retire requests */
    if (unlikely(drc->retwnd > 0))
        return (false);

    /* finally, retire if drc->size is above intended high water mark */
    if (unlikely((drc)->size > drc->hiwat))
        return (true);

    return (false);
}

static inline bool
nfs_dupreq_v4_cacheable(nfs_request_data_t *nfs_req)
{
    COMPOUND4args *arg_c4 = (COMPOUND4args *) &nfs_req->arg_nfs;
    if (arg_c4->minorversion > 0)
        return (false);
    if ((nfs_req->lookahead.flags & (
		 NFS_LOOKAHEAD_CREATE /* override OPEN4_CREATE */)))
	    return (true );
    if ((nfs_req->lookahead.flags & (
		 NFS_LOOKAHEAD_OPEN | /* all logical OPEN */
		 NFS_LOOKAHEAD_CLOSE |
		 NFS_LOOKAHEAD_LOCK | /* includes LOCKU */
		 NFS_LOOKAHEAD_READ | /* because large, though idempotent */
		 NFS_LOOKAHEAD_READDIR)))
	    return (false);
    return (true);
}

/**
 *
 * nfs_dupreq_start: start a duplicate request transaction
 *
 * Finds any matching request entry in the cache, if one exists, else
 * creates one in the START state.  On any non-error return, the refcnt
 * of the corresponding entry is incremented.
 *
 * @param req [IN] the request to be cached
 * @param arg [IN] pointer to the called-with arguments
 * @param res_nfs [IN] pointer to the result to cache
 *
 * @return DUPREQ_SUCCESS if successful.
 * @return DUPREQ_INSERT_MALLOC_ERROR if an error occured during insertion.
 *
 */
dupreq_status_t
nfs_dupreq_start(nfs_request_data_t *nfs_req, struct svc_req *req)
{
    dupreq_status_t status = DUPREQ_SUCCESS;
    dupreq_entry_t *dv, *dk = NULL;
    bool release_dk = true;
    nfs_res_t *res = NULL;
    drc_t *drc;

    /* Disabled? */
    if (nfs_param.core_param.drc.disabled) {
        req->rq_u1 = (void*) DUPREQ_NOCACHE;
        res = alloc_nfs_res();
        goto out;
    }

    req->rq_u1 = (void*) DUPREQ_BAD_ADDR1;
    req->rq_u2 = (void*) DUPREQ_BAD_ADDR1;

    drc = nfs_dupreq_get_drc(req);
    if (! drc) {
        status = DUPREQ_INSERT_MALLOC_ERROR;
        goto out;
    }

    switch (drc->type) {
    case DRC_TCP_V4:
        if (nfs_req->funcdesc->service_function == nfs4_Compound) {
		if (! nfs_dupreq_v4_cacheable(nfs_req)) {
                /* for such requests, we merely thread the request through
                 * for later cleanup--all v41 caching is handled by the v41
                 * slot reply cache */
                req->rq_u1 = (void*) DUPREQ_NOCACHE;
                res = alloc_nfs_res();
                goto out;
            }
        }
        break;
    default:
        /* likewise for other protocol requests we may not or choose not
         * to cache */
        if (! (nfs_req->funcdesc->dispatch_behaviour & CAN_BE_DUP)) {
            req->rq_u1 = (void*) DUPREQ_NOCACHE;
            res = alloc_nfs_res();
            goto out;
        }
        break;
    }

    dk = alloc_dupreq();
    dk->hin.drc = drc; /* trans. call path ref to dv */

    switch (drc->type) {
    case DRC_TCP_V4:
    case DRC_TCP_V3:
        dk->hin.tcp.rq_xid = req->rq_xid;
        /* XXX needed? */
        dk->hin.rq_prog = req->rq_prog;
        dk->hin.rq_vers = req->rq_vers;
        dk->hin.rq_proc = req->rq_proc;
        break;
    case DRC_UDP_V234:
        dk->hin.tcp.rq_xid = req->rq_xid;
        if (unlikely(! copy_xprt_addr(&dk->hin.addr, req->rq_xprt))) {
            status = DUPREQ_INSERT_MALLOC_ERROR;
            goto release_dk;
        }
        dk->hin.rq_prog = req->rq_prog;
        dk->hin.rq_vers = req->rq_vers;
        dk->hin.rq_proc = req->rq_proc;
        break;
    default:
        /* error */
        status = DUPREQ_ERROR;
        goto release_dk;
        break;
    }

    switch (drc->type) {
    case DRC_UDP_V234:
        (void) drc_shared_hash(drc, nfs_req, dk);
        break;
    case DRC_TCP_V3:
    case DRC_TCP_V4:
        (void) drc_tcp_hash(drc, nfs_req, dk);
        break;
    default:
        /* error */
        status = DUPREQ_ERROR;
        goto release_dk;
        break;
    }

    dk->state = DUPREQ_START;
    dk->timestamp = time(NULL);

    LogFullDebug(COMPONENT_DUPREQ,
                 "alloc new dupreq entry dk=%p, dk xid=%u state=%s",
                 dk, dk->hin.tcp.rq_xid,
                 dupreq_state_table[dk->state]);

    {
        struct opr_rbtree_node *nv;
        struct rbtree_x_part *t =
            rbtx_partition_of_scalar(&drc->xt, dk->hk[0]);
        pthread_mutex_lock(&t->mtx); /* partition lock */
        nv = rbtree_x_cached_lookup(&drc->xt, t, &dk->rbt_k, dk->hk[0]);
        if (nv) {
            /* cached request */
            dv = opr_containerof(nv, dupreq_entry_t, rbt_k);
            pthread_mutex_lock(&dv->mtx);
            if (unlikely(dv->state == DUPREQ_START)) {
                status = DUPREQ_BEING_PROCESSED;
            } else {
                /* satisfy req from the DRC, incref, extend window */
                res = dv->res;
                pthread_mutex_lock(&drc->mtx);
                drc_inc_retwnd(drc);
                pthread_mutex_unlock(&drc->mtx);
                status = DUPREQ_EXISTS;
                (dv->refcnt)++;
            }
            req->rq_u1 = dv;
            pthread_mutex_unlock(&dv->mtx);
        } else {
            /* new request */
            res = req->rq_u2 = dk->res = alloc_nfs_res();
            (void) rbtree_x_cached_insert(&drc->xt, t, &dk->rbt_k, dk->hk[0]);
            (dk->refcnt)++;
            /* add to q tail */
            pthread_mutex_lock(&drc->mtx);
            TAILQ_INSERT_TAIL(&drc->dupreq_q, dk, fifo_q);
            ++(drc->size);
            pthread_mutex_unlock(&drc->mtx);
            req->rq_u1 = dk;
            release_dk = false;
            dv = dk;
        }
        pthread_mutex_unlock(&t->mtx);
    }

    LogFullDebug(COMPONENT_DUPREQ,
                 "starting dv=%p xid=%u on DRC=%p state=%s, status=%s, "
                 "refcnt=%d",
                 dv, dk->hin.tcp.rq_xid, drc, dupreq_state_table[dv->state],
                 dupreq_status_table[status],
                 dv->refcnt);

release_dk:
    if (release_dk)
        nfs_dupreq_free_dupreq(dk);

    nfs_dupreq_put_drc(req->rq_xprt, drc, DRC_FLAG_NONE); /* dk ref */

out:
    if (res)
        nfs_req->res_nfs = req->rq_u2 = res;

    return (status);
}

/**
 *
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
 * @param xid [IN] the transfer id to be used as key
 * @param pnfsreq [IN] the request pointer to cache
 *
 * @return DUPREQ_SUCCESS if successful.
 * @return DUPREQ_INSERT_MALLOC_ERROR if an error occured.
 *
 */
dupreq_status_t
nfs_dupreq_finish(struct svc_req *req,  nfs_res_t *res_nfs)
{
    dupreq_entry_t *ov = NULL, *dv = (dupreq_entry_t *) req->rq_u1;
    dupreq_status_t status = DUPREQ_SUCCESS;
    struct rbtree_x_part *t;
    drc_t *drc = NULL;

   /* do nothing if req is marked no-cache */
    if (dv == (void*) DUPREQ_NOCACHE)
        goto out;

    /* do nothing if nfs_dupreq_start failed completely */
    if (dv == (void*) DUPREQ_BAD_ADDR1)
        goto out;

    pthread_mutex_lock(&dv->mtx);
    dv->res = res_nfs;
    dv->timestamp = time(NULL);
    dv->state = DUPREQ_COMPLETE;
    drc = dv->hin.drc;
    pthread_mutex_unlock(&dv->mtx);

    /* cond. remove from q head */
    pthread_mutex_lock(&drc->mtx);

    LogFullDebug(COMPONENT_DUPREQ,
                 "completing dv=%p xid=%u on DRC=%p state=%s, status=%s, "
                 "refcnt=%d",
                 dv, dv->hin.tcp.rq_xid, drc,
                 dupreq_state_table[dv->state],
                 dupreq_status_table[status],
                 dv->refcnt);

    /* ok, do the new retwnd calculation here.  then, put drc only if
     * we retire an entry */
    if (drc_should_retire(drc)) {
        /* again: */
        ov = TAILQ_FIRST(&drc->dupreq_q);
        if (likely(ov)) {
	    /* finished request count against retwnd */
	    drc_dec_retwnd(drc);
            /* check refcnt */
	    if (ov->refcnt > 0) {
		/* ov still in use, apparently */
                goto unlock;
	    }
            /* remove q entry */
            TAILQ_REMOVE(&drc->dupreq_q, ov, fifo_q);
            --(drc->size); 

            /* remove dict entry */
            t = rbtx_partition_of_scalar(&drc->xt, ov->hk[0]);
	    /* interlock */
	    pthread_mutex_unlock(&drc->mtx);
	    pthread_mutex_lock(&t->mtx); /* partition lock */
            rbtree_x_cached_remove(&drc->xt, t, &ov->rbt_k, ov->hk[0]);
	    pthread_mutex_unlock(&t->mtx);

            LogFullDebug(COMPONENT_DUPREQ,
                         "retiring dv=%p xid=%u on DRC=%p state=%s, "
                         "status=%s, refcnt=%d",
                         ov, ov->hin.tcp.rq_xid, ov->hin.drc,
                         dupreq_state_table[dv->state],
                         dupreq_status_table[status],
                         ov->refcnt);

            /* deep free ov */
            nfs_dupreq_free_dupreq(ov);
            goto out;
        }
    }

unlock:
    pthread_mutex_unlock(&drc->mtx);

out:
    return (status);
}

/**
 *
 * @brief Remove an entry (request) from a duplicate request cache.
 *
 * The expected pattern is that nfs_rpc_execute shall delete requests only
 * in error conditions.  The refcnt of the corresponding duplicate request
 * entry is unchanged (ie., the caller must still call nfs_dupreq_rele).
 *
 * We assert req->rq_u1 now points to the corresonding duplicate request
 * cache entry.
 *
 * @param req [IN] The svc_req structure.
 *
 * @return DUPREQ_SUCCESS if successful.
 *
 */
dupreq_status_t
nfs_dupreq_delete(struct svc_req *req)
{
    dupreq_entry_t *dv = (dupreq_entry_t *) req->rq_u1;
    dupreq_status_t status = DUPREQ_SUCCESS;
    struct rbtree_x_part *t;
    drc_t *drc;

    /* do nothing if req is marked no-cache */
    if (dv == (void*) DUPREQ_NOCACHE)
        goto out;

    /* do nothing if nfs_dupreq_start failed completely */
    if (dv == (void*) DUPREQ_BAD_ADDR1)
        goto out;

    pthread_mutex_lock(&dv->mtx);
    drc = dv->hin.drc;
    dv->state = DUPREQ_DELETED;
    pthread_mutex_unlock(&dv->mtx);

    LogFullDebug(COMPONENT_DUPREQ,
                 "deleting dv=%p xid=%u on DRC=%p state=%s, status=%s, "
                 "refcnt=%d",
                 dv, dv->hin.tcp.rq_xid, drc,
                 dupreq_state_table[dv->state],
                 dupreq_status_table[status],
                 dv->refcnt);

    /* XXX dv holds a ref on drc */
    t = rbtx_partition_of_scalar(&drc->xt, dv->hk[0]);

    pthread_mutex_lock(&t->mtx);
    rbtree_x_cached_remove(&drc->xt, t, &dv->rbt_k, dv->hk[0]);

    pthread_mutex_unlock(&t->mtx);
    pthread_mutex_lock(&drc->mtx);

    if (TAILQ_IS_ENQUEUED(dv, fifo_q))
        TAILQ_REMOVE(&drc->dupreq_q, dv, fifo_q);
    --(drc->size);

    /* release dv's ref and unlock */
    nfs_dupreq_put_drc(req->rq_xprt, drc, DRC_FLAG_LOCKED);
    /* !LOCKED */

out:
    return (status);
}

/**
 *
 * @brief Decrement the call path refcnt on a cache entry.
 *
 * We assert req->rq_u1 now points to the corresonding duplicate request
 * cache entry (dv).
 *
 * In the common case, a refcnt of 0 indicates that dv is cached.  If
 * also dv->state == DUPREQ_DELETED, the request entry has been discarded
 * and should be destroyed here.
 *
 * @param req [IN] The svc_req structure.
 *
 * @return (nothing)
 *
 */
void nfs_dupreq_rele(struct svc_req *req, const nfs_function_desc_t *func)
{
    dupreq_entry_t *dv = (dupreq_entry_t *) req->rq_u1;

   /* no-cache cleanup */
    if (dv == (void*) DUPREQ_NOCACHE) {
        LogFullDebug(COMPONENT_DUPREQ,
                     "releasing no-cache res %p", req->rq_u2);
	func->free_function(req->rq_u2);
        free_nfs_res(req->rq_u2);
        goto out;
    }

    pthread_mutex_lock(&dv->mtx);

    LogFullDebug(COMPONENT_DUPREQ,
                 "releasing dv=%p xid=%u on DRC=%p state=%s, "
                 "refcnt=%d",
                 dv, dv->hin.tcp.rq_xid, dv->hin.drc,
                 dupreq_state_table[dv->state],
                 dv->refcnt);

    (dv->refcnt)--;
    if (dv->refcnt == 0) {
        if (dv->state == DUPREQ_DELETED) {
            pthread_mutex_unlock(&dv->mtx);
            /* deep free */
            nfs_dupreq_free_dupreq(dv);
            return;
        }
    }
    pthread_mutex_unlock(&dv->mtx);

out:
    /* dispose RPC header */
    if (req->rq_auth)
            SVCAUTH_RELEASE(req->rq_auth, req);

    (void) free_rpc_msg(req->rq_msg);

    return;
}

/**
 * @brief Shutdown the dupreq2 package.
 *
 * @return Nothing.
 */
void dupreq2_pkgshutdown(void)
{
    /* XXX do nothing */
}
