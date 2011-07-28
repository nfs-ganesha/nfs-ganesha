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
#include <sys/rwlock.h>
#include <sys/thread.h>
#include <sys/types.h>

#include <pthread.h>
#include <errno.h>

/*ARGSUSED*/
void
rw_init(krwlock_t *rwlp, char *name, int type, void *arg)
{
	VERIFY(pthread_rwlock_init(&rwlp->rw_lock, NULL) == 0);
	zmutex_init(&rwlp->mutex);
	rwlp->rw_owner = NULL;
	rwlp->thr_count = 0;
}

void
rw_destroy(krwlock_t *rwlp)
{
	VERIFY(pthread_rwlock_destroy(&rwlp->rw_lock) == 0);
	zmutex_destroy(&rwlp->mutex);
	rwlp->rw_owner = (void *)-1UL;
	rwlp->thr_count = -2;
}

void
rw_enter(krwlock_t *rwlp, krw_t rw)
{
	//ASSERT(!RW_LOCK_HELD(rwlp));
	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_owner != curthread);

	if (rw == RW_READER) {
		VERIFY(pthread_rwlock_rdlock(&rwlp->rw_lock) == 0);

		mutex_enter(&rwlp->mutex);
		ASSERT(rwlp->thr_count >= 0);
		rwlp->thr_count++;
		mutex_exit(&rwlp->mutex);
		ASSERT(rwlp->rw_owner == NULL);
	} else {
		VERIFY(pthread_rwlock_wrlock(&rwlp->rw_lock) == 0);

		ASSERT(rwlp->rw_owner == NULL);
		ASSERT(rwlp->thr_count == 0);
		rwlp->thr_count = -1;
		rwlp->rw_owner = curthread;
	}
}

void
rw_exit(krwlock_t *rwlp)
{
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	if(rwlp->rw_owner == curthread) {
		/* Write locked */
		ASSERT(rwlp->thr_count == -1);
		rwlp->thr_count = 0;
		rwlp->rw_owner = NULL;
	} else {
		/* Read locked */
		ASSERT(rwlp->rw_owner == NULL);
		mutex_enter(&rwlp->mutex);
		ASSERT(rwlp->thr_count >= 1);
		rwlp->thr_count--;
		mutex_exit(&rwlp->mutex);
	}
	VERIFY(pthread_rwlock_unlock(&rwlp->rw_lock) == 0);
}

int
rw_tryenter(krwlock_t *rwlp, krw_t rw)
{
	int rv;

	ASSERT(rwlp->rw_owner != (void *)-1UL);
	ASSERT(rwlp->rw_owner != curthread);

	if (rw == RW_READER)
		rv = pthread_rwlock_tryrdlock(&rwlp->rw_lock);
	else
		rv = pthread_rwlock_trywrlock(&rwlp->rw_lock);

	if (rv == 0) {
		if(rw == RW_READER) {
			mutex_enter(&rwlp->mutex);
			ASSERT(rwlp->thr_count >= 0);
			rwlp->thr_count++;
			mutex_exit(&rwlp->mutex);
			ASSERT(rwlp->rw_owner == NULL);
		} else {
			ASSERT(rwlp->rw_owner == NULL);
			ASSERT(rwlp->thr_count == 0);
			rwlp->thr_count = -1;
			rwlp->rw_owner = curthread;
		}
		return (1);
	}
	VERIFY(rv == EBUSY);

	return (0);
}

/*ARGSUSED*/
int
rw_tryupgrade(krwlock_t *rwlp)
{
	ASSERT(rwlp->rw_owner != (void *)-1UL);

	return (0);
}

int rw_lock_held(krwlock_t *rwlp)
{
	int ret;
	mutex_enter(&rwlp->mutex);
	ret = rwlp->thr_count != 0;
	mutex_exit(&rwlp->mutex);
	return ret;
}
