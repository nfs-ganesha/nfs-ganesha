/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#define DRC_FLAG_RECYCLE 0x1

typedef struct drc {
	enum drc_type type;
	struct rbtree_x xt;
	/* Define the tail queue */
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

/*
 *  The old code would drop any duplicate request while the original
 *  request was still in progress, assuming that the response would be
 *  sent. Unfortunately, if a TCP connection is broken while the request
 *  is in progress, sending the response fails. The client never retries
 *  and gets stuck.
 *
 *  Now when this occurs, we queue up the request and suspend it (utlizing
 *  the async infrastructure). When the original request processing
 *  completes and calls nfs_dupreq_finish() we track if there was an error
 *  sending the response. If so, we don't mark the DRC entry as complete
 *  and instead resume the first retry to attempt to send the response.
 *
 *  That resumed retry will call nfs_dupreq_finish() after it tries to
 *  send the response, so if there is a queue of retries, there are more
 *  opportunities to re-send a failed response.
 *
 *  The same retry logic is followed when nfs_dupreq_delete() is called
 *  if there are again queued duplicate requests, however, those retries
 *  instead are re-submitted for a new attempt to process. This logic
 *  occurs when there is an NFS_DROP result from a retryable error or
 *  an auth error.
 *
 *  Once the request is succesfully completed, any additional queued
 *  requests are dropped.
 *
 *  We limit the queue to 3 duplicates. That should be more than enough
 *  to get through an issue like this unless the server has severely
 *  stalled out on the original request.
 */

#define DUPREQ_MAX_DUPES 3

struct dupreq_entry {
	struct opr_rbtree_node rbt_k;
	/* Define the tail queue */
	TAILQ_ENTRY(dupreq_entry) fifo_q;
	/* Queued duplicate requests waiting for request completion. Limited
	 * to DUPREQ_MAX_DUPES.
	 */
	TAILQ_HEAD(dupes, nfs_request) dupes;
	pthread_mutex_t mtx;
	struct {
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
	bool complete;
	uint32_t refcnt;
	nfs_res_t *res;
	enum nfs_req_result rc;
	/* Count of duplicate requests fielded. This coundts ALL duplicate
	 * requests whether queued while the request is completing and those
	 * that arrive after completion.
	 */
	int dupe_cnt;
};

typedef struct dupreq_entry dupreq_entry_t;

extern pool_t *nfs_res_pool;

static inline nfs_res_t *alloc_nfs_res(void)
{
	return pool_alloc(nfs_res_pool);
}

static inline void free_nfs_res(nfs_res_t *res)
{
	pool_free(nfs_res_pool, res);
}

static inline enum nfs_req_result nfs_dupreq_reply_rc(nfs_request_t *reqnfs)
{
	dupreq_entry_t *dv = reqnfs->svc.rq_u1;

	return dv->rc;
}

typedef enum dupreq_status {
	DUPREQ_SUCCESS = 0,
	DUPREQ_BEING_PROCESSED,
	DUPREQ_EXISTS,
	DUPREQ_DROP,
} dupreq_status_t;

void dupreq2_pkginit(void);
void dupreq2_pkgshutdown(void);

drc_t *drc_get_tcp_drc(struct svc_req *);
void drc_release_tcp_drc(drc_t *);
void nfs_dupreq_put_drc(drc_t *drc);

dupreq_status_t nfs_dupreq_start(nfs_request_t *);
void nfs_dupreq_finish(nfs_request_t *,  enum nfs_req_result);
void nfs_dupreq_delete(nfs_request_t *,  enum nfs_req_result);
void nfs_dupreq_rele(nfs_request_t *);

#endif /* NFS_DUPREQ_H */
