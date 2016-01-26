/*
 * Copyright © from 2012 Panasas Inc.
 * Author: Boaz Harrosh <bharrosh@panasas.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, email to the Free Software
 * Foundation, Inc., licensing@fsf.org
 *
 * -------------
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/ioctl.h>

#include "fsal_convert.h"

/* Implementing this */
#include "panfs_um_pnfs.h"
/* With this */
#include "panfs_pnfs_ioctl.h"
#include "attrs.h"

/**
 * @file   panfs_um_pnfs.c
 * @author Boaz Harrosh <bharrosh@panasas.com>
 *
 * @brief pNFS IOCTL wrapper library Implementation
 *
 * Given an open file descriptor, these functions below will setup the
 * appropriate IOCTL call into the panfs.ko filesystem.
 *
 * This file is edited with the LINUX coding style: (Will be enforced)
 *	- Tab characters of 8 spaces wide
 *	- Lines not longer then 80 chars
 *	- etc ... (See linux Documentation/CodingStyle.txt)
 */

nfsstat4 panfs_um_getdeviceinfo(int fd, struct pan_ioctl_xdr *da_addr_body,
				const layouttype4 type,
				const struct pnfs_deviceid *deviceid)
{
	struct pan_getdeviceinfo_ioctl pgi = {
		.hdr.size = sizeof(pgi),
		.da_addr_body = *da_addr_body,
		.type = type,
		.deviceid = *deviceid,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_DEVICEINFO, &pgi);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	*da_addr_body = pgi.da_addr_body;
	return pgi.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutget(int fd, struct pan_ioctl_xdr *loc_body,
			    uint64_t clientid, void *recall_file_info,
			    const struct fsal_layoutget_arg *arg,
			    struct fsal_layoutget_res *res)
{
	struct pan_layoutget_ioctl pli = {
		.hdr.size = sizeof(pli),
		.loc_body = *loc_body,
		.clientid = clientid,
		.recall_file_info = recall_file_info,
		.arg = arg,
		.res = res,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTGET, &pli);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	*loc_body = pli.loc_body;
	return pli.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutreturn(int fd, struct pan_ioctl_xdr *lrf_body,
			       const struct fsal_layoutreturn_arg *arg)
{
	struct pan_layoutreturn_ioctl plri = {
		.hdr.size = sizeof(plri),
		.lrf_body = *lrf_body,
		.arg = arg,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTRETURN, &plri);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	return plri.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutcommit(int fd, struct pan_ioctl_xdr *lou_body,
			       const struct fsal_layoutcommit_arg *arg,
			       struct fsal_layoutcommit_res *res)
{
	struct pan_layoutcommit_ioctl plci = {
		.hdr.size = sizeof(plci),
		.lou_body = *lou_body,
		.arg = arg,
		.res = res,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTCOMMIT, &plci);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	*lou_body = plci.lou_body;
	return plci.hdr.nfsstat;
}

int panfs_um_recieve_layoutrecall(int fd,
				  struct pan_cb_layoutrecall_event *events,
				  int max_events, int *num_events)
{
	struct pan_cb_layoutrecall_ioctl pcli = {
		.hdr.size = sizeof(pcli),
		.events = events,
		.max_events = max_events,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTRECALL, &pcli);
	if (ret)
		return errno;

	*num_events = pcli.num_events;
	return pcli.hdr.nfsstat;
}

int panfs_um_cancel_recalls(int fd, int debug_magic)
{
	struct pan_cancel_recalls_ioctl pcri = {
		.hdr.size = sizeof(pcri),
		.debug_magic = debug_magic,
	};

	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_CANCEL_RECALLS, &pcri);
	if (ret)
		return errno;

	return pcri.hdr.nfsstat;
}

fsal_status_t panfs_um_get_attr(int fd, struct pan_attrs *pan_attrs)
{
	fsal_errors_t error = ERR_FSAL_NO_ERROR;
	int ret;
	pan_fs_client_ioctl_get_attr_args_t args;
	struct pan_fs_client_ioctl_get_attr_args_holder_s get_attrs;
	int num_aces;

	memset(&args, 0, sizeof(args));
	get_attrs.get_attr_args = &args;

	args.flags = PAN_FS_CLIENT_IOCTL_GET_F__GET_CACHED |
		PAN_FS_CLIENT_IOCTL_GET_F__OPT_ATTRS |
		PAN_FS_CLIENT_IOCTL_GET_F__SORT_V1_ACL |
		PAN_FS_CLIENT_IOCTL_GET_F__CACHE_ACL;
	args.acl_version = PAN_FS_ACL_VERSION;

	ret = ioctl(fd, PAN_FS_CLIENT_IOC_ATTR_GET, &get_attrs);
	if (ret) {
		ret = errno;
		error = posix2fsal_error(ret);
		return fsalstat(error, ret);
	}

	if (args.acl_version < PAN_FS_ACL_VERSION_MIN  ||
	    args.acl_version > PAN_FS_ACL_VERSION_MAX) {
		ret = -EINVAL;
		error = posix2fsal_error(ret);
		return fsalstat(error, ret);
	}

	num_aces = MIN(args.num_aces, PAN_FS_ACL_LEN_MAX);
	memcpy(pan_attrs->acls.aces, args.panfs_acl,
			num_aces * sizeof(struct pan_fs_ace_s));
	pan_attrs->acls.naces = num_aces;

	return fsalstat(error, 0);
}

fsal_status_t panfs_um_set_attr(int fd, struct pan_attrs *pan_attrs)
{
	fsal_errors_t error = ERR_FSAL_NO_ERROR;
	int ret;
	pan_fs_client_ioctl_set_attr_args_t args;
	struct pan_fs_client_ioctl_set_attr_args_holder_s set_attrs;
	int num_aces;

	memset(&args, 0, sizeof(args));
	set_attrs.set_attr_args = &args;

	num_aces = MIN(pan_attrs->acls.naces, PAN_FS_ACL_LEN_MAX);
	memcpy(args.panfs_acl, pan_attrs->acls.aces,
			num_aces * sizeof(struct pan_fs_ace_s));
	args.num_aces = num_aces;
	args.attr_mask |= PAN_FS_CLIENT_IOCTL_SET_ATTR_ACL_REPLACE;
	args.acl_version = PAN_FS_ACL_VERSION;

	ret = ioctl(fd, PAN_FS_CLIENT_IOC_ATTR_SET, &set_attrs);
	if (ret) {
		ret = errno;
		error = posix2fsal_error(ret);
		return fsalstat(error, ret);
	}

	return fsalstat(error, 0);
}
