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

#ifndef _SYS_FLOCK_H
#define _SYS_FLOCK_H

#include <sys/callb.h>
#include <sys/file.h>

#define cleanlocks(v,p,i) ((void) 0)
#define chklock(a,b,c,d,e,f) (0)

int convoff(struct vnode *vp, struct flock64 *lckdat, int whence, offset_t offset);

/*
 * Optional callbacks for blocking lock requests.  Each function is called
 * twice.
 * The first call is after the request is put in the "sleeping" list, but
 *   before waiting.  At most one callback may return a callb_cpr_t object;
 *   the others must return NULL.  If a callb_cpr_t is returned, the thread
 *   will be marked as safe to suspend while waiting for the lock.
 * The second call is after the request wakes up.  Note that the request
 *   might not have been granted at the second call (e.g., the request was
 *   signalled).
 * New callbacks should be added to the head of the list.  For the first
 * call the list is walked in order.  For the second call the list is
 * walked backwards (in case the callbacks need to reacquire locks).
 */

typedef enum {FLK_BEFORE_SLEEP, FLK_AFTER_SLEEP} flk_cb_when_t;

struct flk_callback {
	struct flk_callback *cb_next;	/* circular linked list */
	struct flk_callback *cb_prev;
	callb_cpr_t	*(*cb_callback)(flk_cb_when_t, void *);	/* fcn ptr */
	void		*cb_data;	/* ptr to callback data */
};

typedef struct flk_callback flk_callback_t;

#endif
