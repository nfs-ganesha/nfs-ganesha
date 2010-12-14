/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/thread.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

/*ARGSUSED*/
void
cv_init(kcondvar_t *cv, char *name, int type, void *arg)
{
	ASSERT(type == CV_DEFAULT);

	VERIFY(pthread_cond_init(cv, NULL) == 0);
}

void
cv_destroy(kcondvar_t *cv)
{
	VERIFY(pthread_cond_destroy(cv) == 0);
}

void
cv_wait(kcondvar_t *cv, kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	VERIFY(pthread_cond_wait(cv, &mp->m_lock) == 0);
	mp->m_owner = curthread;
}

clock_t
cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime)
{
	int error;
	struct timespec ts;
	struct timeval tv;
	clock_t delta;

top:
	delta = abstime - lbolt;

	if (delta <= 0)
		return (-1);

	VERIFY(gettimeofday(&tv, NULL) == 0);

	ts.tv_sec = tv.tv_sec + delta / hz;
	ts.tv_nsec = tv.tv_usec * 1000 + (delta % hz) * (NANOSEC / hz);
	ASSERT(ts.tv_nsec >= 0);

	if(ts.tv_nsec >= NANOSEC) {
		ts.tv_sec++;
		ts.tv_nsec -= NANOSEC;
	}

	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	error = pthread_cond_timedwait(cv, &mp->m_lock, &ts);
	mp->m_owner = curthread;

	if (error == EINTR)
		goto top;

	if (error == ETIMEDOUT)
		return (-1);

	ASSERT(error == 0);

	return (1);
}

void
cv_signal(kcondvar_t *cv)
{
	VERIFY(pthread_cond_signal(cv) == 0);
}

void
cv_broadcast(kcondvar_t *cv)
{
	VERIFY(pthread_cond_broadcast(cv) == 0);
}
