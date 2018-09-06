/*
 * Copyright (c) 2013, The Linux Box Corporation
 *
 * Some portions copyright CEA/DAM/DIF  (2008)
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
 * @addtogroup delayed
 * @{
 */

/**
 * @file delayed_exec.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Implementation of the delayed execution system
 */

#include "config.h"
#include <pthread.h>
#ifdef LINUX
#include <sys/signal.h>
#elif FREEBSD
#include <signal.h>
#endif
#include "abstract_mem.h"
#include "delayed_exec.h"
#include "log.h"
#include "avltree.h"
#include "misc/queue.h"
#include "gsh_intrinsic.h"
#include "common_utils.h"

/**
 * @brief A list of tasks
 */

LIST_HEAD(delayed_tasklist, delayed_task);

/**
 * @brief All tasks executing at a given time.
 */

struct delayed_multi {
	struct timespec realtime;	/*< A wallclock time at which to
					   perform the given task. */
	struct delayed_tasklist list;	/*< The linked list of tasks to
					   perform. */
	struct avltree_node node;	/*< Our node in the tree */
};

/**
 * @brief An individual delayed task
 */

struct delayed_task {
	/** Function for delayed task */
	void (*func)(void *);
	/** Argument for delayed task */
	void *arg;
	/** Link in the task list. */
	LIST_ENTRY(delayed_task) link;
};

/**
 * @brief A list of threads
 */

LIST_HEAD(delayed_threadlist, delayed_thread);

/**
 * @brief An individual executor thread
 */

struct delayed_thread {
	pthread_t id;		/*< Thread id */
	 LIST_ENTRY(delayed_thread) link;	/*< Link in the thread list. */
};

/**
 *  @{
 * Delayed execution state.
 */

/** list of all threads */
static struct delayed_threadlist thread_list;
/** Mutex for delayed execution */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
/** Condition variable for delayed execution */
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
/** The timer tree */
static struct avltree tree;

/**
 * @brief Posssible states for the delayed executor
 */
enum delayed_state {
	delayed_running,	/*< Executor is running */
	delayed_stopping	/*< Executor is stopping */
};

/** State for the executor */
static enum delayed_state delayed_state;

enum delayed_employment {
	delayed_employed,	/*< Work is available, do it. */
	delayed_on_break,	/*< Work is available, but wait for it */
	delayed_unemployed	/*< No work is available, wait indefinitely */
};

/** @} */

static int comparator(const struct avltree_node *a,
		      const struct avltree_node *b)
{
	struct delayed_multi *p =
	    avltree_container_of(a, struct delayed_multi, node);
	struct delayed_multi *q =
	    avltree_container_of(b, struct delayed_multi, node);

	return gsh_time_cmp(&p->realtime, &q->realtime);
}

/**
 * @brief Get a task to perform
 *
 * This function must be called with the mutex held.
 *
 * @param[out] when The time for which to wait, if there is one
 * @param[out] func The function to execute, if there is one
 * @param[out] arg  The argument to supply, if there is one
 *
 * @retval delayed_employed if there is a task to perform now.  Its
 *         argument and function are stored in @c arg and @c func.
 * @retval delayed_on_break if there is work but it is not to be
 *         performed yet.  The time at which it is to be performed is
 *         set in *when.
 * @retval delayed_unemployed if there is no work at all.  The caller
 *         should wait indefinitely to be signalled.
 */

static enum delayed_employment delayed_get_work(struct timespec *when,
						void (**func) (void *),
						void **arg)
{
	struct avltree_node *first = avltree_first(&tree);
	struct delayed_multi *mul;
	struct timespec current;
	struct delayed_task *task;

	if (first == NULL)
		return delayed_unemployed;

	now(&current);
	mul = avltree_container_of(first, struct delayed_multi, node);

	if (gsh_time_cmp(&mul->realtime, &current) > 0) {
		*when = mul->realtime;
		return delayed_on_break;
	}

	task = LIST_FIRST(&mul->list);
	*func = task->func;
	*arg = task->arg;
	LIST_REMOVE(task, link);
	gsh_free(task);
	if (LIST_EMPTY(&mul->list)) {
		avltree_remove(first, &tree);
		gsh_free(mul);
	}

	return delayed_employed;
}

/**
 * @brief Thread function to execute delayed tasks
 *
 * @param[in] arg The thread entry (cast to void)
 *
 * @return NULL, always and forever.
 */

void *delayed_thread(void *arg)
{
	struct delayed_thread *thr = arg;
	int old_type = 0;
	int old_state = 0;
	sigset_t old_sigmask;

	SetNameFunction("Async");

	/* Excplicitly and definitely enable cancellation */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);

	/**
	 * @see fridgethr_start_routine on asynchronous cancellation
	 */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

	pthread_sigmask(SIG_SETMASK, NULL, &old_sigmask);

	PTHREAD_MUTEX_lock(&mtx);
	while (delayed_state == delayed_running) {
		struct timespec then;
		void (*func)(void *);
		void *farg;

		switch (delayed_get_work(&then, &func, &farg)) {
		case delayed_unemployed:
			pthread_cond_wait(&cv, &mtx);
			break;

		case delayed_on_break:
			pthread_cond_timedwait(&cv, &mtx, &then);
			break;

		case delayed_employed:
			PTHREAD_MUTEX_unlock(&mtx);
			func(farg);
			PTHREAD_MUTEX_lock(&mtx);
			break;
		}
	}
	LIST_REMOVE(thr, link);
	if (LIST_EMPTY(&thread_list))
		pthread_cond_broadcast(&cv);

	PTHREAD_MUTEX_unlock(&mtx);
	gsh_free(thr);

	return NULL;
}

/**
 * @brief Initialize and start the delayed execution system
 */

void delayed_start(void)
{
	/* Make this a parameter later */
	const size_t threads_to_start = 1;
	/* Thread attributes */
	pthread_attr_t attr;
	/* Thread index */
	int i;

	LIST_INIT(&thread_list);
	avltree_init(&tree, comparator, 0);

	if (threads_to_start == 0) {
		LogFatal(COMPONENT_THREAD,
			 "You can't execute tasks with zero threads.");
	}

	if (pthread_attr_init(&attr) != 0)
		LogFatal(COMPONENT_THREAD, "can't init pthread's attributes");

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
		LogFatal(COMPONENT_THREAD, "can't set pthread's join state");

	PTHREAD_MUTEX_lock(&mtx);
	delayed_state = delayed_running;

	for (i = 0; i < threads_to_start; ++i) {
		struct delayed_thread *thread =
		    gsh_malloc(sizeof(struct delayed_thread));
		int rc = 0;

		rc = pthread_create(&thread->id, &attr, delayed_thread, thread);
		if (rc != 0) {
			LogFatal(COMPONENT_THREAD,
				 "Unable to start delayed executor: %d", rc);
		}
		LIST_INSERT_HEAD(&thread_list, thread, link);
	}
	PTHREAD_MUTEX_unlock(&mtx);
	pthread_attr_destroy(&attr);
}

/**
 * @brief Shut down the delayed executor
 */

void delayed_shutdown(void)
{
	int rc = -1;
	struct timespec then;

	now(&then);
	then.tv_sec += 120;

	PTHREAD_MUTEX_lock(&mtx);
	delayed_state = delayed_stopping;
	pthread_cond_broadcast(&cv);
	while ((rc != ETIMEDOUT) && !LIST_EMPTY(&thread_list))
		rc = pthread_cond_timedwait(&cv, &mtx, &then);

	if (!LIST_EMPTY(&thread_list)) {
		struct delayed_thread *thr;

		LogMajor(COMPONENT_THREAD,
			 "Delayed executor threads not shutting down cleanly, taking harsher measures.");
		while ((thr = LIST_FIRST(&thread_list)) != NULL) {
			LIST_REMOVE(thr, link);
			pthread_cancel(thr->id);
			gsh_free(thr);
		}
	}
	PTHREAD_MUTEX_unlock(&mtx);
}

/**
 * @brief Submit a new task
 *
 * @param[in] func  The function to run
 * @param[in] arg   The argument to run it with
 * @param[in] delay The dleay in nanoseconds
 *
 * @retval 0 on success.
 * @retval ENOMEM on inability to allocate memory causing other than success.
 */

int delayed_submit(void (*func) (void *), void *arg, nsecs_elapsed_t delay)
{
	struct delayed_multi *mul = NULL;
	struct delayed_task *task = NULL;
	struct avltree_node *collision = NULL;
	struct avltree_node *first = NULL;

	mul = gsh_malloc(sizeof(struct delayed_multi));
	task = gsh_malloc(sizeof(struct delayed_task));

	now(&mul->realtime);
	timespec_add_nsecs(delay, &mul->realtime);

	PTHREAD_MUTEX_lock(&mtx);
	first = avltree_first(&tree);
	collision = avltree_insert(&mul->node, &tree);
	if (unlikely(collision)) {
		gsh_free(mul);
		/* There is already a node for this exact time in the
		   tree.  Add our task to its list. */
		mul =
		    avltree_container_of(collision, struct delayed_multi, node);
	} else {
		LIST_INIT(&mul->list);
	}
	task->func = func;
	task->arg = arg;
	LIST_INSERT_HEAD(&mul->list, task, link);
	if (!first || comparator(&mul->node, first) < 0)
		pthread_cond_broadcast(&cv);

	PTHREAD_MUTEX_unlock(&mtx);

	return 0;
}

/** @} */
