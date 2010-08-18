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
 * Use is subject to license terms.
 */

#ifndef _SOL_KERN_SYS_PARAM_H
#define _SOL_KERN_SYS_PARAM_H

#include <sys/param_aux.h>

extern unsigned long _pagesize;
extern unsigned int _pageshift;

#define PAGESIZE   _pagesize
#define PAGESHIFT  _pageshift
#define PAGEOFFSET (PAGESIZE - 1)
#define PAGEMASK   (~PAGEOFFSET)

#define ptob(x)  ((uint64_t)(x) << PAGESHIFT)
#define btop(x)  (((x) >> PAGESHIFT))
#define btopr(x) ((((x) + PAGEOFFSET) >> PAGESHIFT))

#define MAXLINK 32767 /* max links */

#define MAXOFF32_T 0x7fffffff

/*
 * _POSIX_VDISABLE has historically been defined in <sys/param.h> since
 * an early merge with AT&T source.  It has also historically been defined
 * in <sys/termios.h>. The POSIX standard, IEEE Std. 1003.1-1988 initially
 * required the existence of _POSIX_VDISABLE in <sys/termios.h>.
 * Subsequent versions of the IEEE Standard as well as the X/Open
 * specifications required that _POSIX_VDISABLE be defined in <unistd.h>
 * while still allowing for it's existence in other headers.  With the
 * introduction of XPG6, _POSIX_VDISABLE can only be defined in <unistd.h>.
 */
#if !defined(_XPG6) || defined(__EXTENSIONS__)
#ifndef	_POSIX_VDISABLE
#define	_POSIX_VDISABLE 0	/* Disable special character functions */
#endif
#endif /* !defined(_XPG6) || defined(__EXTENSIONS__) */

#endif
