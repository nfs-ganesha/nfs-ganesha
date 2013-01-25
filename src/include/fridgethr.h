/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * ---------------------------------------
 */

/**
 * @defgroup fridgethr Thread Fridge
 *
 * The thread fridge provides a simple implementation of a thread pool
 * built atop POSIX threading capabilities.
 *
 * @{
 */

/**
 * @file fridgethr.c
 * @brief Header for the thread fridge
 */

#ifndef FRIDGETHR_H
#define FRIDGETHR_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "nfs_core.h"

struct fridgethr;

/**
 * @brief A given thread in the fridge
 */

struct fridgethr_entry {
	/**
	 * @brief Thread context
	 */
	struct fridgethr_context {
		uint32_t uflags; /*< Flags (for any use) */
		pthread_t id; /*< Thread ID */
		pthread_mutex_t mtx; /*< Mutex for fiddling this
				         thread */
		pthread_cond_t cv; /*< Condition variable to wait for sync */
		sigset_t sigmask; /*< This thread's signal mask */
		void (*func)(struct fridgethr_context *); /*< Function being
							      executed */
		void *arg; /*< Functions argument */
	} ctx;
	uint32_t flags; /*< Thread-fridge flags (for handoff) */
	bool frozen; /*< Thread is frozen */
	struct timespec timeout; /*< Wait timeout */
	struct glist_head idle_link; /*< Link in the idle queue */
	struct fridgethr *fr; /*< The fridge we belong to */
};

/**
 * @brief Enumeration governing requests when the fridge is full
 */

typedef enum {
	fridgethr_defer_queue, /*< If the fridge is full, queue requests for
				   later and return immediately. */
	fridgethr_defer_fail, /*< If the fridge is full, return an
			          error immediately. */
	fridgethr_defer_block /*< if the fridge is full, wait for a
			          thread to become available and
				  execute on it.  Optionally, return
				  an error on timeout. */
} fridgethr_defer_t;

/**
 * @brief Parameters set at fridgethr_init
 */

struct fridgethr_params {
	uint32_t thr_max; /*< Maximum number of threads */
	uint32_t thr_min; /*< Low watermark for threads.  Do not
			      expire threads out if we have this many
			      or fewer. */
	time_t expiration_delay_s; /*< Time frozen threads will wait
				       without work before terminating.
				       0 for no expiration. */
	fridgethr_defer_t deferment; /*< Deferment strategy for this
				         fridge */
	time_t block_delay; /*< How long to wait before a thread
			        becomes available. */
};

/**
 * @brief Queued requests
 */
struct fridgethr_work {
	struct glist_head link; /*< Link in the work queue */
	void (*func)(struct fridgethr_context *); /*< Function being
						      executed */
	void *arg; /*< Functions argument */
};

/**
 * @brief Commands a caller can issue
 */

typedef enum {
	fridgethr_comm_run, /*< Demand all threads execute */
	fridgethr_comm_pause, /*< Demand all threads suspend */
	fridgethr_comm_stop /*< Demand all threads exit */
} fridgethr_comm_t;

/**
 * @brief Structure representing a group of threads
 */

struct fridgethr {
	char *s; /*< Name for this fridge */
	struct fridgethr_params p; /*< Parameters */
	pthread_mutex_t mtx; /*< Mutex */
	pthread_attr_t attr; /*< Creation attributes */
	uint32_t nthreads; /*< Number of threads running */
	struct glist_head idle_q; /*< Idle threads */
	uint32_t nidle; /*< Number of idle threads */
	uint32_t flags; /*< Fridge-wide flags */
	fridgethr_comm_t command; /*< Command state */
	void (*cb_func)(void *); /*< Callback on command completion */
	void *cb_arg; /*< Argument for completion callback */
	bool transitioning; /*< Changing state */
	union {
		struct glist_head work_q; /*< Work queued */
		struct {
			pthread_cond_t cond; /*< Condition variable
					         on which we wait for a
					         thread to become
					         available. */
			uint32_t waiters; /*< Requests blocked waiting
					      for a thread. */
		} block;
	} deferment;
};

#define fridgethr_flag_none 0x0000 /*< Null flag */
#define fridgethr_flag_available 0x0001 /*< I am available to be
					    dispatched */
#define fridgethr_flag_dispatched 0x0002 /*< You have been dispatched */

int fridgethr_init(struct fridgethr **,
		   const char *,
		   const struct fridgethr_params *);
void fridgethr_destroy(struct fridgethr *);

int fridgethr_get(struct fridgethr*,
		  void (*)(struct fridgethr_context *),
		  void *);

int fridgethr_pause(struct fridgethr *,
		    void (*)(void *),
		    void *);
int fridgethr_stop(struct fridgethr *,
		   void (*)(void *),
		   void *);
int fridgethr_start(struct fridgethr *,
		    void (*)(void *),
		    void *);
int fridgethr_sync_command(struct fridgethr *,
			   fridgethr_comm_t ,
			   time_t);

#endif /* FRIDGETHR_H */

/** @} */
