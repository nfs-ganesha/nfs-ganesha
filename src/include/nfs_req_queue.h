/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
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
 * @file nfs_req_queue.h
 * @author Matt Benjamin <matt@linuxbox.com>
 * @brief NFS request queue package
 *
 * This module defines an infrastructure for classification and
 * dispatch of incoming protocol requests using a forward queueing
 * model, with priority and isolation partitions.
 */

#ifndef NFS_REQ_QUEUE_H
#define NFS_REQ_QUEUE_H

#include "ganesha_list.h"
#include "wait_queue.h"

/* XXX moving to gsh_intrinsic.h */
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64	/* XXX arch-specific define */
#endif
#define CACHE_PAD(_n) char __pad ## _n [CACHE_LINE_SIZE]

struct req_q {
	pthread_spinlock_t sp;
	struct glist_head q;	/* LIFO */
	uint32_t size;
	uint32_t max;
	uint32_t waiters;
};

struct req_q_pair {
	const char *s;
	 CACHE_PAD(0);
	struct req_q producer;	/* from decoder */
	 CACHE_PAD(1);
	struct req_q consumer;	/* to executor */
	 CACHE_PAD(2);
};

#define REQ_Q_MOUNT 0
#define REQ_Q_CALL 1
#define REQ_Q_LOW_LATENCY 2	/*< GETATTR, RENEW, etc */
#define REQ_Q_HIGH_LATENCY 3	/*< READ, WRITE, COMMIT, etc */
#define N_REQ_QUEUES 4

extern const char *req_q_s[N_REQ_QUEUES];	/* for debug prints */

struct req_q_set {
	struct req_q_pair qset[N_REQ_QUEUES];
};

struct nfs_req_st {
	struct {
		uint32_t ctr;
		struct req_q_set nfs_request_q;
		uint64_t size;
		pthread_spinlock_t sp;
		struct glist_head wait_list;
		uint32_t waiters;
	} reqs;
	 CACHE_PAD(1);
	struct {
		pthread_mutex_t mtx;
		struct glist_head q;
		uint32_t stalled;
		bool active;
	} stallq;
};

extern struct nfs_req_st nfs_req_st;

void nfs_rpc_queue_init(void);

static inline void nfs_rpc_q_init(struct req_q *q)
{
	glist_init(&q->q);
	pthread_spin_init(&q->sp, PTHREAD_PROCESS_PRIVATE);
	q->size = 0;
	q->waiters = 0;
}

static inline uint32_t nfs_rpc_q_next_slot(void)
{
	uint32_t ix = atomic_inc_uint32_t(&nfs_req_st.reqs.ctr);
	if (!ix)
		ix = atomic_inc_uint32_t(&nfs_req_st.reqs.ctr);
	return ix;
}

static inline void nfs_rpc_queue_awaken(void *arg)
{
	struct nfs_req_st *st = arg;
	struct glist_head *g = NULL;
	struct glist_head *n = NULL;

	pthread_spin_lock(&st->reqs.sp);
	glist_for_each_safe(g, n, &st->reqs.wait_list) {
		wait_q_entry_t *wqe = glist_entry(g, wait_q_entry_t, waitq);
		pthread_cond_signal(&wqe->lwe.cv);
		pthread_cond_signal(&wqe->rwe.cv);
	}
	pthread_spin_unlock(&st->reqs.sp);
}

#endif				/* NFS_REQ_QUEUE_H */
