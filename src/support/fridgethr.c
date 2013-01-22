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

int fridgethr_init(struct thr_fridge **frout,
		   const char *s,
		   const struct thr_fridge_params *p)
{
	/* The fridge under construction */
	struct thr_fridge *frobj
		= gsh_malloc(sizeof(struct thr_fridge));
	/* The return code for this function */
	int rc = 0;
	/* True if the thread attributes have been initialized */
	bool attrinit = false;
	/* True if the fridge mutex has been initialized */
	bool mutexinit = false;

	*frout  = NULL;
	if (frobj == NULL) {
		LogMajor(COMPONENT_DISPATCH,
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
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to initialize thread attributes for "
			 "fridge %s: %d", s, rc);
		goto out;
	}
	attrinit = true;
	rc = pthread_attr_setscope(&frobj->attr, PTHREAD_SCOPE_SYSTEM);
	if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to set thread scope for fridge %s: %d",
			 s, rc);
		goto out;
	}
	rc = pthread_attr_setdetachstate(&frobj->attr,
					 PTHREAD_CREATE_DETACHED);
	if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to set threads detached for fridge %s: %d",
			 s, rc);
		goto out;
	}
	/* This always succeeds on Linux (if you believe the manual),
	   but SUS defines errors. */
	rc = pthread_mutex_init(&frobj->mtx, NULL);
	if (rc != 0) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to initialize mutex for fridge %s: %d",
			 s, rc);
		goto out;
	}
	mutexinit = true;

	frobj->s = gsh_strdup(s);
	if (!frobj->s) {
		LogMajor(COMPONENT_DISPATCH,
			 "Unable to allocate memory in fridge %s",
			 s);
		rc = ENOMEM;
	}

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

void fridgethr_destroy(struct thr_fridge *fr)
{
	pthread_mutex_destroy(&fr->mtx);
	pthread_attr_destroy(&fr->attr);
	gsh_free(fr->s);
	gsh_free(fr);
}

static bool fridgethr_freeze(thr_fridge_t *fr,
			     struct fridge_thr_context *thr_ctx);

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
	fridge_entry_t *fe = (fridge_entry_t *) arg;
	thr_fridge_t *fr = fe->fr;
	bool reschedule;
	int rc = 0;

	SetNameFunction(fr->s);

	rc = pthread_sigmask(SIG_SETMASK, (sigset_t *) NULL,
			     &fe->ctx.sigmask);

	/* The only allowable errors are EFAULT and EINVAL, both of
	   which would indicate bugs in the code. */
	assert(rc == 0);

	do {
		fe->ctx.func(&fe->ctx);
		reschedule = fridgethr_freeze(fr, &fe->ctx);
	} while (reschedule);

	/* finalize this -- note that at present, fe is not on any
	 * thread queue */
	pthread_mutex_lock(&fr->mtx);
	--(fr->nthreads);
	pthread_mutex_unlock(&fr->mtx);

	pthread_mutex_destroy(&fe->ctx.mtx);
	pthread_cond_destroy(&fe->ctx.cv);
	gsh_free(fe);

	return (NULL);
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

int fridgethr_get(thr_fridge_t *fr,
		  void (*func)(fridge_thr_context_t *),
		  void *arg)
{
	/* The entry for the new/found thread */
	fridge_entry_t *fe;
	/* Return code */
	int rc = 0;
	/* The mutex has/not been initialized */
	bool mutexed = false;
	/* The condition variable has/not been initialized */
	bool conditioned = false;

	PTHREAD_MUTEX_lock(&fr->mtx);

	if (fr->nidle > 0) {
		/* Grab a thread and go */
		fe = glist_first_entry(&fr->idle_q, fridge_entry_t, q);
		glist_del(&fe->q);
		--(fr->nidle);

		PTHREAD_MUTEX_lock(&fe->ctx.mtx);

		fe->ctx.func = func;
		fe->ctx.arg = arg;
		fe->frozen = false;

		/* XXX reliable handoff */
		fe->flags |= fridgethr_flag_syncdone;
		if (fe->flags & fridgethr_flag_waitsync) {
			/* pthread_cond_signal never returns an
			   error, at least under Linux. */
			pthread_cond_signal(&fe->ctx.cv);
		}
		PTHREAD_MUTEX_unlock(&fe->ctx.mtx);
		PTHREAD_MUTEX_unlock(&fr->mtx);
	} else if ((fr->p.thr_max == 0) ||
		   (fr->nthreads < fr->p.thr_max)) {
		/* Make a enw thread */

		++(fr->nthreads);
		PTHREAD_MUTEX_unlock(&fr->mtx);

		fe = gsh_calloc(sizeof(fridge_entry_t), 1);
		if (fe == NULL) {
			goto create_err;
		}

		fe->fr = fr;
		rc = pthread_mutex_init(&fe->ctx.mtx, NULL);
		if (rc != 0) {
			LogMajor(COMPONENT_DISPATCH,
				 "Unable to initialize mutex for new thread "
				 "in fridge %s: %d",
				 fr->s, rc);
			goto create_err;
		}
		mutexed = true;

		rc = pthread_cond_init(&fe->ctx.cv, NULL);
		if (rc != 0) {
			LogMajor(COMPONENT_DISPATCH,
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
			LogMajor(COMPONENT_DISPATCH,
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
	} else {
		/* Queue */
		struct thr_work_queued *q = NULL;


		q = gsh_malloc(sizeof(struct thr_work_queued));
		if (q == NULL) {
			PTHREAD_MUTEX_unlock(&fr->mtx);
			LogMajor(COMPONENT_DISPATCH,
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
 * @brief Wait for more work
 *
 * This function, called by a worker thread, will cause it to wait for
 * more work (or exit).
 *
 * @retval true if we have more work to do.
 * @retval false if we need to go away.
 */

bool fridgethr_freeze(thr_fridge_t *fr, struct fridge_thr_context *thr_ctx)
{
	/* Entry for this thread */
	fridge_entry_t *fe = container_of(thr_ctx, fridge_entry_t, ctx);
	/* Return code from system calls */
	int rc = 0;

	pthread_mutex_lock(&fr->mtx);
	if (!glist_empty(&fr->work_q)) {
		struct thr_work_queued *q
			= glist_first_entry(&fr->work_q,
					    struct thr_work_queued,
					    link);
		glist_del(&q->link);
		pthread_mutex_unlock(&fr->mtx);
		fe->ctx.func = q->func;
		fe->ctx.arg = q->arg;
		gsh_free(q);
		return true;
	}
	glist_add_tail(&fr->idle_q, &fe->q);
	++(fr->nidle);

	pthread_mutex_lock(&fe->ctx.mtx);
	pthread_mutex_unlock(&fr->mtx);

	fe->frozen = true;
	fe->flags |= fridgethr_flag_waitsync;
	while (!(fe->flags & fridgethr_flag_syncdone)) {
		if (fr->p.expiration_delay_s > 0 ) {
			fe->timeout.tv_sec = time(NULL)
				+ fr->p.expiration_delay_s;
			fe->timeout.tv_nsec = 0;
			rc = pthread_cond_timedwait(&fe->ctx.cv,
						    &fe->ctx.mtx,
						    &fe->timeout);
		} else {
			rc = pthread_cond_wait(&fe->ctx.cv, &fe->ctx.mtx);
		}
	}

	fe->flags &= ~(fridgethr_flag_waitsync |
		       fridgethr_flag_syncdone);
	pthread_mutex_unlock(&fe->ctx.mtx);

	/* rescheduled */

	/* prints unreliable, nb */

	if (rc == 0) {
#ifdef LINUX
		/* pthread_t is a 'pointer to struct' on FreeBSD vs
		   'unsigned long' on Linux */
		LogFullDebug(COMPONENT_THREAD,
			     "fr %p re-use idle thread %u (nthreads %u "
			     "nidle %u)", fr,
			     (unsigned int) fe->ctx.id,
			     fr->nthreads, fr->nidle);
#endif
		return (true);
	}
	assert(rc == ETIMEDOUT); /* any other error is very bad */
#ifdef LINUX
	/* pthread_t is a 'pointer to struct' on FreeBSD vs 'unsigned
	   long' on Linux */
	LogFullDebug(COMPONENT_THREAD,
		     "fr %p thread %u idle out (nthreads %u nidle %u)",
		     fr, (unsigned int) fe->ctx.id, fr->nthreads, fr->nidle);
#endif

	return (false);
}
/** @} */
