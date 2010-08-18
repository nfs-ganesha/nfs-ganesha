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
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/thread.h>
#include <sys/types.h>

#include <errno.h>
#include <pthread.h>

void
zmutex_init(kmutex_t *mp)
{
	mp->m_owner = NULL;
#ifdef DEBUG
	pthread_mutexattr_t attr;
	VERIFY(pthread_mutexattr_init(&attr) == 0);
	VERIFY(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) == 0);
	VERIFY(pthread_mutex_init(&mp->m_lock, &attr) == 0);
	VERIFY(pthread_mutexattr_destroy(&attr) == 0);
#else
	VERIFY(pthread_mutex_init(&mp->m_lock, NULL) == 0);
#endif
}

void
zmutex_destroy(kmutex_t *mp)
{
	ASSERT(mp->m_owner == NULL);
	VERIFY(pthread_mutex_destroy(&mp->m_lock) == 0);
	mp->m_owner = (void *)-1UL;
}

void
mutex_enter(kmutex_t *mp)
{
	ASSERT(mp->m_owner != (void *)-1UL);
	ASSERT(mp->m_owner != curthread);
	VERIFY(pthread_mutex_lock(&mp->m_lock) == 0);
	ASSERT(mp->m_owner == NULL);
	mp->m_owner = curthread;
}

int
mutex_tryenter(kmutex_t *mp)
{
	ASSERT(mp->m_owner != (void *)-1UL);
	int ret = pthread_mutex_trylock(&mp->m_lock);

	if (ret == 0) {
		ASSERT(mp->m_owner == NULL);
		mp->m_owner = curthread;
		return (1);
	} else {
		VERIFY(ret == EBUSY);
		return (0);
	}
}

void
mutex_exit(kmutex_t *mp)
{
	ASSERT(mutex_owner(mp) == curthread);
	mp->m_owner = NULL;
	VERIFY(pthread_mutex_unlock(&mp->m_lock) == 0);
}

void *
mutex_owner(kmutex_t *mp)
{
	return (mp->m_owner);
}
