/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    fridgethr.c
 * \brief   A small pthread-based thread pool.
 *
 * fridgethr.c : A small pthread-based thread pool.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/signal.h>

#include "fridgethr.h"

int
fridgethr_init(thr_fridge_t *fr, const char *s)
{
    fr->nthreads = 0;
    fr->nidle = 0;
    fr->thr_max = 0; /* XXX implement */
    fr->s = strdup(s);
    fr->flags = FridgeThr_Flag_None;

    pthread_attr_init(&fr->attr); 
    pthread_attr_setscope(&fr->attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&fr->attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&fr->mtx, NULL);

    /* idle threads q */
    init_glist(&fr->idle_q);

    return (0);
} /* fridgethr_init */

static bool
fridgethr_freeze(thr_fridge_t *fr, struct fridge_thr_context *thr_ctx);

static void *
fridgethr_start_routine(void *arg)
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

struct fridge_thr_context *
fridgethr_get(thr_fridge_t *fr, void *(*func)(void*),
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
        if (! pfe)
            Fatal();

        pfe->fr = fr;
        pfe->ctx.id = pthread_self();
        pthread_mutex_init(&pfe->ctx.mtx, NULL);
        pthread_cond_init(&pfe->ctx.cv, NULL);
        pfe->ctx.func = func;
        pfe->ctx.arg = arg;
        pfe->frozen = FALSE;

        retval = pthread_create(&pfe->ctx.id, &fr->attr/*  NULL */, fridgethr_start_routine,
				pfe);
	if(retval) {
		LogCrit(COMPONENT_THREAD,
			"pthread_create bogus: %d", errno);
		assert(errno == 0);
	}

        LogFullDebug(COMPONENT_THREAD,
                "fr %p created thread %u (nthreads %u nidle %u)",
                     fr, (unsigned int) pfe->ctx.id, fr->nthreads, fr->nidle);

        goto out;
    }

    pfe = glist_first_entry(&fr->idle_q, fridge_entry_t, q);
    glist_del(&pfe->q);
    --(fr->nidle);

    pthread_mutex_lock(&pfe->ctx.mtx);

    pfe->ctx.func = func;
    pfe->ctx.arg = arg;
    pfe->frozen = FALSE;

    /* XXX reliable handoff */
    pfe->flags |= FridgeThr_Flag_SyncDone;
    if (pfe->flags & FridgeThr_Flag_WaitSync) {
        pthread_cond_signal(&pfe->ctx.cv);
    }
    pthread_mutex_unlock(&pfe->ctx.mtx);
    pthread_mutex_unlock(&fr->mtx);

out:
    return (&(pfe->ctx));
} /* fridgethr_get */

bool
fridgethr_freeze(thr_fridge_t *fr, struct fridge_thr_context *thr_ctx)
{
    fridge_entry_t *pfe = container_of(thr_ctx, fridge_entry_t, ctx);
    int rc = 0;

    pthread_mutex_lock(&fr->mtx);
    glist_add_tail(&fr->idle_q, &pfe->q);
    ++(fr->nidle);

    pthread_mutex_lock(&pfe->ctx.mtx);
    pthread_mutex_unlock(&fr->mtx);

    pfe->frozen = TRUE;
    pfe->flags |= FridgeThr_Flag_WaitSync;

    while (! (pfe->flags & FridgeThr_Flag_SyncDone)) {
        if (fr->expiration_delay_s > 0 ) {
            (void) gettimeofday(&pfe->tp, NULL);
            pfe->timeout.tv_sec = pfe->tp.tv_sec + fr->expiration_delay_s;
            pfe->timeout.tv_nsec = 0; 
            rc = pthread_cond_timedwait(&pfe->ctx.cv, &pfe->ctx.mtx,
                                        &pfe->timeout);
        }
        else
            rc = pthread_cond_wait(&pfe->ctx.cv, &pfe->ctx.mtx);
    }

    pfe->flags &= ~(FridgeThr_Flag_WaitSync|FridgeThr_Flag_SyncDone);
    pthread_mutex_unlock(&pfe->ctx.mtx);

    /* rescheduled */

    /* prints unreliable, nb */

    if (rc != ETIMEDOUT) {
        LogFullDebug(COMPONENT_THREAD,
                "fr %p re-use idle thread %u (nthreads %u nidle %u)",
                fr, (unsigned int)pfe->ctx.id, fr->nthreads, fr->nidle);
        return (TRUE);
    }

    LogFullDebug(COMPONENT_THREAD,
            "fr %p thread %u idle out (nthreads %u nidle %u)",
            fr, (unsigned int)pfe->ctx.id, fr->nthreads, fr->nidle);

    return (FALSE);
} /* fridgethr_freeze */
