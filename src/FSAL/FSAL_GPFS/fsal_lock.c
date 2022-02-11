// SPDX-License-Identifier: LGPL-3.0-or-later
/** @file fsal_lock.s
 *
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

/* _FILE_OFFSET_BITS macro causes F_GETLK/SETLK/SETLKW to be defined to
 * F_GETLK64/SETLK64/SETLKW64. Currently GPFS kernel module doesn't work
 * with these 64 bit macro values through ganesha interface. Undefine it
 * here to use plain F_GETLK/SETLK/SETLKW values.
 */
#undef _FILE_OFFSET_BITS

#include "config.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

fsal_status_t
GPFSFSAL_lock_op(struct fsal_export *export, fsal_lock_op_t lock_op,
		 fsal_lock_param_t *req_lock, fsal_lock_param_t *confl_lock,
		 struct set_get_lock_arg *sg_lock_arg)
{
	struct glock *glock = sg_lock_arg->lock;
	int retval;
	int errsv;

	if (req_lock->lock_sle_type == FSAL_LEASE_LOCK)
		retval = gpfs_ganesha(OPENHANDLE_SET_DELEGATION, sg_lock_arg);
	else if (lock_op == FSAL_OP_LOCKT)
		retval = gpfs_ganesha(OPENHANDLE_GET_LOCK, sg_lock_arg);
	else
		retval = gpfs_ganesha(OPENHANDLE_SET_LOCK, sg_lock_arg);

	if (retval) {
		errsv = errno;
		goto err_out;
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if (confl_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT &&
		    glock->flock.l_type != F_UNLCK) {
			confl_lock->lock_length = glock->flock.l_len;
			confl_lock->lock_start = glock->flock.l_start;
			confl_lock->lock_type = glock->flock.l_type;
		} else {
			confl_lock->lock_length = 0;
			confl_lock->lock_start = 0;
			confl_lock->lock_type = FSAL_NO_LOCK;
		}
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err_out:
	/* error only section from here on */

	if ((confl_lock != NULL) &&
	    (lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_LOCKB)) {
		glock->cmd = F_GETLK;
		if (gpfs_ganesha(OPENHANDLE_GET_LOCK, sg_lock_arg)) {
			int errsv2 = errno;

			LogCrit(COMPONENT_FSAL,
				"After failing a set lock request, an attempt to get the current owner details also failed.");
			if (errsv2 == EUNATCH)
				LogFatal(COMPONENT_FSAL,
					 "GPFS Returned EUNATCH");
		} else {
			confl_lock->lock_length = glock->flock.l_len;
			confl_lock->lock_start = glock->flock.l_start;
			confl_lock->lock_type = glock->flock.l_type;
		}
	}

	if (retval == 1) {
		LogFullDebug(COMPONENT_FSAL, "GPFS queued blocked lock");
		return fsalstat(ERR_FSAL_BLOCKED, 0);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "GPFS lock operation failed error %d %d (%s)",
		     retval, errsv, strerror(errsv));

	if (errsv == EUNATCH)
		LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");

	if (errsv == EGRACE)
		return fsalstat(ERR_FSAL_IN_GRACE, 0);

	return fsalstat(posix2fsal_error(errsv), errsv);
}
