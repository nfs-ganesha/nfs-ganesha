/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @addtogroup fridgethr
 * @{
 */

/**
 * @file fridgethr.c
 * @brief Implementation of the thread fridge
 *
 */

#include "config.h"

#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef LINUX
#include <sys/signal.h>
#elif FREEBSD
#include <signal.h>
#endif
#include "abstract_mem.h"
#include "fridgethr.h"
#include "nfs_core.h"

/**
 * @brief Initialize a thread fridge
 *
 * @note It is more robust to initialize the parameters to 0 than set
 * specifically what is desired, otherwise uninitialized memory could
 * provoke unexpected behaviour when new parameters are added.
 *
 * @param[out] frout The fridge to initialize
 * @param[in]  s     The name of the fridge
 * @param[in]  p     Fridge parameters
 *
 * @return 0 on success, POSIX errors on failure.
 */

int fridgethr_init(struct fridgethr **frout, const char *s,
		   const struct fridgethr_params *p)
{
	/* The fridge under construction */
	struct fridgethr *frobj = gsh_malloc(sizeof(struct fridgethr));
	/* The return code for this function */
	int rc = 0;
	/* True if the thread attributes have been initialized */
	bool attrinit = false;
	/* True if the fridge mutex has been initialized */
	bool mutexinit = false;

	if ((p->thr_min > p->thr_max) && p->thr_max != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Minimum of %d is greater than maximum of %d in "
			 "fridge %s", p->thr_min, p->thr_max, s);
		rc = EINVAL;
		goto out;
	}

	if ((p->wake_threads != NULL) &&
	    (p->flavor != fridgethr_flavor_looper)) {
		LogMajor(COMPONENT_THREAD,
			 "Wake function only allowed on loopers: %s", s);
		rc = EINVAL;
		goto out;
	}

	*frout = NULL;
	if (frobj == NULL) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to allocate thread fridge for %s", s);
		rc = ENOMEM;
		goto out;
	}
	frobj->p = *p;

	frobj->s = NULL;
	frobj->nthreads = 0;
	frobj->nidle = 0;
	frobj->flags = fridgethr_flag_none;

	/* This always succeeds on Linux, but it might fail on other
	   systems or future versions of Linux. */
	rc = pthread_attr_init(&frobj->attr);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize thread attributes for "
			 "fridge %s: %d", s, rc);
		goto out;
	}
	attrinit = true;
	rc = pthread_attr_setscope(&frobj->attr, PTHREAD_SCOPE_SYSTEM);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to set thread scope for fridge %s: %d", s, rc);
		goto out;
	}
	rc = pthread_attr_setdetachstate(&frobj->attr, PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to set threads detached for fridge %s: %d", s,
			 rc);
		goto out;
	}
	/* This always succeeds on Linux (if you believe the manual),
	   but SUS defines errors. */
	rc = pthread_mutex_init(&frobj->mtx, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize mutex for fridge %s: %d", s, rc);
		goto out;
	}
	mutexinit = true;

	frobj->s = gsh_strdup(s);
	if (!frobj->s) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to allocate memory in fridge %s", s);
		rc = ENOMEM;
	}

	frobj->command = fridgethr_comm_run;
	frobj->transitioning = false;

	/* Thread list */
	glist_init(&frobj->thread_list);

	/* Idle threads queue */
	glist_init(&frobj->idle_q);

	/* Flavor */

	if (frobj->p.flavor == fridgethr_flavor_worker) {
		/* Deferment */
		switch (frobj->p.deferment) {
		case fridgethr_defer_queue:
			glist_init(&frobj->deferment.work_q);
			break;

		case fridgethr_defer_block:
			pthread_cond_init(&frobj->deferment.block.cond, NULL);
			frobj->deferment.block.waiters = 0;
			break;

		case fridgethr_defer_fail:
			/* Failing is easy. */
			break;

		default:
			LogMajor(COMPONENT_THREAD,
				 "Invalid value fridgethr_defer_t of %d in %s",
				 frobj->p.deferment, s);
			rc = EINVAL;
			goto out;
		}
	} else if (frobj->p.flavor == fridgethr_flavor_looper) {
		if (frobj->p.deferment != fridgethr_defer_fail) {
			LogMajor(COMPONENT_THREAD,
				 "Deferment is not allowed in looper fridges: "
				 "In fridge %s, requested deferment of %d.", s,
				 frobj->p.deferment);
			rc = EINVAL;
			goto out;
		}
	} else {
		LogMajor(COMPONENT_THREAD,
			 "Thread flavor of %d is disallowed in fridge: %s",
			 frobj->p.flavor, s);
		rc = EINVAL;
		goto out;
	}

	*frout = frobj;
	rc = 0;

 out:

	if (rc != 0) {
		if (mutexinit) {
			pthread_mutex_destroy(&frobj->mtx);
			mutexinit = false;
		}
		if (attrinit) {
			pthread_attr_destroy(&frobj->attr);
			attrinit = false;
		}
		if (frobj) {
			if (frobj->s) {
				gsh_free(frobj->s);
				frobj->s = NULL;
			}
			gsh_free(frobj);
			frobj = NULL;
		}
	}

	return rc;
}

/**
 * @brief Destroy a thread fridge
 *
 * @param[in] fr The fridge to destroy
 */

void fridgethr_destroy(struct fridgethr *fr)
{
	pthread_mutex_destroy(&fr->mtx);
	pthread_attr_destroy(&fr->attr);
	gsh_free(fr->s);
	gsh_free(fr);
}

/**
 * @brief Finish a transition
 *
 * Notify whoever cares that we're done and mark the transition as
 * complete.  The fridge lock must be held when calling this function.
 *
 * @param[in,out] fr     The fridge
 * @param[in]     locked The completion mutex is already locked.
 *                       Neither acquire nor release it.
 */

static void fridgethr_finish_transition(struct fridgethr *fr, bool locked)
{
	if (!fr->transitioning)
		return;

	if (fr->cb_mtx && !locked)
		pthread_mutex_lock(fr->cb_mtx);

	if (fr->cb_func != NULL)
		fr->cb_func(fr->cb_arg);

	if (fr->cb_cv)
		pthread_cond_broadcast(fr->cb_cv);

	if (fr->cb_mtx && !locked)
		pthread_mutex_unlock(fr->cb_mtx);

	if (!locked) {
		fr->cb_mtx = NULL;
		fr->cb_cv = NULL;
	}

	fr->cb_func = NULL;
	fr->cb_arg = NULL;
	fr->transitioning = false;
}

/**
 * @brief Test whether the fridge has deferred work waiting
 *
 * @note This function must be called with the fridge mutex held.
 *
 * @return true if deferred work is waiting.
 */

static bool fridgethr_deferredwork(struct fridgethr *fr)
{
	bool res = false;

	switch (fr->p.deferment) {
	case fridgethr_defer_queue:
		res = !glist_empty(&fr->deferment.work_q);
		break;

	case fridgethr_defer_block:
		res = (fr->deferment.block.waiters > 0);
		break;

	case fridgethr_defer_fail:
		res = false;
		break;
	}

	return res;
}

/**
 * @brief Get deferred work
 *
 * This function only does something in the case of a queueing
 * fridge.  If work is available, it loads it into the thread context
 * and returns true.  If work is not available (or the fridge is not a
 * queueing fridge) it returns false and leaves the context untouched.
 *
 * @param[in,out] fr Fridge
 * @param[in,out] fe Fridge entry
 *
 * @note This function must be called with the fridge mutex held.
 *
 * @return true if deferred work has been dequeued.
 */

static bool fridgethr_getwork(struct fridgethr *fr, struct fridgethr_entry *fe)
{
	if ((fr->p.deferment == fridgethr_defer_block)
	    || (fr->p.deferment == fridgethr_defer_fail)
	    || glist_empty(&fr->deferment.work_q)) {
		return false;
	} else {
		struct fridgethr_work *q =
		    glist_first_entry(&fr->deferment.work_q,
				      struct fridgethr_work,
				      link);
		glist_del(&q->link);
		fe->ctx.func = q->func;
		fe->ctx.arg = q->arg;
		gsh_free(q);
		return true;
	}
}

/**
 * @brief Wait for more work
 *
 * This function, called by a worker thread, will cause it to wait for
 * more work (or exit).
 *
 * @note To dispatch a task to a sleeping thread, that is, to load a
 * function and argument into its context and have them executed,
 * fridgethr_flag_dispatched must be set.  If the thread awakes and
 * firdgethr_flag_dispatched is not set, it will decide what to do on
 * its own based on the current command and queue.
 *
 * @retval true if we have more work to do.
 * @retval false if we need to go away.
 */

static bool fridgethr_freeze(struct fridgethr *fr,
			     struct fridgethr_context *thr_ctx)
{
	/* Entry for this thread */
	struct fridgethr_entry *fe =
	    container_of(thr_ctx, struct fridgethr_entry,
			 ctx);
	/* Return code from system calls */
	int rc = 0;

	PTHREAD_MUTEX_lock(&fr->mtx);
 restart:
	/* If we are not paused and there is work left to do in the
	   queue, do it. */
	if (!(fr->command == fridgethr_comm_pause) &&
	    fridgethr_getwork(fr, fe)) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return true;
	}

	/* rc would have been set in the while loop below */
	if (((rc == ETIMEDOUT) && (fr->nthreads > fr->p.thr_min))
	    || (fr->command == fridgethr_comm_stop)) {
		/* We do this here since we already have the fridge
		   lock. */
		--(fr->nthreads);
		glist_del(&fe->thread_link);
		if ((fr->nthreads == 0) && (fr->command == fridgethr_comm_stop)
		    && (fr->transitioning) && !fridgethr_deferredwork(fr)) {
			/* We're the last thread to exit, signal the
			   transition to pause complete. */
			fridgethr_finish_transition(fr, false);
		}
		PTHREAD_MUTEX_lock(&fe->ctx.mtx);
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return false;
	}

	assert(fr->command != fridgethr_comm_stop);

	glist_add_tail(&fr->idle_q, &fe->idle_link);
	++(fr->nidle);
	if ((fr->nidle == fr->nthreads) && (fr->command == fridgethr_comm_pause)
	    && (fr->transitioning)) {
		/* We're the last thread to suspend, signal the
		   transition to pause complete. */
		fridgethr_finish_transition(fr, false);
	}

	PTHREAD_MUTEX_lock(&fe->ctx.mtx);
	fe->frozen = true;
	fe->flags |= fridgethr_flag_available;
	/* Not ideal, but no ideal factoring occurred to me. */
	if ((fr->p.deferment == fridgethr_defer_block)
	    && (fr->deferment.block.waiters > 0)) {
		pthread_cond_signal(&fr->deferment.block.cond);
	}
	PTHREAD_MUTEX_unlock(&fr->mtx);

	/* It is a state machine, keep going until we have a
	   transition that gets us out. */
	while (true) {
		if ((fr->p.wake_threads == NULL)
		    || (fr->command != fridgethr_comm_run)) {
			if (fr->p.thread_delay > 0) {
				clock_gettime(CLOCK_REALTIME, &fe->timeout);
				fe->timeout.tv_sec += fr->p.thread_delay;
				rc = pthread_cond_timedwait(&fe->ctx.cv,
							    &fe->ctx.mtx,
							    &fe->timeout);
			} else
				rc = pthread_cond_wait(&fe->ctx.cv,
						       &fe->ctx.mtx);
		}

		if (rc == ETIMEDOUT)
			fe->ctx.woke = false;
		else
			fe->ctx.woke = true;

		/* Clear this while we have the lock, we can set it
		   again before continuing */
		fe->frozen = false;

		/* It's repetition, but it saves us from having to
		   drop and then reacquire the lock later. */
		if (fe->flags & fridgethr_flag_dispatched) {
			fe->flags &=
			    ~(fridgethr_flag_available |
			      fridgethr_flag_dispatched);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
			break;
		}

		/* Clear available so we won't be dispatched while
		   we're acquiring the fridge lock. */
		fe->flags &= ~fridgethr_flag_available;
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		PTHREAD_MUTEX_lock(&fr->mtx);

		/* Nothing to do, loop around. */
		if (fr->command != fridgethr_comm_stop
		    && ((fr->command == fridgethr_comm_pause)
			|| (!fridgethr_deferredwork(fr)))
		    && (fr->p.flavor == fridgethr_flavor_worker)) {
			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			fe->frozen = true;
			fe->flags |= fridgethr_flag_available;

			/* Not ideal, but no ideal factoring occurred to me. */
			if ((fr->p.deferment == fridgethr_defer_block)
			    && (fr->deferment.block.waiters > 0)) {
				pthread_cond_signal(&fr->deferment.block.cond);
			}
			PTHREAD_MUTEX_unlock(&fr->mtx);
			continue;
		}

		--(fr->nidle);
		glist_del(&fe->idle_link);
		if (fr->p.flavor == fridgethr_flavor_worker)
			goto restart;
		else {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			break;
		}
	}

	/* We were already unfrozen and taken off the idle queue, so
	   there's nothing more to do than: */
	return true;
}
/**
 * @brief Operation context.
 *
 * This carries everything relevant to a protocol operation
 * Since it is a thread local, it is exclusively in the thread context
 * and cannot be shared with another thread.
 *
 * This will always point to a valid structure.  When its contents go out
 * of scope this is set to NULL but since dereferencing with this expectation,
 * a SEGV will result.  This will point to one of three structures:
 *
 * 1. The req_ctx declared in rpc_execute().  This is the state for any NFS op.
 *
 * 2. The op_context declared/referenced in a 9P fid.
 *    Same as req_ctx but for 9P operations.
 *
 * 3. A root context which is used for upcalls, exports bashing, and async
 *    events that call functions that expect a context set up.
 */

__thread struct req_op_context *op_ctx;


/**
 * @brief Initialization of a new thread in the fridge
 *
 * This routine calls the procedure that implements the actual
 * functionality wanted by a thread in a loop, handling rescheduling.
 *
 * @param[in] arg The fridge entry for this thread
 *
 * @return NULL.
 */

static void *fridgethr_start_routine(void *arg)
{
	struct fridgethr_entry *fe = arg;
	struct fridgethr *fr = fe->fr;
	bool reschedule;
	int rc = 0;
	int old_type = 0;
	int old_state = 0;

	SetNameFunction(fr->s);

	/* Excplicitly and definitely enable cancellation */
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);

	/* The only time a thread would be cancelled is if it were to
	   fail to honor a more civil timeout request that times
	   out.  In these cases we assume the thread has gone into an
	   infinite loop or deadlocked or otherwise experienced some
	   unfortunate state.  Since deferred cancellation is
	   effective on condition waits, may be effective on
	   read-write locks and won't be effective on mutices,
	   asynchronous seems the way to go.  We would only do this
	   on the way to taking down the system in any case. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_type);

	rc = pthread_sigmask(SIG_SETMASK, NULL, &fe->ctx.sigmask);

	/* The only allowable errors are EFAULT and EINVAL, both of
	   which would indicate bugs in the code. */
	assert(rc == 0);

	if (fr->p.thread_initialize)
		fr->p.thread_initialize(&fe->ctx);

	do {
		fe->ctx.func(&fe->ctx);
		if (fr->p.task_cleanup)
			fr->p.task_cleanup(&fe->ctx);

		reschedule = fridgethr_freeze(fr, &fe->ctx);

	} while (reschedule);

	if (fr->p.thread_finalize)
		fr->p.thread_finalize(&fe->ctx);

	pthread_mutex_destroy(&fe->ctx.mtx);
	pthread_cond_destroy(&fe->ctx.cv);
	gsh_free(fe);
	fe = NULL;
	/* At this point the fridge entry no longer exists and must
	   not be accessed. */
	return NULL;
}

/**
 * @brief Do the actual work of spawning a thread
 *
 * @note This function must be called with the fridge mutex held and
 * it releases the fridge mutex.
 *
 * @param[in] fr   The fridge in which to spawn the thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return 0 on success or POSIX error codes.
 */

static int fridgethr_spawn(struct fridgethr *fr,
			   void (*func) (struct fridgethr_context *), void *arg)
{
	/* Return code */
	int rc = 0;
	/* Newly created thread entry */
	struct fridgethr_entry *fe = NULL;
	/* The mutex has/not been initialized */
	bool mutexed = false;
	/* The condition variable has/not been initialized */
	bool conditioned = false;

	fe = gsh_calloc(sizeof(struct fridgethr_entry), 1);
	if (fe == NULL)
		goto create_err;

	glist_init(&fe->thread_link);
	fe->fr = fr;
	rc = pthread_mutex_init(&fe->ctx.mtx, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize mutex for new thread "
			 "in fridge %s: %d", fr->s, rc);
		goto create_err;
	}
	mutexed = true;

	rc = pthread_cond_init(&fe->ctx.cv, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize condition variable "
			 "for new thread in fridge %s: %d", fr->s, rc);
		goto create_err;
	}
	conditioned = true;

	fe->ctx.func = func;
	fe->ctx.arg = arg;
	fe->frozen = false;

	rc = pthread_create(&fe->ctx.id, &fr->attr, fridgethr_start_routine,
			    fe);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to create new thread " "in fridge %s: %d",
			 fr->s, rc);
		goto create_err;
	}
#ifdef LINUX
	/* pthread_t is a 'pointer to struct' on FreeBSD vs
	   'unsigned long' on Linux */
	LogFullDebug(COMPONENT_THREAD,
		     "fr %p created thread %u (nthreads %u nidle %u)", fr,
		     (unsigned int)fe->ctx.id, fr->nthreads, fr->nidle);
#endif
	/* Make a new thread */
	++(fr->nthreads);

	glist_add_tail(&fr->thread_list, &fe->thread_link);
	PTHREAD_MUTEX_unlock(&fr->mtx);

	return rc;

 create_err:

	if (conditioned)
		pthread_cond_destroy(&fe->ctx.cv);

	if (mutexed)
		pthread_mutex_destroy(&fe->ctx.mtx);

	if (fe != NULL)
		gsh_free(fe);
	PTHREAD_MUTEX_unlock(&fr->mtx);

	return rc;
}

/**
 * @brief Queue a request
 *
 * Put a request on the queue and return immediately.
 *
 * @note This function must be called with the fridge lock held.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return 0 or POSIX errors.
 */

static int fridgethr_queue(struct fridgethr *fr,
			   void (*func) (struct fridgethr_context *), void *arg)
{
	/* Queue */
	struct fridgethr_work *q;

	assert(fr->p.deferment == fridgethr_defer_queue);

	q = gsh_malloc(sizeof(struct fridgethr_work));
	if (q == NULL) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Unable to allocate memory for "
			 "work queue item in fridge %s", fr->s);
		return ENOMEM;
	}
	glist_init(&q->link);
	q->func = func;
	q->arg = arg;
	glist_add_tail(&fr->deferment.work_q, &q->link);

	return 0;
}

/**
 * @brief Dispatch a job to an idle queue
 *
 * @note The fridge lock must be held when calling this routine.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return true if the job was successfully dispatched.
 */

static bool fridgethr_dispatch(struct fridgethr *fr,
			       void (*func) (struct fridgethr_context *),
			       void *arg)
{
	/* The entry for the found thread */
	struct fridgethr_entry *fe;
	/* Iterator over the list */
	struct glist_head *g = NULL;
	/* Saved pointer so we don't trash iteration */
	struct glist_head *n = NULL;
	/* If we successfully dispatched */
	bool dispatched = false;

	/* Try to grab a thread */
	glist_for_each_safe(g, n, &fr->idle_q) {
		fe = container_of(g, struct fridgethr_entry, idle_link);
		PTHREAD_MUTEX_lock(&fe->ctx.mtx);
		/* Get rid of a potential race condition
		   where the thread wakes up and exits or
		   otherwise redirects itself */
		if (fe->flags & fridgethr_flag_available) {
			glist_del(&fe->idle_link);
			--(fr->nidle);
			fe->ctx.func = func;
			fe->ctx.arg = arg;
			fe->frozen = false;
			fe->flags |= fridgethr_flag_dispatched;
			pthread_cond_signal(&fe->ctx.cv);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
			dispatched = true;
			break;
		}
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
	}

	return dispatched;
}

/**
 * @brief Block a request
 *
 * Block on thread availability and schedule a thread when one becomes
 * available.
 *
 * @note This function must be called with the fridge lock held.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return 0 or POSIX errors.
 */

static int fridgethr_block(struct fridgethr *fr,
			   void (*func) (struct fridgethr_context *), void *arg)
{
	/* Successfully dispatched */
	bool dispatched = true;
	/* Return code */
	int rc = 0;

	++(fr->deferment.block.waiters);
	do {
		if (fr->p.block_delay > 0) {
			struct timespec t;
			clock_gettime(CLOCK_REALTIME, &t);
			t.tv_sec += fr->p.block_delay > 0;
			rc = pthread_cond_timedwait(&fr->deferment.block.cond,
						    &fr->mtx, &t);
		} else {
			rc = pthread_cond_wait(&fr->deferment.block.cond,
					       &fr->mtx);
		}
		if (rc == 0) {
			switch (fr->command) {
			case fridgethr_comm_run:
				dispatched = fridgethr_dispatch(fr, func, arg);
				break;

			case fridgethr_comm_stop:
				rc = EPIPE;
				break;

			case fridgethr_comm_pause:
				/* Nothing, just go through the loop
				   again. */
				break;
			}
		}
	} while (!dispatched && (rc == 0));
	--(fr->deferment.block.waiters);
	/* We check here, too, in case we get around to falling out
	   after the last thread exited. */
	if ((fr->nthreads == 0) && (fr->command == fridgethr_comm_stop)
	    && (fr->transitioning) && !fridgethr_deferredwork(fr)) {
		/* We're the last thread to exit, signal the
		   transition to pause complete. */
		fridgethr_finish_transition(fr, false);
	}

	return rc;
}

/**
 * @brief Schedule a thread to perform a function
 *
 * This function finds an idle thread to perform func, creating one if
 * no thread is idle and we have not reached maxthreads.  If we have
 * reached maxthreads, defer the request in accord with the fridge's
 * deferment policy.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @retval 0 on success.
 * @retval EPIPE if the fridge is stopped.
 * @retval EWOULDBLOCK if no threads are available.
 * @retval Other POSIX return codes.
 */

int fridgethr_submit(struct fridgethr *fr,
		     void (*func) (struct fridgethr_context *), void *arg)
{
	/* Return code */
	int rc = 0;

	if (fr == NULL) {
		LogMajor(COMPONENT_THREAD,
			 "Attempt to schedule job with no fridge thread");
		return EPIPE;
	}

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->command == fridgethr_comm_stop) {
		LogMajor(COMPONENT_THREAD,
			 "Attempt to schedule job in stopped fridge %s.",
			 fr->s);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EPIPE;
	}

	if (fr->command == fridgethr_comm_pause) {
		LogFullDebug(COMPONENT_THREAD,
			     "Attempt to schedule job in paused "
			     "fridge %s, pausing.", fr->s);
		goto defer;
	}

	if (fr->nidle > 0) {
		if (fridgethr_dispatch(fr, func, arg)) {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return 0;
		}
	}

	if ((fr->p.thr_max == 0) || (fr->nthreads < fr->p.thr_max)) {
		rc = fridgethr_spawn(fr, func, arg);
	} else {
 defer:
		switch (fr->p.deferment) {
		case fridgethr_defer_queue:
			rc = fridgethr_queue(fr, func, arg);
			break;

		case fridgethr_defer_fail:
			rc = EWOULDBLOCK;
			break;

		case fridgethr_defer_block:
			rc = fridgethr_block(fr, func, arg);
		};
		PTHREAD_MUTEX_unlock(&fr->mtx);
	}

	return rc;
}

/**
 * @brief Wake idle threads
 *
 * This function is intended for use in fridgethr_flavor_looper
 * fridges, but nothing bad happens if you call it for
 * fridgethr_flavor_worker fridges.  It wakes all idle threads and
 * exits.
 *
 * @note If there are no idle threads we successfully do nothing.
 *
 * @param[in] fr   The fridge in which to find a thread
 *
 * @retval 0 all idle threads woke.
 * @retval EPIPE fridge is stopped or paused.
 */

int fridgethr_wake(struct fridgethr *fr)
{
	/* Iterator over the list */
	struct glist_head *g = NULL;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->command != fridgethr_comm_run) {
		LogMajor(COMPONENT_THREAD,
			 "Attempt to wake stopped/paused fridge %s.", fr->s);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EPIPE;
	}

	/* Wake the threads */
	glist_for_each(g, &fr->idle_q) {
		/* The entry for the found thread */
		struct fridgethr_entry *fe =
		    container_of(g, struct fridgethr_entry,
				 idle_link);
		PTHREAD_MUTEX_lock(&fe->ctx.mtx);
		pthread_cond_signal(&fe->ctx.cv);
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
	}

	PTHREAD_MUTEX_unlock(&fr->mtx);
	return 0;
}

/**
 * @brief Suspend execution in the fridge
 *
 * Simply change the state to pause.  If everything is already paused,
 * call the callback.
 *
 * @note Both @c mtx and @c cv may be NULL if you want to manage
 * synchrony without any help from the fridge.
 *
 * @param[in,out] fr  The fridge to pause
 * @param[in]     mtx Mutex (must be held when this function is called)
 * @param[in]     cv  Condition variable to be signalled on completion.
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 * @retval EINVAL if an invalid transition (from stopped to paused)
 *         was requested or one of @c mtx and @c cv was NULL but not
 *         both.
 */

int fridgethr_pause(struct fridgethr *fr, pthread_mutex_t *mtx,
		    pthread_cond_t *cv, void (*cb) (void *), void *arg)
{
	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Transition requested during transition in fridge %s",
			 fr->s);
		return EBUSY;
	}

	if ((mtx && !cv) || (cv && !mtx)) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD, "Iff, if you please: %s", fr->s);
		return EINVAL;
	}

	if (fr->command == fridgethr_comm_pause) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Do not pause that which is already paused: %s",
			 fr->s);
		return EALREADY;
	}

	if (fr->command == fridgethr_comm_stop) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Invalid transition, stop to pause: %s", fr->s);
		return EINVAL;
	}

	fr->command = fridgethr_comm_pause;
	fr->transitioning = true;
	fr->cb_mtx = mtx;
	fr->cb_cv = cv;
	fr->cb_func = cb;
	fr->cb_arg = arg;

	if (fr->nthreads == fr->nidle)
		fridgethr_finish_transition(fr, true);

	if (fr->p.wake_threads != NULL)
		fr->p.wake_threads(fr->p.wake_threads_arg);

	PTHREAD_MUTEX_unlock(&fr->mtx);
	return 0;
}

/**
 * @brief Slightly stupid workaround for an unlikely case
 *
 * @param[in] dummy Ignored
 */
static void fridgethr_noop(struct fridgethr_context *dummy)
{
	return;
}

/**
 * @brief Stop execution in the fridge
 *
 * Change state to stopped.  Wake up all the idlers so they stop,
 * too.  If there are no threads and the idle queue is empty, start
 * one up to finish any pending jobs.  (This can happen if we go
 * straight from paused to stopped.)
 *
 * @note Both @c mtx and @c cv may be NULL if you want to manage
 * synchrony without any help from the fridge.
 *
 * @param[in,out] fr  The fridge to pause
 * @param[in]     mtx Mutex (must be held when this function is called)
 * @param[in]     cv  Condition variable to be signalled on completion.
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 * @retval EINVAL if one of @c mtx and @c cv was NULL but not both.
 */

int fridgethr_stop(struct fridgethr *fr, pthread_mutex_t *mtx,
		   pthread_cond_t *cv, void (*cb) (void *), void *arg)
{
	int rc = 0;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Transition requested during transition in fridge %s",
			 fr->s);
		return EBUSY;
	}

	if (fr->command == fridgethr_comm_stop) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Do not stop that which is already stopped: %s",
			 fr->s);
		return EALREADY;
	}

	if ((mtx && !cv) || (cv && !mtx)) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD, "Iff, if you please: %s", fr->s);
		return EINVAL;
	}

	fr->command = fridgethr_comm_stop;
	fr->transitioning = true;
	fr->cb_mtx = mtx;
	fr->cb_cv = cv;
	fr->cb_func = cb;
	fr->cb_arg = arg;
	if ((fr->nthreads == 0) && !fridgethr_deferredwork(fr)) {
		fridgethr_finish_transition(fr, true);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return 0;
	}

	/* If we're a blocking fridge, let everyone know it's time to
	   fail. */
	if ((fr->p.deferment == fridgethr_defer_block)
	    && (fr->deferment.block.waiters > 0)) {
		pthread_cond_broadcast(&fr->deferment.block.cond);
	}

	if (fr->nthreads > 0) {
		/* Wake the idle! */

		/* Iterator over the list */
		struct glist_head *g = NULL;

		glist_for_each(g, &fr->idle_q) {
			struct fridgethr_entry *fe;

			fe = container_of(g,
					  struct
					  fridgethr_entry,
					  idle_link);

			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			/* We don't dispatch or anything, just wake
			   them all up and let them grab work off the
			   queue or terminate. */
			pthread_cond_signal(&fe->ctx.cv);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);

			if (fr->p.wake_threads != NULL)
				fr->p.wake_threads(fr->p.wake_threads_arg);
		}
		PTHREAD_MUTEX_unlock(&fr->mtx);
	} else {
		/* Well, this is embarrassing. */
		assert(fr->p.deferment != fridgethr_defer_fail);
		if (fr->p.deferment == fridgethr_defer_queue) {
			struct fridgethr_work *q =
			    glist_first_entry(&fr->deferment.work_q,
					      struct fridgethr_work,
					      link);
			glist_del(&q->link);
			rc = fridgethr_spawn(fr, q->func, q->arg);
			gsh_free(q);
		} else {
			/* Spawn a dummy to clean out the queue */
			rc = fridgethr_spawn(fr, fridgethr_noop, NULL);
		}
		PTHREAD_MUTEX_unlock(&fr->mtx);
	}
	return rc;
}

/**
 * @brief Start execution in the fridge
 *
 * Change state to running.  Wake up all the idlers.  If there's work
 * queued and we're below maxthreads, start some more threads.
 *
 * @note Both @c mtx and @c cv may be NULL if you want to manage
 * synchrony without any help from the fridge.
 *
 * @param[in,out] fr  The fridge to pause
 * @param[in]     mtx Mutex (must be held when this function is called)
 * @param[in]     cv  Condition variable to be signalled on completion.
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 */

int fridgethr_start(struct fridgethr *fr, pthread_mutex_t *mtx,
		    pthread_cond_t *cv, void (*cb) (void *), void *arg)
{
	/* Return code */
	int rc = 0;
	/* Cap on the number of threads to spawn, just so we know we
	   can terminate. */
	int maybe_spawn = 50;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Transition requested during transition in fridge %s",
			 fr->s);
		return EBUSY;
	}

	if (fr->command == fridgethr_comm_run) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Do not start that which is already started: %s",
			 fr->s);
		return EALREADY;
	}

	if ((mtx && !cv) || (cv && !mtx)) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD, "Iff, if you please: %s", fr->s);
		return EINVAL;
	}

	fr->command = fridgethr_comm_run;
	fr->transitioning = true;
	fr->cb_mtx = mtx;
	fr->cb_cv = cv;
	fr->cb_func = cb;
	fr->cb_arg = arg;
	if ((fr->nthreads == 0) && !fridgethr_deferredwork(fr)) {
		/* No work scheduled and no threads running, but
		   ready to accept requests once more. */
		fridgethr_finish_transition(fr, true);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return 0;
	}

	if (fr->nidle > 0) {
		/* Iterator over the list */
		struct glist_head *g = NULL;

		glist_for_each(g, &fr->idle_q) {
			struct fridgethr_entry *fe;

			fe = container_of(g,
					  struct
					  fridgethr_entry,
					  idle_link);

			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			/* We don't dispatch or anything, just wake
			   them all up and let them grab work off the
			   queue or terminate. */
			pthread_cond_signal(&fe->ctx.cv);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		}
	}

	while (fridgethr_deferredwork(fr) && (maybe_spawn-- > 0)
	       && ((fr->nthreads < fr->p.thr_max) || (fr->p.thr_max == 0))) {
		assert(fr->p.deferment != fridgethr_defer_block);
		/* Start some threads to finish the work */
		if (fr->p.deferment == fridgethr_defer_queue) {
			struct fridgethr_work *q =
			    glist_first_entry(&fr->deferment.work_q,
					      struct fridgethr_work,
					      link);
			glist_del(&q->link);
			rc = fridgethr_spawn(fr, q->func, q->arg);
			gsh_free(q);
			PTHREAD_MUTEX_lock(&fr->mtx);
			if (rc != 0)
				break;
		} else {
			rc = fridgethr_spawn(fr, fridgethr_noop, NULL);
			PTHREAD_MUTEX_lock(&fr->mtx);
			if (rc != 0)
				break;
		}
	}

	if (fr->p.wake_threads != NULL)
		fr->p.wake_threads(fr->p.wake_threads_arg);

	PTHREAD_MUTEX_unlock(&fr->mtx);
	return rc;
}

/**
 * @brief Set a flag to true, to prevent racing condition variable
 *
 * @param[in,out] flag Flag to set
 */

static void fridgethr_trivial_syncer(void *flag)
{
	*(bool *) flag = true;
}

/**
 * @brief Synchronously change the state of the fridge
 *
 * A convenience function that issues a state change and waits for it
 * to complete.
 *
 * @param[in,out] fr      The fridge to change
 * @param[in]     command Command to issue
 * @param[in]     timeout Number of seconds to wait for change or 0
 *                        to wait forever.
 *
 * @retval 0 Success.
 * @retval EINVAL invalid state change requested.
 * @retval EALREADY fridge already in requested state.
 * @retval EBUSY fridge currently in transition.
 * @retval ETIMEDOUT timed out on wait.
 */

int fridgethr_sync_command(struct fridgethr *fr, fridgethr_comm_t command,
			   time_t timeout)
{
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
	bool done = false;
	int rc = 0;
	struct timespec ts;

	PTHREAD_MUTEX_lock(&mtx);
	switch (command) {
	case fridgethr_comm_run:
		rc = fridgethr_start(fr, &mtx, &cv, fridgethr_trivial_syncer,
				     &done);
		break;

	case fridgethr_comm_pause:
		rc = fridgethr_pause(fr, &mtx, &cv, fridgethr_trivial_syncer,
				     &done);
		break;

	case fridgethr_comm_stop:
		rc = fridgethr_stop(fr, &mtx, &cv, fridgethr_trivial_syncer,
				    &done);
		break;

	default:
		rc = EINVAL;
	}

	if (rc != 0) {
		PTHREAD_MUTEX_unlock(&mtx);
		return rc;
	}

	if (timeout != 0) {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout;
	}

	while (!done) {
		if (timeout == 0) {
			rc = pthread_cond_wait(&cv, &mtx);
			assert(rc == 0);
		} else {
			rc = pthread_cond_timedwait(&cv, &mtx, &ts);
			if (rc == ETIMEDOUT)
				LogMajor(COMPONENT_THREAD,
					"Sync command seems to be stalled");
			else
				assert(rc == 0);
		}
	}
	PTHREAD_MUTEX_unlock(&mtx);
	return rc;
}

/**
 * @brief Return true if a looper function should return
 *
 * For the moment, this checks if we're in the middle of a state
 * transition.
 *
 * @param[in] ctx The thread context
 *
 * @retval true if you should break.
 * @retval false if you don't have to.  You still can if you want to.
 */

bool fridgethr_you_should_break(struct fridgethr_context *ctx)
{
	/* Entry for this thread */
	struct fridgethr_entry *fe = container_of(ctx, struct fridgethr_entry,
						  ctx);
	struct fridgethr *fr = fe->fr;
	bool rc;

	PTHREAD_MUTEX_lock(&fr->mtx);
	rc = fr->transitioning;
	PTHREAD_MUTEX_unlock(&fr->mtx);
	return rc;
}

/**
 * @brief Populate a fridge with threads all running the same thing
 *
 * @param[in,out] fr   Fridge to populate
 * @param[in]     func Function each thread should run
 * @param[in]     arg  Argument supplied for that function
 *
 * @retval 0 on success.
 * @retval EINVAL if there is no well-defined thread count.
 * @retval Other codes from thread creation.
 */

int fridgethr_populate(struct fridgethr *fr,
		      void (*func) (struct fridgethr_context *), void *arg)
{
	int threads_to_run;
	int i;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->p.thr_min != 0) {
		threads_to_run = fr->p.thr_min;
	} else if (fr->p.thr_max != 0) {
		threads_to_run = fr->p.thr_max;
	} else {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		LogMajor(COMPONENT_THREAD,
			 "Cannot populate fridge with undefined number of "
			 "threads: %s", fr->s);
		return EINVAL;
	}

	for (i = 0; i < threads_to_run; ++i) {
		struct fridgethr_entry *fe = NULL;
		int rc = 0;

		fe = gsh_calloc(sizeof(struct fridgethr_entry), 1);
		if (fe == NULL) {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return ENOMEM;
		}

		/* Make a new thread */
		++(fr->nthreads);

		glist_add_tail(&fr->thread_list, &fe->thread_link);

		fe->fr = fr;
		rc = pthread_mutex_init(&fe->ctx.mtx, NULL);
		if (rc != 0) {
			LogMajor(COMPONENT_THREAD,
				 "Unable to initialize mutex for new thread "
				 "in fridge %s: %d", fr->s, rc);
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return rc;
		}
		rc = pthread_cond_init(&fe->ctx.cv, NULL);
		if (rc != 0) {
			LogMajor(COMPONENT_THREAD,
				 "Unable to initialize condition variable "
				 "for new thread in fridge %s: %d", fr->s, rc);
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return rc;
		}

		fe->ctx.func = func;
		fe->ctx.arg = arg;
		fe->frozen = false;

		rc = pthread_create(&fe->ctx.id, &fr->attr,
				    fridgethr_start_routine, fe);
		if (rc != 0) {
			LogMajor(COMPONENT_THREAD,
				 "Unable to create new thread "
				 "in fridge %s: %d", fr->s, rc);
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return rc;
		}
	}
	PTHREAD_MUTEX_unlock(&fr->mtx);

	return 0;
}

/**
 * @brief Set the wait time of a running fridge
 *
 * @param[in] ctx          Thread context
 * @param[in] thread_delay New time delay
 */

void fridgethr_setwait(struct fridgethr_context *ctx, time_t thread_delay)
{
	struct fridgethr_entry *fe = container_of(ctx, struct fridgethr_entry,
						  ctx);
	struct fridgethr *fr = fe->fr;
	PTHREAD_MUTEX_lock(&fr->mtx);
	fr->p.thread_delay = thread_delay;
	PTHREAD_MUTEX_unlock(&fr->mtx);
}

/**
 * @brief Get the wait time of a running fridge
 *
 * @param[in] ctx Thread context
 */

time_t fridgethr_getwait(struct fridgethr_context *ctx)
{
	struct fridgethr_entry *fe = container_of(ctx, struct fridgethr_entry,
						  ctx);
	struct fridgethr *fr = fe->fr;
	time_t thread_delay = 0;

	PTHREAD_MUTEX_lock(&fr->mtx);
	thread_delay = fr->p.thread_delay;
	PTHREAD_MUTEX_unlock(&fr->mtx);
	return thread_delay;
}

/**
 * @brief Cancel all of the threads in the fridge
 *
 * This function is done only on Ganesha shutdown and only if a
 * shutdown request has been ignored.  We make no attempt to free the
 * fridge entries, since the threads are set detached and we're on the
 * way out anyway.
 *
 * @param[in,out] fr Fridge to cancel
 */

void fridgethr_cancel(struct fridgethr *fr)
{
	/* Thread iterator */
	struct glist_head *ti = NULL;
	/* Next thread link */
	struct glist_head *tn = NULL;

	pthread_mutex_lock(&fr->mtx);
	LogEvent(COMPONENT_THREAD, "Cancelling %d threads from fridge %s.",
		 fr->nthreads, fr->s);
	glist_for_each_safe(ti, tn, &fr->thread_list) {
		struct fridgethr_entry *t = glist_entry(ti,
							struct fridgethr_entry,
							thread_link);
		/* The only error we can get is no such thread.
		   Which means the thread isn't running.  Which is
		   good enough for me. */
		pthread_cancel(t->ctx.id);
		glist_del(&t->thread_link);
		--(fr->nthreads);
	}
	pthread_mutex_unlock(&fr->mtx);
	LogEvent(COMPONENT_THREAD, "All threads in %s cancelled.", fr->s);
}

struct fridgethr *general_fridge;

int general_fridge_init(void)
{
	struct fridgethr_params frp;
	int rc = 0;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 4;
	frp.thr_min = 0;
	frp.flavor = fridgethr_flavor_worker;
	frp.deferment = fridgethr_defer_queue;

	rc = fridgethr_init(&general_fridge, "Gen_Fridge", &frp);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize general fridge, error code %d.",
			 rc);
		return rc;
	}

	return 0;
}

int general_fridge_shutdown(void)
{
	int rc = fridgethr_sync_command(general_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_THREAD,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(general_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Failed shutting down general fridge: %d", rc);
	}

	return rc;
}

/** @} */
