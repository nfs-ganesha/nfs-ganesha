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
 * Copyright (c) 2006 Ricardo Correia.
 */

#ifndef _SYS_THREAD_H
#define _SYS_THREAD_H

#include <pthread.h>

typedef pthread_t kthread_t;
typedef kthread_t *kthread_id_t;

extern kthread_t *zk_thread_create(void (*func)(), void *arg);

#define thread_create(stk, stksize, func, arg, len, pp, state, pri) zk_thread_create(func, arg)
#define thread_exit(r) pthread_exit(NULL)
#define thr_self() pthread_self()

#define curthread ((void *)(uintptr_t)thr_self())

#endif
