/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Copyright (c) 2012-2017 Red Hat, Inc. and/or its affiliates.
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
 * @file 9p_req_queue.h
 * @author Matt Benjamin <matt@linuxbox.com>
 * @brief 9P request queue package
 *
 * This module defines an infrastructure for classification and
 * dispatch of incoming protocol requests using a forward queueing
 * model, with priority and isolation partitions.
 */

#ifndef _9P_REQ_QUEUE_H
#define _9P_REQ_QUEUE_H

#include "gsh_list.h"
#include "gsh_wait_queue.h"

struct req_q {
	pthread_spinlock_t sp;
	struct glist_head q;	/* LIFO */
	uint32_t size;
	uint32_t max;
	uint32_t waiters;
};

struct req_q_pair {
	const char *s;
	GSH_CACHE_PAD(0);
	struct req_q producer;	/* from decoder */
	GSH_CACHE_PAD(1);
	struct req_q consumer;	/* to executor */
	GSH_CACHE_PAD(2);
};

enum req_q_e {
	REQ_Q_LOW_LATENCY,	/*< GETATTR, RENEW, etc */
	N_REQ_QUEUES
};

struct req_q_set {
	struct req_q_pair qset[N_REQ_QUEUES];
};

struct _9p_req_st {
	struct {
		uint32_t ctr;
		struct req_q_set _9p_request_q;
		uint64_t size;
		pthread_spinlock_t sp;
		struct glist_head wait_list;
		uint32_t waiters;
	} reqs;
	GSH_CACHE_PAD(1);
};

static inline void _9p_rpc_q_init(struct req_q *q)
{
	glist_init(&q->q);
	pthread_spin_init(&q->sp, PTHREAD_PROCESS_PRIVATE);
	q->size = 0;
	q->waiters = 0;
}

static inline void _9p_queue_awaken(void *arg)
{
	struct _9p_req_st *st = arg;
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

#endif				/* _9P_REQ_QUEUE_H */
