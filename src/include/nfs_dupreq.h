/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * ---------------------------------------
 */

/**
 * @file nfs_dupreq.h
 * @brief Prototypes for duplicate requst cache
 */

#ifndef NFS_DUPREQ_H
#define NFS_DUPREQ_H

#include <stdbool.h>
#include <string.h>
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include <misc/rbtree_x.h>
#include <misc/queue.h>

enum drc_type {
	DRC_TCP_V4, /*< safe to use an XID-based, per-connection DRC */
	DRC_TCP_V3, /*< a shared, checksummed DRC per address */
	DRC_UDP_V234 /*< UDP is strongly discouraged in RFC 3530bis */
};

#define DRC_FLAG_NONE 0x0000
#define DRC_FLAG_HASH 0x0001
#define DRC_FLAG_CKSUM 0x0002
#define DRC_FLAG_ADDR 0x0004
#define DRC_FLAG_PORT 0x0008
#define DRC_FLAG_LOCKED 0x0010
#define DRC_FLAG_RECYCLE 0x0020
#define DRC_FLAG_RELEASE 0x0040

typedef struct drc {
	enum drc_type type;
	struct rbtree_x xt;
	TAILQ_HEAD(drc_tailq, dupreq_entry) dupreq_q;
	pthread_mutex_t mtx;
	uint32_t npart;
	uint32_t cachesz;
	uint32_t size;
	uint32_t maxsize;
	uint32_t hiwat;
	uint32_t flags;
	uint32_t refcnt; /* call path refs */
	uint32_t retwnd;
	union {
		struct {
			sockaddr_t addr;
			struct opr_rbtree_node recycle_k;
			TAILQ_ENTRY(drc) recycle_q; /* XXX drc */
			time_t recycle_time;
			uint64_t hk; /* hash key */
		} tcp;
	} d_u;
} drc_t;

typedef enum dupreq_state {
	DUPREQ_START = 0,
	DUPREQ_COMPLETE,
	DUPREQ_DELETED
} dupreq_state_t;

struct dupreq_entry {
	struct opr_rbtree_node rbt_k;
	TAILQ_ENTRY(dupreq_entry) fifo_q;
	pthread_mutex_t mtx;
	struct {
		drc_t *drc;
		sockaddr_t addr;
		struct {
			uint32_t rq_xid;
			uint64_t checksum;
		} tcp;
		uint32_t rq_prog;
		uint32_t rq_vers;
		uint32_t rq_proc;
	} hin;
	uint64_t hk;		/* hash key */
	dupreq_state_t state;
	uint32_t refcnt;
	nfs_res_t *res;
	time_t timestamp;
};

typedef struct dupreq_entry dupreq_entry_t;

extern pool_t *nfs_res_pool;

static inline nfs_res_t *alloc_nfs_res(void)
{
	/* XXX can pool/ctor zero mem? */
	nfs_res_t *res = pool_alloc(nfs_res_pool, NULL);
	memset(res, 0, sizeof(nfs_res_t));
	return res;
}

static inline void free_nfs_res(nfs_res_t *res)
{
	pool_free(nfs_res_pool, res);
}

typedef enum dupreq_status {
	DUPREQ_SUCCESS = 0,
	DUPREQ_INSERT_MALLOC_ERROR,
	DUPREQ_BEING_PROCESSED,
	DUPREQ_EXISTS,
	DUPREQ_ERROR,
} dupreq_status_t;

void dupreq2_pkginit(void);
void dupreq2_pkgshutdown(void);

drc_t *drc_get_tcp_drc(struct svc_req *);
void drc_release_tcp_drc(drc_t *);

dupreq_status_t nfs_dupreq_start(nfs_request_data_t *,
				 struct svc_req *);
dupreq_status_t nfs_dupreq_finish(struct svc_req *, nfs_res_t *);
dupreq_status_t nfs_dupreq_delete(struct svc_req *);
void nfs_dupreq_rele(struct svc_req *, const nfs_function_desc_t *);

#endif /* NFS_DUPREQ_H */
