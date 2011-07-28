/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 1991-2006 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_MUTEX_H
#define	_SYS_MUTEX_H

#include <pthread.h>

typedef struct kmutex {
	void	*m_owner;
	pthread_mutex_t	m_lock;
} kmutex_t;

#define	MUTEX_DEFAULT	USYNC_THREAD
#undef MUTEX_HELD
#define	MUTEX_HELD(m) ((m)->m_owner == curthread)

#define	mutex_init(mp, b, c, d)		zmutex_init((kmutex_t *)(mp))
#define	mutex_destroy(mp)		zmutex_destroy((kmutex_t *)(mp))

extern void zmutex_init(kmutex_t *mp);
extern void zmutex_destroy(kmutex_t *mp);
extern void mutex_enter(kmutex_t *mp);
extern void mutex_exit(kmutex_t *mp);
extern int mutex_tryenter(kmutex_t *mp);
extern void *mutex_owner(kmutex_t *mp);

#endif	/* _SYS_MUTEX_H */
