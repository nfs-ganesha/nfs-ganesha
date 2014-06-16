/*
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
 *
 */
#include "config.h"

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

/**
 * GPFSFSAL_lock_op:
 * Lock/unlock/test an owner independent (anonymous) lock for region in a file.
 *
 * \param obj_hdl (input):
 *        File handle of the file to lock.
 * \param p_owner (input):
 *        Owner for the requested lock; Opaque to FSAL.
 * \param lock_op (input):
 *        Can be either FSAL_OP_LOCKT, FSAL_OP_LOCK, FSAL_OP_UNLOCK.
 *        The operations are test if a file region is locked, lock a
 *        file region, unlock a file region.
 * \param request_lock (input):
 *        Lock information, type, byte range....
 * \param conflicting_lock (output):
 *        Conflicting lock information, type, byte range....
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: One of the in put parameters is NULL.
 *      - ERR_FSAL_PERM: lock_op was FSAL_OP_LOCKT and the result was that the
 *                       operation would not be possible.
 */
fsal_status_t GPFSFSAL_lock_op(struct fsal_export *export,
			       struct fsal_obj_handle *obj_hdl,	/* IN */
			       void *p_owner,	/* IN */
			       fsal_lock_op_t lock_op,	/* IN */
			       fsal_lock_param_t request_lock,	/* IN */
			       fsal_lock_param_t *conflicting_lock)
{				/* OUT */
	int retval;
	struct glock glock_args;
	struct set_get_lock_arg gpfs_sg_arg;
	struct gpfs_fsal_obj_handle *myself;
	struct gpfs_filesystem *gpfs_fs;

	if (obj_hdl == NULL) {
		LogDebug(COMPONENT_FSAL, "obj_hdl arg is NULL.");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}
	if (p_owner == NULL) {
		LogDebug(COMPONENT_FSAL, "p_owner arg is NULL.");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	if (conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT) {
		LogDebug(COMPONENT_FSAL,
			 "Conflicting_lock argument can't be NULL with lock_op  = LOCKT");
		return fsalstat(ERR_FSAL_FAULT, 0);
	}

	myself = container_of(obj_hdl, struct gpfs_fsal_obj_handle, obj_handle);
	gpfs_fs = obj_hdl->fs->private;
	glock_args.lfd = myself->u.file.fd;

	LogFullDebug(COMPONENT_FSAL,
		     "Locking: op:%d sle_type:%d type:%d start:%llu length:%llu owner:%p",
		     lock_op, request_lock.lock_sle_type,
		     request_lock.lock_type,
		     (unsigned long long)request_lock.lock_start,
		     (unsigned long long)request_lock.lock_length, p_owner);

	if (lock_op == FSAL_OP_LOCKT)
		glock_args.cmd = F_GETLK;
	else if (lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK)
		glock_args.cmd = F_SETLK;
	else if (lock_op == FSAL_OP_LOCKB)
		glock_args.cmd = F_SETLKW;
	else if (lock_op == FSAL_OP_CANCEL)
		glock_args.cmd = GPFS_F_CANCELLK;
	else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: Lock operation requested was not TEST, GET, or SET.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (request_lock.lock_type == FSAL_LOCK_R)
		glock_args.flock.l_type = F_RDLCK;
	else if (request_lock.lock_type == FSAL_LOCK_W)
		glock_args.flock.l_type = F_WRLCK;
	else {
		LogDebug(COMPONENT_FSAL,
			 "ERROR: The requested lock type was not read or write.");
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	if (lock_op == FSAL_OP_UNLOCK)
		glock_args.flock.l_type = F_UNLCK;

	glock_args.flock.l_len = request_lock.lock_length;
	glock_args.flock.l_start = request_lock.lock_start;
	glock_args.flock.l_whence = SEEK_SET;

	glock_args.lfd = myself->u.file.fd;
	glock_args.lock_owner = p_owner;
	gpfs_sg_arg.mountdirfd = gpfs_fs->root_fd;
	gpfs_sg_arg.lock = &glock_args;
	gpfs_sg_arg.reclaim = request_lock.lock_reclaim;

	errno = 0;

	if (request_lock.lock_sle_type == FSAL_LEASE_LOCK)
		retval = gpfs_ganesha(OPENHANDLE_SET_DELEGATION, &gpfs_sg_arg);
	else
		retval =
		    gpfs_ganesha(lock_op ==
				 FSAL_OP_LOCKT ? OPENHANDLE_GET_LOCK :
				 OPENHANDLE_SET_LOCK, &gpfs_sg_arg);

	if (retval) {
		int errsv = errno;

		if ((conflicting_lock != NULL)
		    && (lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_LOCKB)) {
			int retval2;
			glock_args.cmd = F_GETLK;
			retval2 =
			    gpfs_ganesha(OPENHANDLE_GET_LOCK, &gpfs_sg_arg);
			int errsv2 = errno;
			if (retval2) {
				LogCrit(COMPONENT_FSAL,
					"After failing a set lock request, An attempt to get the current owner details also failed.");
				if (errsv2 == EUNATCH)
					LogFatal(COMPONENT_FSAL,
						"GPFS Returned EUNATCH");
			} else {
				conflicting_lock->lock_length =
				    glock_args.flock.l_len;
				conflicting_lock->lock_start =
				    glock_args.flock.l_start;
				conflicting_lock->lock_type =
				    glock_args.flock.l_type;
			}
		}
		if (retval == 1) {
			LogFullDebug(COMPONENT_FSAL,
				     "GPFS queued blocked lock");
			return fsalstat(ERR_FSAL_BLOCKED, 0);
		} else {
			LogFullDebug(COMPONENT_FSAL,
				     "GPFS lock operation failed error %d %d (%s)",
				     retval, errsv, strerror(errsv));
			if (errsv == EUNATCH)
				LogFatal(COMPONENT_FSAL,
					"GPFS Returned EUNATCH");
			if (errsv == EGRACE)
				return fsalstat(ERR_FSAL_IN_GRACE, 0);
			return fsalstat(posix2fsal_error(errsv), errsv);
		}
	}

	/* F_UNLCK is returned then the tested operation would be possible. */
	if (conflicting_lock != NULL) {
		if (lock_op == FSAL_OP_LOCKT
		    && glock_args.flock.l_type != F_UNLCK) {
			conflicting_lock->lock_length = glock_args.flock.l_len;
			conflicting_lock->lock_start = glock_args.flock.l_start;
			conflicting_lock->lock_type = glock_args.flock.l_type;
		} else {
			conflicting_lock->lock_length = 0;
			conflicting_lock->lock_start = 0;
			conflicting_lock->lock_type = FSAL_NO_LOCK;
		}
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
