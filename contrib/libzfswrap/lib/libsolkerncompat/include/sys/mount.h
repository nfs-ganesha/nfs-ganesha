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

#ifndef _SOL_SYS_MOUNT_H
#define _SOL_SYS_MOUNT_H

/* LINUX */
#define S_WRITE S_WRITE_MOUNT
#include_next <sys/mount.h>
#undef S_WRITE

/*
 * Some old glibc headers don't define BLKGETSIZE64
 * and we don't want to require the kernel headers
 */
#if !defined(BLKGETSIZE64)
#define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

#define MS_DATA     0x0004 /* 6-argument mount */
#define MS_SYSSPACE 0x0008 /* Mounta already in kernel space */
#define MS_OVERLAY  0x0080 /* Allow overlay mounts */
#define MS_FORCE    0x0400 /* Forced unmount */

#endif
