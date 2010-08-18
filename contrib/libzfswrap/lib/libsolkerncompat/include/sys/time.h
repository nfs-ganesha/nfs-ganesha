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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Copyright 2006 Ricardo Correia.
 * Use is subject to license terms.
 */

#ifndef _SOL_KERN_SYS_TIME_H
#define _SOL_KERN_SYS_TIME_H

#include <sys/time_aux.h>
#include <sys/debug.h>

#define gethrestime_sec() time(NULL)
#define gethrestime(t)    VERIFY(clock_gettime(CLOCK_REALTIME, t) == 0)

#define TIMESTRUC_TO_TIME(ts,ti) *(ti) = (ts).tv_sec
#define TIME_TO_TIMESTRUC(ti,ts) do { (ts)->tv_sec = (ti); (ts)->tv_nsec = 0; } while(0)

#endif
