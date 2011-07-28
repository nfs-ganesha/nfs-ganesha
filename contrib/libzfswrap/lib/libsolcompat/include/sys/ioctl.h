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
 * Copyright 2006 Ricardo Correia.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SOL_SYS_IOCTL_H
#define _SOL_SYS_IOCTL_H

#include <sys/types.h>

#include_next <sys/ioctl.h>

static inline int real_ioctl(int fd, int request, void *arg)
{
	return ioctl(fd, request, arg);
}

/*
 * Redefine ioctl() to a zfs-fuse specific function that works
 * with UNIX sockets
 */
int zfsfuse_ioctl(int fd, int32_t request, void *arg);

#define ioctl(a,b,c) zfsfuse_ioctl(a,b,c)

#endif
