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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SOL_PROC_H
#define _SOL_PROC_H

#include <sys/rctl.h>
#include <pthread.h>

#define issig(why) (FALSE)

#if 0
extern void tsd_create(uint_t *, void (*)(void *));
extern void tsd_destroy(uint_t *);
extern void *tsd_get(uint_t);
extern int tsd_set(uint_t, void *);
#endif

#define tsd_create(kp,df) VERIFY(pthread_key_create((pthread_key_t *) (kp), (df)) == 0)
#define tsd_destroy(kp)   VERIFY(pthread_key_delete((pthread_key_t) *(kp)) == 0)
#define tsd_get(k)        pthread_getspecific((pthread_key_t) (k))
#define tsd_set(k,dp)     pthread_setspecific((pthread_key_t) (k), (dp))

#endif

