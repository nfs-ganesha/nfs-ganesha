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
	frobj->flags = FridgeThr_Flag_None;

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
	if (frobj->p.stacksize != 0) {
		rc = pthread_attr_setstacksize(&frobj->attr,
					       frobj->p.stacksize);
		if (rc != 0) {
			LogMajor(COMPONENT_DISPATCH,
				 "Unable to set thread stack size for "
				 "fridge %s: %d", s, rc);
			goto out;
		}
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

	/* idle threads q */
	init_glist(&frobj->idle_q);

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
 * @param[in] arg The firdge entry for this thread
 *
 * @return NULL.
 */

static void *fridgethr_start_routine(void *arg)
{
	fridge_entry_t *pfe = (fridge_entry_t *) arg;
	thr_fridge_t *fr = pfe->fr;
	bool reschedule;

	SetNameFunction(fr->s);

	(void) pthread_sigmask(SIG_SETMASK, (sigset_t *) NULL,
			       &pfe->ctx.sigmask);
	do {
		(void) pfe->ctx.func(&pfe->ctx);
		reschedule = fridgethr_freeze(fr, &pfe->ctx);
	} while (reschedule);

	/* finalize this -- note that at present, pfe is not on any
	 * thread queue */
	pthread_mutex_lock(&fr->mtx);
	--(fr->nthreads);
	pthread_mutex_unlock(&fr->mtx);

	pthread_mutex_destroy(&pfe->ctx.mtx);
	pthread_cond_destroy(&pfe->ctx.cv);
	gsh_free(pfe);

	return (NULL);
}

/**
 * @brief Schedule a thread to perform a function
 *
 * This function finds an idle thread to perform func, creating one if
 * no thread is idle.
 *
 * @todo Support maxthreads.
 *
 * @param[in] fr   The fridge in which to find a thread
 * @param[in] func The thing to do
 * @param[in] arg  The thing to do it to
 *
 * @return The context of the thread got.
 */

struct fridge_thr_context *fridgethr_get(thr_fridge_t *fr,
					 void *(*func)(void*),
					 void *arg)
{
	fridge_entry_t *pfe;
	int retval = 0;

	pthread_mutex_lock(&fr->mtx);

	if (fr->nidle == 0) {
		/* fr accting */
		++(fr->nthreads);
		pthread_mutex_unlock(&fr->mtx);

		/* new thread */
		pfe = (fridge_entry_t *) gsh_calloc(sizeof(fridge_entry_t), 1);
		if (!pfe)
			Fatal();

		pfe->fr = fr;
		pfe->ctx.id = pthread_self();
		pthread_mutex_init(&pfe->ctx.mtx, NULL);
		pthread_cond_init(&pfe->ctx.cv, NULL);
		pfe->ctx.func = func;
		pfe->ctx.arg = arg;
		pfe->frozen = false;

		retval = pthread_create(&pfe->ctx.id, &fr->attr,
					fridgethr_start_routine,
					pfe);
		if (retval) {
			LogCrit(COMPONENT_THREAD,
				"pthread_create bogus: %d", errno);
			return NULL;
		}

#ifdef LINUX
		/* pthread_t is a 'pointer to struct' on FreeBSD vs
		   'unsigned long' on Linux */
		LogFullDebug(COMPONENT_THREAD,
			     "fr %p created thread %u (nthreads %u nidle %u)",
			     fr, (unsigned int) pfe->ctx.id,
			     fr->nthreads,
			     fr->nidle);
#endif

		goto out;
	}

	pfe = glist_first_entry(&fr->idle_q, fridge_entry_t, q);
	glist_del(&pfe->q);
	--(fr->nidle);

	pthread_mutex_lock(&pfe->ctx.mtx);

	pfe->ctx.func = func;
	pfe->ctx.arg = arg;
	pfe->frozen = false;

	/* XXX reliable handoff */
	pfe->flags |= FridgeThr_Flag_SyncDone;
	if (pfe->flags & FridgeThr_Flag_WaitSync) {
		pthread_cond_signal(&pfe->ctx.cv);
	}
	pthread_mutex_unlock(&pfe->ctx.mtx);
	pthread_mutex_unlock(&fr->mtx);

out:
	return (&(pfe->ctx));
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
	fridge_entry_t *pfe = container_of(thr_ctx, fridge_entry_t, ctx);
	int rc = 0;

	pthread_mutex_lock(&fr->mtx);
	glist_add_tail(&fr->idle_q, &pfe->q);
	++(fr->nidle);

	pthread_mutex_lock(&pfe->ctx.mtx);
	pthread_mutex_unlock(&fr->mtx);

	pfe->frozen = true;
	pfe->flags |= FridgeThr_Flag_WaitSync;
	while (!(pfe->flags & FridgeThr_Flag_SyncDone)) {
		if (fr->p.expiration_delay_s > 0 ) {
			pfe->timeout.tv_sec = time(NULL)
				+ fr->p.expiration_delay_s;
			pfe->timeout.tv_nsec = 0;
			rc = pthread_cond_timedwait(&pfe->ctx.cv,
						    &pfe->ctx.mtx,
						    &pfe->timeout);
		} else {
			rc = pthread_cond_wait(&pfe->ctx.cv, &pfe->ctx.mtx);
		}
	}

	pfe->flags &= ~(FridgeThr_Flag_WaitSync |
			FridgeThr_Flag_SyncDone);
	pthread_mutex_unlock(&pfe->ctx.mtx);

	/* rescheduled */

	/* prints unreliable, nb */

	if (rc == 0) {
#ifdef LINUX
		/* pthread_t is a 'pointer to struct' on FreeBSD vs
		   'unsigned long' on Linux */
		LogFullDebug(COMPONENT_THREAD,
			     "fr %p re-use idle thread %u (nthreads %u "
			     "nidle %u)", fr, (unsigned int)
			     pfe->ctx.id,
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
		     fr, (unsigned int) pfe->ctx.id, fr->nthreads, fr->nidle);
#endif

	return (false);
}
/** @} */
