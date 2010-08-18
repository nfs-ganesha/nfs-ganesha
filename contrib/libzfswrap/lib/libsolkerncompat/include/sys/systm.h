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

#ifndef _SYS_SYSTM_H
#define _SYS_SYSTM_H

#include <sys/debug.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/dditypes.h>

#include <string.h>
#include <strings.h>
#include <errno.h>

extern uint64_t physmem;

#define	lbolt	(gethrtime() >> 23)
#define	lbolt64	(gethrtime() >> 23)
#define	hz	119	/* frequency when using gethrtime() >> 23 for lbolt */

extern struct vnode *rootdir;	/* pointer to vnode of root directory */

extern void delay(clock_t ticks);

static inline int fuword8(const void *from, uint8_t *to)
{
	*to = *((uint8_t *) from);
	return 0;
}

#define kcopy(from,to,count) memmove(to,from,count)
#define ovbcopy(from,to,count) memmove(to,from,count)

static inline int copystr(const char *from, char *to, size_t maxlength, size_t *lencopied)
{
	if(maxlength == 0)
		return ENAMETOOLONG;
	if(maxlength < 0)
		return EFAULT;

	size_t length = strlen(from);
	if(length >= maxlength) {
		memcpy(to, from, maxlength - 1);
		to[maxlength - 1] = '\0';
		if(lencopied != NULL)
			*lencopied = maxlength;
		return ENAMETOOLONG;
	}

	strcpy(to, from);
	if(lencopied != NULL)
		*lencopied = length + 1;
	return 0;
}


/*
 * These must be implemented in the program itself.
 * For zfs-fuse, take a look at zfs-fuse/zfsfuse_socket.c
 */
extern int xcopyin(const void *src, void *dest, size_t size);
extern int xcopyout(const void *src, void *dest, size_t size);
extern int copyinstr(const char *from, char *to, size_t max, size_t *len);

#define copyout(kaddr, uaddr, count) xcopyout(kaddr, uaddr, count)

#endif
