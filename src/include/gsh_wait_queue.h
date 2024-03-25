/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 *
 * @file wait_queue.h
 * @author Matt Benjamin
 * @brief Pthreads-based wait queue package
 *
 * @section DESCRIPTION
 *
 * This module provides simple wait queues using pthreads primitives.
 */

#ifndef GSH_WAIT_QUEUE_H
#define GSH_WAIT_QUEUE_H

#include <errno.h>
#include <pthread.h>
#include "gsh_list.h"
#include "common_utils.h"

typedef struct wait_entry {
	pthread_mutex_t wq_mtx;
	pthread_cond_t wq_cv;
} wait_entry_t;

#define Wqe_LFlag_None 0x0000
#define Wqe_LFlag_WaitSync 0x0001
#define Wqe_LFlag_SyncDone 0x0002

/* thread wait queue */
typedef struct wait_q_entry {
	uint32_t flags;
	uint32_t waiters;
	wait_entry_t lwe; /* left */
	wait_entry_t rwe; /* right */
	struct glist_head waitq;
} wait_q_entry_t;

static inline void init_wait_entry(wait_entry_t *we)
{
	PTHREAD_MUTEX_init(&we->wq_mtx, NULL);
	PTHREAD_COND_init(&we->wq_cv, NULL);
}

static inline void init_wait_q_entry(wait_q_entry_t *wqe)
{
	glist_init(&wqe->waitq);
	init_wait_entry(&wqe->lwe);
	init_wait_entry(&wqe->rwe);
}

static inline void destroy_wait_entry(wait_entry_t *we)
{
	PTHREAD_MUTEX_destroy(&we->wq_mtx);
	PTHREAD_COND_destroy(&we->wq_cv);
}

static inline void destroy_wait_q_entry(wait_q_entry_t *wqe)
{
	destroy_wait_entry(&wqe->lwe);
	destroy_wait_entry(&wqe->rwe);
}

#endif /* GSH_WAIT_QUEUE_H */
