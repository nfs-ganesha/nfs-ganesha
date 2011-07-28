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

#ifndef _SOL_KERN_SYS_FILE_H
#define _SOL_KERN_SYS_FILE_H

#include <sys/file_aux.h>
#include <sys/avl.h>
#include <sys/vnode.h>
#include <sys/types.h>

#define	FKIOCTL		0x80000000	/* ioctl addresses are from kernel */

typedef struct file {
	struct vnode *f_vnode;  /* pointer to vnode structure */
	offset_t      f_offset; /* read/write character pointer */

	int           f_client; /* client socket */
	int           f_oldfd;  /* requested fd */
	avl_node_t    f_node;   /* avl node link */
} file_t;

/* The next 2 functions are implemented in zfs-fuse/zfsfuse_socket.c */
extern file_t *getf(int);
extern void releasef(int);

#endif
