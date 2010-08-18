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

#ifndef _VM_SEG_ENUM_H
#define _VM_SEG_ENUM_H

/*
 * seg_rw gives the access type for a fault operation
 */
#ifndef S_WRITE
enum seg_rw {
	S_OTHER,		/* unknown or not touched */
	S_READ,			/* read access attempted */
	S_WRITE,		/* write access attempted */
	S_EXEC,			/* execution access attempted */
	S_CREATE,		/* create if page doesn't exist */
	S_READ_NOCOW		/* read access, don't do a copy on write */
};
#define S_WRITE
#endif

#endif
