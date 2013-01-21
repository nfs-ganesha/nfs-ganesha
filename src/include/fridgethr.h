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

struct thr_fridge;

/**
 * @brief A given thread in the fridge
 */

struct fridge_thr_entry {
	/**
	 * @brief Thread context
	 */
	struct fridge_thr_context {
		uint32_t uflags; /*< Flags (for any use) */
		pthread_t id; /*< Thread ID */
		pthread_mutex_t mtx; /*< Mutex for fiddling this
				         thread */
		pthread_cond_t cv; /*< Condition variable to wait for sync */
		sigset_t sigmask; /*< This thread's signal mask */
		void (*func)(struct fridge_thr_context *); /*< Function being
							      executed */
		void *arg; /*< Functions argument */
	} ctx;
	uint32_t flags; /*< Thread-fridge flags (for handoff) */
	bool frozen; /*< Thread is frozen */
	struct timespec timeout; /*< Wait timeout */
	struct glist_head q; /*< A link in whatever list we're part of */
	struct thr_fridge *fr; /*< The fridge we belong to */
};

typedef struct fridge_thr_entry fridge_entry_t;
typedef struct fridge_thr_context fridge_thr_context_t;

/**
 * @brief Parameters set at fridgethr_init
 */

struct thr_fridge_params {
	uint32_t thr_max; /*< Maximum number of threads */
	/**
	 * Stack size for created threads.
	 *
	 * @note A screwy value here can cause pthread_create to fail
	 * rather than firdgethr_init.  In general you should just set
	 * it to 0 and take the default.
	 */
	uint32_t stacksize;
	uint32_t expiration_delay_s; /*< Expiration for frozen threads */
};

/**
 * @brief Queued requests
 */
struct thr_work_queued {
	struct glist_head link; /*< Link in the work queue */
	void (*func)(struct fridge_thr_context *); /*< Function being
						       executed */
	void *arg; /*< Functions argument */
};

/**
 * @brief Structure representing a group of threads
 */

typedef struct thr_fridge {
	char *s; /*< Name for this fridge */
	struct thr_fridge_params p; /*< Parameters */
	pthread_mutex_t mtx; /*< Mutex */
	pthread_attr_t attr; /*< Creation attributes */
	uint32_t nthreads; /*< Number of threads running */
	struct glist_head idle_q; /*< Idle threads */
	struct glist_head work_q; /*< Work queued */
	uint32_t nidle; /*< Number of idle threads */
	uint32_t flags; /*< Fridge-wide flags */
} thr_fridge_t;

/** @brief Null flag */
#define FridgeThr_Flag_None     0x0000
/** @brief Wait for a rendezvous */
#define FridgeThr_Flag_WaitSync 0x0001
/** @brief Completed something */
#define FridgeThr_Flag_SyncDone 0x0002

int fridgethr_init(struct thr_fridge **,
		   const char *,
		   const struct thr_fridge_params *);
void fridgethr_destroy(struct thr_fridge *);

int fridgethr_get(thr_fridge_t *,
		  void (*)(fridge_thr_context_t *),
		  void *);
#endif /* FRIDGETHR_H */

/** @} */
