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
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <fcntl.h>

/*
 * Workaround stupid glibc bug, since it defines O_LARGEFILE to be 0
 * on 64-bit compilations... How are we supposed to know if O_LARGEFILE
 * is being used to open a file, since 32-bit apps also work on 64-bit
 * platforms? I mean, just because it's not necessary doesn't mean it
 * should define it to be 0!
 */

#if O_LARGEFILE == 0
#undef O_LARGEFILE
#define O_LARGEFILE 0100000
#endif
