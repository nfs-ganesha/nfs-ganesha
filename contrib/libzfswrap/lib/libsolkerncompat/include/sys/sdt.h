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

#ifndef _SYS_SDT_H
#define _SYS_SDT_H

/*
 * Dtrace SDT probes have different signatures in userland than they do in
 * kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 */
 
#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#endif	/* DTRACE_PROBE */
#define	DTRACE_PROBE(a)	((void)0)

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#endif	/* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)	((void)0)

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#endif	/* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)	((void)0)

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#endif	/* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)	((void)0)

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#endif	/* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)

#endif
