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
#include "ganesha_list.h"

struct fridgethr;

/*< Decoder thread pool */
extern struct fridgethr *req_fridge;

/**
 * @brief A given thread in the fridge
 */

struct fridgethr_entry {
	/**
	 * @brief Thread context
	 */
	struct fridgethr_context {
		uint32_t uflags; /*< Flags (for any use) */
		pthread_t id;	/*< Thread ID */
		pthread_mutex_t mtx;	/*< Mutex for fiddling this
					   thread */
		pthread_cond_t cv;	/*< Condition variable to wait for sync
					 */
		sigset_t sigmask;	/*< This thread's signal mask */
		bool woke;	/*< Set to false on first run and if wait
				   in fridgethr_freeze didn't time out. */
		void *thread_info;	/*< Information belonging to the
					   user and associated with the
					   thread.  Never modified by the
					   fridgethr code. */
		void (*func) (struct fridgethr_context *); /*< Function being
							       executed */
		void *arg;	/*< Functions argument */
	} ctx;
	uint32_t flags; /*< Thread-fridge flags (for handoff) */
	bool frozen; /*< Thread is frozen */
	struct timespec timeout; /*< Wait timeout */
	struct glist_head thread_link; /*< Link in the list of all
					   threads */
	struct glist_head idle_link; /*< Link in the idle queue */
	struct fridgethr *fr; /*< The fridge we belong to */
};

/**
 * @brief Fridge flavor, governing style of operation.
 */

typedef enum {
	fridgethr_flavor_worker = 0, /*< Take submitted jobs, do them,
					 and then wait for more work
					 to be submitted. */
	fridgethr_flavor_looper = 1 /*< Each thread takes a single
					job and repeats it. */
} fridgethr_flavor_t;

/**
 * @brief Enumeration governing requests when the fridge is full
 */

typedef enum {
	fridgethr_defer_fail = 0, /*< If the fridge is full, return an
				      error immediately.  This is the
				      only allowable value for
				      fridgethr_flavor_looper. */
	fridgethr_defer_queue = 1, /*< If the fridge is full, queue
				       requests for later and return
				       immediately. */
	fridgethr_defer_block = 2 /*< if the fridge is full, wait for
				      a thread to become available and
				      execute on it.  Optionally,
				      return an error on timeout. */
} fridgethr_defer_t;

/**
 * @brief Parameters set at fridgethr_init
 */

struct fridgethr_params {
	uint32_t thr_max; /*< Maximum number of threads */
	uint32_t thr_min; /*< Low watermark for threads.  Do not
			      expire threads out if we have this many
			      or fewer. */
	time_t thread_delay; /*< Time frozen threads will wait after
				 performing work.  For
				 fridgethr_flavor_worker fridges,
				 threads exit if they are above the
				 low water mark and no work is
				 available after timeout.  For
				 fridgethr_flavor_looper fridges,
				 sleep for this period before
				 re-executing the supplied
				 function. */
	fridgethr_flavor_t flavor; /*< Execution flavor for this
				       fridge. */
	fridgethr_defer_t deferment; /*< Deferment strategy for this
					 fridge */
	time_t block_delay; /*< How long to wait before a thread
				becomes available. */
	/**
	 * If non-NULL, run after every submitted job.
	 */
	void (*task_cleanup)(struct fridgethr_context *);
	/**
	 * If non-NULL, called on thread creation just before we start
	 * work, but after we set the function name (so it can be
	 * overridden.)
	 */
	void (*thread_initialize)(struct fridgethr_context *);
	/**
	 * If non-NULL, called on thread exit, just before the context
	 * is freed.
	 */
	void (*thread_finalize)(struct fridgethr_context *);
	/**
	 * Use this function to wake up all threads on a state
	 * transition.	Specifying this function implies that the
	 * worker in a thread will wait for more work on its own. The
	 * run function must be written either so that it exits after
	 * any given piece of work or such that it calls @c
	 * fridgethr_you_should_break before waiting.
	 */
	void (*wake_threads)(void *);
	/* Argument for wake_threads */
	void *wake_threads_arg;
};

/**
 * @brief Queued requests
 */
struct fridgethr_work {
	struct glist_head link;	/*< Link in the work queue */
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
	char *s;		/*< Name for this fridge */
	struct fridgethr_params p;	/*< Parameters */
	pthread_mutex_t mtx;	/*< Mutex */
	pthread_attr_t attr;	/*< Creation attributes */
	struct glist_head thread_list;	/*< List of threads */
	uint32_t nthreads;	/*< Number of threads in fridge */
	struct glist_head idle_q;	/*< Idle threads */
	uint32_t nidle;		/*< Number of idle threads */
	uint32_t flags;		/*< Fridge-wide flags */
	fridgethr_comm_t command;	/*< Command state */
	void (*cb_func) (void *);	/*< Callback on command completion */
	void *cb_arg;		/*< Argument for completion callback */
	pthread_mutex_t *cb_mtx;	/*< Mutex for completion condition
					    variable */
	pthread_cond_t *cb_cv;	/*< Condition variable, signalled on
				   completion */
	bool transitioning; /*< Changing state */
	union {
		struct glist_head work_q; /*< Work queued */
		struct {
			pthread_cond_t cond; /*< Condition variable on which
						 we wait for a thread to become
						 available. */
			uint32_t waiters; /*< Requests blocked waiting for a
					      thread. */
		} block;
	} deferment;
};

#define fridgethr_flag_none 0x0000 /*< Null flag */
#define fridgethr_flag_available 0x0001 /*< I am available to be
					    dispatched */
#define fridgethr_flag_dispatched 0x0002 /*< You have been dispatched */

int fridgethr_init(struct fridgethr **, const char *,
		   const struct fridgethr_params *);
void fridgethr_destroy(struct fridgethr *);

int fridgethr_submit(struct fridgethr *, void (*)(struct fridgethr_context *),
		     void *);
int fridgethr_wake(struct fridgethr *);

int fridgethr_pause(struct fridgethr *, pthread_mutex_t *, pthread_cond_t *,
		    void (*)(void *), void *);
int fridgethr_stop(struct fridgethr *, pthread_mutex_t *, pthread_cond_t *,
		   void (*)(void *), void *);
int fridgethr_start(struct fridgethr *, pthread_mutex_t *, pthread_cond_t *,
		    void (*)(void *), void *);
int fridgethr_sync_command(struct fridgethr *, fridgethr_comm_t, time_t);
bool fridgethr_you_should_break(struct fridgethr_context *);
int fridgethr_populate(struct fridgethr *, void (*)(struct fridgethr_context *),
		      void *);

void fridgethr_setwait(struct fridgethr_context *ctx, time_t thread_delay);
time_t fridgethr_getwait(struct fridgethr_context *ctx);

void fridgethr_cancel(struct fridgethr *fr);

extern struct fridgethr *general_fridge;
int general_fridge_init(void);
int general_fridge_shutdown(void);

#endif				/* FRIDGETHR_H */

/** @} */
