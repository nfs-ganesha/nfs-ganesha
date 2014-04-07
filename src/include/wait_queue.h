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

#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H

#include <errno.h>
#include <pthread.h>
#include "ganesha_list.h"

typedef struct wait_entry {
	pthread_mutex_t mtx;
	pthread_cond_t cv;
} wait_entry_t;

#define Wqe_LFlag_None        0x0000
#define Wqe_LFlag_WaitSync    0x0001
#define Wqe_LFlag_SyncDone    0x0002

/* thread wait queue */
typedef struct wait_q_entry {
	uint32_t flags;
	uint32_t waiters;
	wait_entry_t lwe;	/* left */
	wait_entry_t rwe;	/* right */
	struct glist_head waitq;
} wait_q_entry_t;

static inline int gsh_mutex_init(pthread_mutex_t *m,
				 const pthread_mutexattr_t *a
				 __attribute__ ((unused)))
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr,
#if defined(__linux__)
				  PTHREAD_MUTEX_ADAPTIVE_NP
#else
				  PTHREAD_MUTEX_DEFAULT
#endif
	    );
	return pthread_mutex_init(m, &attr);
}

static inline void init_wait_entry(wait_entry_t *we)
{
	gsh_mutex_init(&we->mtx, NULL);
	pthread_cond_init(&we->cv, NULL);
}

static inline void init_wait_q_entry(wait_q_entry_t *wqe)
{
	glist_init(&wqe->waitq);
	init_wait_entry(&wqe->lwe);
	init_wait_entry(&wqe->rwe);
}

static inline void thread_delay_ms(unsigned long ms)
{
	struct timespec then = {
		.tv_sec = ms / 1000,
		.tv_nsec = (ms % 1000) * 1000000UL
	};
	nanosleep(&then, NULL);
}

#endif /* WAIT_QUEUE_H */
