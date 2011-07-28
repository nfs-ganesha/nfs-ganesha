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
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <errno.h>

/*
 * convoff - converts the given data (start, whence) to the
 * given whence.
 */
int
convoff(vp, lckdat, whence, offset)
	struct vnode   *vp;
	struct flock64 *lckdat;
	int whence;
	offset_t offset;
{
	int error;
	struct vattr vattr;

	if ((lckdat->l_whence == 2) || (whence == 2)) {
		vattr.va_mask = AT_SIZE;
		if (error = VOP_GETATTR(vp, &vattr, 0, CRED(), NULL))
			return (error);
	}

	switch (lckdat->l_whence) {
	case 1:
		lckdat->l_start += offset;
		break;
	case 2:
		lckdat->l_start += vattr.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (EINVAL);
	}

	if (lckdat->l_start < 0)
		return (EINVAL);

	switch (whence) {
	case 1:
		lckdat->l_start -= offset;
		break;
	case 2:
		lckdat->l_start -= vattr.va_size;
		/* FALLTHRU */
	case 0:
		break;
	default:
		return (EINVAL);
	}

	lckdat->l_whence = (short)whence;
	return (0);
}
