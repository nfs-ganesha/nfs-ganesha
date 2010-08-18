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

#ifndef _SYS_RWLOCK_H
#define	_SYS_RWLOCK_H

#include <sys/mutex.h>
#include <pthread.h>

typedef struct krwlock {
	kmutex_t   mutex;
	int        thr_count;
	void       *rw_owner;
	pthread_rwlock_t   rw_lock;
} krwlock_t;

typedef int krw_t;

#define	RW_READER	0
#define	RW_WRITER	1
#define	RW_DEFAULT	0

#undef RW_READ_HELD

#define RW_WRITE_HELD(x) ((x)->rw_owner == curthread)
#define RW_LOCK_HELD(x) rw_lock_held(x)

extern void rw_init(krwlock_t *rwlp, char *name, int type, void *arg);
extern void rw_destroy(krwlock_t *rwlp);
extern void rw_enter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryenter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryupgrade(krwlock_t *rwlp);
extern void rw_exit(krwlock_t *rwlp);
extern int rw_lock_held(krwlock_t *rwlp);
#define	rw_downgrade(rwlp) do { } while (0)

#endif	/* _SYS_RWLOCK_H */
