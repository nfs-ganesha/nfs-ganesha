/**
 * @file fsal_share.c
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
 *
 */

#include "config.h"
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 *  @param mntfd Mount file descriptor
 *  @param owner Owner
 *  @param req_share Request share
 *
 *  @return Status of operation
 */
fsal_status_t
GPFSFSAL_share_op(int mntfd, int fd, void *owner, fsal_share_param_t req_share)
{
	int rc = 0;
	struct share_reserve_arg share_arg;
	int errsv = 0;

	LogFullDebug(COMPONENT_FSAL,
		     "Share reservation: access:%u deny:%u owner:%p",
		     req_share.share_access, req_share.share_deny,
		     owner);

	share_arg.mountdirfd = mntfd;
	share_arg.openfd = fd;
	share_arg.share_access = req_share.share_access;
	share_arg.share_deny = req_share.share_deny;

	rc = gpfs_ganesha(OPENHANDLE_SHARE_RESERVE, &share_arg);
	errsv = errno;

	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "gpfs_ganesha: OPENHANDLE_SHARE_RESERVE returned error, rc=%d, errno=%d",
			 rc, errsv);
		if (errsv == EUNATCH)
			LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");
		return fsalstat(posix2fsal_error(errsv), errsv);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
