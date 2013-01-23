/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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

#include "fridgethr.h"

/**
 * @brief Initialize a thread fridge
 *
 * @param[out] frout The fridge to initialize
 * @param[in]  s     The name of the fridge
 * @param[in]  p     Fridge parameters
 *
 * @return 0 on success, POSIX errors on failure.
 */

int fridgethr_init(struct fridgethr **frout,
		   const char *s,
		   const struct fridgethr_params *p)
{
	/* The fridge under construction */
	struct fridgethr *frobj
		= gsh_malloc(sizeof(struct fridgethr));
	/* The return code for this function */
	int rc = 0;
	/* True if the thread attributes have been initialized */
	bool attrinit = false;
	/* True if the fridge mutex has been initialized */
	bool mutexinit = false;

	*frout  = NULL;
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
			 "Unable to set thread scope for fridge %s: %d",
			 s, rc);
		goto out;
	}
	rc = pthread_attr_setdetachstate(&frobj->attr,
					 PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to set threads detached for fridge %s: %d",
			 s, rc);
		goto out;
	}
	/* This always succeeds on Linux (if you believe the manual),
	   but SUS defines errors. */
	rc = pthread_mutex_init(&frobj->mtx, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize mutex for fridge %s: %d",
			 s, rc);
		goto out;
	}
	mutexinit = true;

	frobj->s = gsh_strdup(s);
	if (!frobj->s) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to allocate memory in fridge %s",
			 s);
		rc = ENOMEM;
	}

	frobj->command = fridgethr_comm_run;
	frobj->transitioning = false;

	/* Idle threads queue */
	init_glist(&frobj->idle_q);

	/* Work queue */
	init_glist(&frobj->work_q);

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
 * Notify whoever cares taht we're done and mark the transition as
 * complete.  The fridge lock must be held when calling this
 * function.
 *
 * @todo ACE: I am not really happy with spawning a thread just to
 * call the completion function, but if we're going to want to be able
 * to do things like wait for the transition to finish, we need to
 * make sure that the completion function is called in a different
 * thread than the one that requested the state change.
 *
 * @param[in,out] fr The fridge
 */

static void fridgethr_finish_transition(struct fridgethr *fr)
{
	if ((fr->transitioning == true) &&
	    (fr->cb_func != NULL)) {
		int rc = 0;
		pthread_t thr;

		rc = pthread_create(&thr, &fr->attr,
				    (void *) fr->cb_func,
				    fr->cb_arg);
		if (rc != 0) {
			/* There is nothing good to do in this
			   situation but log. */
			LogMajor(COMPONENT_THREAD,
				 "Unable to create thread for callback "
				 "in fridge %s: %d",
				 fr->s, rc);
		}
	}
	fr->cb_func = NULL;
	fr->cb_arg = NULL;
	fr->transitioning = false;
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
	struct fridgethr_entry *fe
		= container_of(thr_ctx, struct fridgethr_entry,
			       ctx);
	/* Return code from system calls */
	int rc = 0;

	PTHREAD_MUTEX_lock(&fr->mtx);
restart:
	/* If we are not paused and there is work left to do in the
	   queue, do it. */
	if (!glist_empty(&fr->work_q) &&
	    !(fr->command == fridgethr_comm_pause)) {
		struct fridgethr_work *q
			= glist_first_entry(&fr->work_q,
					    struct fridgethr_work,
					    link);
		glist_del(&q->link);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		fe->ctx.func = q->func;
		fe->ctx.arg = q->arg;
		gsh_free(q);
		return true;
	}

	/* If there is no work to do in the queue and we either timed
	   out from the last cond wait OR we have been told to stop,
	   destroy ourselves. */
	if ((rc == ETIMEDOUT) ||
	    (fr->command == fridgethr_comm_stop)) {
		/* We  do this here since we already have the fridge
		   lock. */
		--(fr->nthreads);
		if ((fr->nthreads == 0) &&
		    (fr->command == fridgethr_comm_stop) &&
		    (fr->transitioning)) {
			/* We're the last thread to exit, signal the
			   transition to pause complete. */
			fridgethr_finish_transition(fr);
		}
		PTHREAD_MUTEX_lock(&fe->ctx.mtx);
		fe->ctx.func = NULL;
		fe->ctx.arg = NULL;
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		pthread_mutex_destroy(&fe->ctx.mtx);
		pthread_cond_destroy(&fe->ctx.cv);
		gsh_free(fe);
		return false;
	}

	assert(fr->command != fridgethr_comm_stop);
	glist_add_tail(&fr->idle_q, &fe->q);
	++(fr->nidle);
	if ((fr->nidle == fr->nthreads) &&
	    (fr->command == fridgethr_comm_pause) &&
	    (fr->transitioning)) {
		/* We're the last thread to suspend, signal the
		   transition to pause complete. */
		fridgethr_finish_transition(fr);
	}

	PTHREAD_MUTEX_lock(&fe->ctx.mtx);
	fe->frozen = true;
	fe->flags |= fridgethr_flag_available;
	PTHREAD_MUTEX_unlock(&fr->mtx);

	/* It is a state machine, keep going until we have a
	   transition that gets us out.*/
	while (true) {
		if (fr->p.expiration_delay_s > 0 ) {
			clock_gettime(CLOCK_REALTIME,
				      &fe->timeout);
			fe->timeout.tv_sec += fr->p.expiration_delay_s;
			rc = pthread_cond_timedwait(&fe->ctx.cv,
						    &fe->ctx.mtx,
						    &fe->timeout);
		} else {
			rc = pthread_cond_wait(&fe->ctx.cv, &fe->ctx.mtx);
		}

		/* Clear this while we have the lock, we can set it
		   again before continuing */
		fe->frozen = false;

		/* It's repetition, but it saves us from having to
		   drop and then reacquire the lock later. */
		if (fe->flags & fridgethr_flag_dispatched) {
			fe->flags &= ~(fridgethr_flag_available |
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
		if (fr->command != fridgethr_comm_stop &&
		    ((fr->command == fridgethr_comm_pause) ||
		     (glist_empty(&fr->work_q)))) {
			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			PTHREAD_MUTEX_unlock(&fr->mtx);
			fe->frozen = true;
			fe->flags |= fridgethr_flag_available;
			continue;
		}

		--(fr->nidle);
		glist_del(&fe->q);
		goto restart;
	}

	/* We were already unfrozen and taken off the idle queue, so
	   there's nothing more to do than: */
	return true;
}

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

	SetNameFunction(fr->s);

	rc = pthread_sigmask(SIG_SETMASK, NULL,
			     &fe->ctx.sigmask);

	/* The only allowable errors are EFAULT and EINVAL, both of
	   which would indicate bugs in the code. */
	assert(rc == 0);

	do {
		fe->ctx.func(&fe->ctx);
		reschedule = fridgethr_freeze(fr, &fe->ctx);
	} while (reschedule);

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
			   void (*func)(struct fridgethr_context *),
			   void *arg)
{
	/* Return code */
	int rc = 0;
	/* Newly created thread entry */
	struct fridgethr_entry *fe = NULL;
	/* The mutex has/not been initialized */
	bool mutexed = false;
	/* The condition variable has/not been initialized */
	bool conditioned = false;

	/* Make a enw thread */
	++(fr->nthreads);
	PTHREAD_MUTEX_unlock(&fr->mtx);
	fe = gsh_calloc(sizeof(struct fridgethr_entry), 1);
	if (fe == NULL) {
		goto create_err;
	}

	fe->fr = fr;
	rc = pthread_mutex_init(&fe->ctx.mtx, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize mutex for new thread "
			 "in fridge %s: %d",
			 fr->s, rc);
		goto create_err;
	}
	mutexed = true;

	rc = pthread_cond_init(&fe->ctx.cv, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to initialize condition variable "
			 "for new thread in fridge %s: %d",
			 fr->s, rc);
		goto create_err;
	}
	conditioned = true;

	fe->ctx.func = func;
	fe->ctx.arg = arg;
	fe->frozen = false;

	rc = pthread_create(&fe->ctx.id, &fr->attr,
			    fridgethr_start_routine,
			    fe);
	if (rc != 0) {
		LogMajor(COMPONENT_THREAD,
			 "Unable to create new thread "
			 "in fridge %s: %d", fr->s, rc);
		goto create_err;
	}

#ifdef LINUX
	/* pthread_t is a 'pointer to struct' on FreeBSD vs
	   'unsigned long' on Linux */
	LogFullDebug(COMPONENT_THREAD,
		     "fr %p created thread %u (nthreads %u nidle %u)",
		     fr, (unsigned int) fe->ctx.id,
		     fr->nthreads,
		     fr->nidle);
#endif

	return rc;

create_err:

	PTHREAD_MUTEX_lock(&fr->mtx);
	--(fr->nthreads);
	PTHREAD_MUTEX_unlock(&fr->mtx);

	if (conditioned) {
		pthread_cond_destroy(&fe->ctx.cv);
	}

	if (mutexed) {
		pthread_mutex_destroy(&fe->ctx.mtx);
	}

	if (fe != NULL) {
		gsh_free(free);
	}

	return rc;
}

/**
 * @brief Schedule a thread to perform a function
 *
 * This function finds an idle thread to perform func, creating one if
 * no thread is idle and we have not reached maxthreads.  If we have
 * reached maxthreads, queue the request.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return 0 on success or POSIX error codes.
 */

int fridgethr_get(struct fridgethr *fr,
		  void (*func)(struct fridgethr_context *),
		  void *arg)
{
	/* The entry for the new/found thread */
	struct fridgethr_entry *fe;
	/* Return code */
	int rc = 0;

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
		goto queue;
	}

	if (fr->nidle > 0) {
		/* Iterator over the list */
		struct glist_head *g = NULL;
		/* Saved pointer so we don't trash iteration */
		struct glist_head *n = NULL;
		/* If we successfully dispatched */
		bool dispatched = false;

		/* Try to grab a thread */
		glist_for_each_safe(g, n, &fr->idle_q) {
			fe = container_of(g, struct fridgethr_entry, q);
			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			/* Get rid of a potential race condition
			   where the thread wakes up and exits or
			   otherwise redirects itself */
			if (fe->flags & fridgethr_flag_available) {
				glist_del(&fe->q);
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
		if (dispatched) {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			return 0;
		}
	}

	if ((fr->p.thr_max == 0) ||
	    (fr->nthreads < fr->p.thr_max)) {
		rc = fridgethr_spawn(fr, func, arg);
	} else {
		/* Queue */
		struct fridgethr_work *q;

	queue:
		q = gsh_malloc(sizeof(struct fridgethr_work));
		if (q == NULL) {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			LogMajor(COMPONENT_THREAD,
				 "Unable to allocate memory for "
				 "work queue item in fridge %s", fr->s);
			return ENOMEM;
		}
		init_glist(&q->link);
		q->func = func;
		q->arg = arg;
		glist_add_tail(&fr->work_q, &q->link);
		PTHREAD_MUTEX_unlock(&fr->mtx);
	}

	return rc;
}

/**
 * @brief Suspend execution in the fridge
 *
 * Simply change the state to pause.  If everything is already paused,
 * call the callback.
 *
 * @param[in,out] fr  The fridge to pause
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 * @retval EINVAL if an invalid transition (from stopped to paused)
 *         was requested.
 */

int fridgethr_pause(struct fridgethr *fr,
		    void (*cb)(void *),
		    void *arg)
{
	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EBUSY;
	}

	if (fr->command == fridgethr_comm_pause) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EALREADY;
	}

	if (fr->command == fridgethr_comm_stop) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EINVAL;
	}

	fr->command = fridgethr_comm_pause;
	fr->transitioning = true;
	fr->cb_func = cb;
	fr->cb_arg = arg;
	if (fr->nthreads == fr->nidle) {
		fridgethr_finish_transition(fr);
	}
	PTHREAD_MUTEX_unlock(&fr->mtx);
	return 0;
}

/**
 * @brief Stop execution in the fridge
 *
 * Change state to stopped.  Wake up all the idlers so they stop,
 * too.  If there are no threads and the idle queue is empty, start
 * one up to finish any pending jobs.  (This can happen if we go
 * straight from paused to stopped.)
 *
 * @param[in,out] fr  The fridge to pause
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 */

int fridgethr_stop(struct fridgethr *fr,
		   void (*cb)(void *),
		   void *arg)
{
	int rc = 0;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EBUSY;
	}

	if (fr->command == fridgethr_comm_stop) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EALREADY;
	}

	fr->command = fridgethr_comm_stop;
	fr->transitioning = true;
	fr->cb_func = cb;
	fr->cb_arg = arg;
	if ((fr->nthreads == 0) &&
	    glist_empty(&fr->work_q)) {
		fridgethr_finish_transition(fr);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return 0;
	}

	if (fr->nthreads > 0) {
		/* Wake the idle! */

		/* Iterator over the list */
		struct glist_head *g = NULL;

		glist_for_each(g, &fr->idle_q) {
			struct fridgethr_entry *fe
				= container_of(g, struct fridgethr_entry, q);
			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			/* We don't dispatch or anything, just wake
			   them all up and let them grab work off the
			   queue or terminate. */
			pthread_cond_signal(&fe->ctx.cv);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		}
		PTHREAD_MUTEX_unlock(&fr->mtx);
	} else {
		/* Well, this is embarrassing. */
		struct fridgethr_work *q
			= glist_first_entry(&fr->work_q,
					    struct fridgethr_work,
					    link);
		glist_del(&q->link);
		rc = fridgethr_spawn(fr, q->func, q->arg);
		gsh_free(q);
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
 * @param[in,out] fr  The fridge to pause
 * @param[in]     cb  Function to call once all threads are paused
 * @param[in]     arg Argument to supply
 *
 * @retval 0 on success.
 * @retval EBUSY if a state transition is in progress.
 * @retval EALREADY if the fridge is already paused.
 */

int fridgethr_start(struct fridgethr *fr,
		    void (*cb)(void *),
		    void *arg)
{
	/* Return code */
	int rc = 0;
	/* Cap on the number of threads to spawn, just so we know we
	   can terminate. */
	int maybe_spawn = 50;

	PTHREAD_MUTEX_lock(&fr->mtx);
	if (fr->transitioning) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EBUSY;
	}

	if (fr->command == fridgethr_comm_run) {
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return EALREADY;
	}

	fr->command = fridgethr_comm_run;
	fr->transitioning = true;
	fr->cb_func = cb;
	fr->cb_arg = arg;
	if ((fr->nthreads == 0) &&
	    glist_empty(&fr->work_q)) {
		/* No work scheduled and no threads running, but
		   ready to accept requests once more. */
		fridgethr_finish_transition(fr);
		PTHREAD_MUTEX_unlock(&fr->mtx);
		return 0;
	}

	if (fr->nidle > 0) {
		/* Iterator over the list */
		struct glist_head *g = NULL;

		glist_for_each(g, &fr->idle_q) {
			struct fridgethr_entry *fe
				= container_of(g, struct fridgethr_entry, q);
			PTHREAD_MUTEX_lock(&fe->ctx.mtx);
			/* We don't dispatch or anything, just wake
			   them all up and let them grab work off the
			   queue or terminate. */
			pthread_cond_signal(&fe->ctx.cv);
			PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		}
	}


	while ((!glist_empty(&fr->work_q)) &&
	       (maybe_spawn-- > 0) &&
	       (fr->nthreads < fr->p.thr_max)) {
		/* Start some threads to finish the work */
		struct fridgethr_work *q
			= glist_first_entry(&fr->work_q,
					    struct fridgethr_work,
					    link);
		glist_del(&q->link);
		rc = fridgethr_spawn(fr, q->func, q->arg);
		gsh_free(q);
		PTHREAD_MUTEX_lock(&fr->mtx);
		if (rc != 0) {
			break;
		}
	}
	PTHREAD_MUTEX_unlock(&fr->mtx);
	return rc;
}

/**
 * @brief Acquire a mutex and signal a condition variable
 *
 * @param[in] arg An array whose first argument is a mutex and whose
 *                second is a condition variable
 */

static void fridgethr_state_signal(void *arg)
{
	void **array = arg;
	pthread_mutex_t *mtx = array[0];
	pthread_cond_t *cond = array[1];

	PTHREAD_MUTEX_lock(mtx);
	pthread_cond_signal(cond);
	PTHREAD_MUTEX_unlock(mtx);
}


/**
 * @brief Synchronously change the state of the fridge
 *
 * A convenience function that issues a state change and waits for it
 * to complete.
 *
 * @todo This is a gross hack, fix it up later after we check that
 * state change actually works.
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

int fridgethr_sync_command(struct fridgethr *fr,
			   fridgethr_comm_t command,
			   time_t timeout)
{
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	void *arg[2] = {
		&mtx, &cond
	};
	int rc = 0;

	PTHREAD_MUTEX_lock(&mtx);
	switch (command) {
	case fridgethr_comm_run:
		rc = fridgethr_start(fr,
				     fridgethr_state_signal,
				     arg);
		break;

	case fridgethr_comm_pause:
		rc = fridgethr_pause(fr,
				     fridgethr_state_signal,
				     arg);
		break;

	case fridgethr_comm_stop:
		rc = fridgethr_stop(fr,
				    fridgethr_state_signal,
				    arg);
		break;

	default:
		rc = EINVAL;
	}

	if (rc != 0) {
		PTHREAD_MUTEX_unlock(&mtx);
		return rc;
	}

	if (timeout == 0) {
		rc = pthread_cond_wait(&cond, &mtx);
	} else {
		struct timespec t;
		clock_gettime(CLOCK_REALTIME, &t);
		t.tv_sec += timeout;
		rc = pthread_cond_timedwait(&cond, &mtx, &t);
	}
	if (rc == 0) {
		PTHREAD_MUTEX_unlock(&mtx);
	}

	return rc;
}

/** @} */
