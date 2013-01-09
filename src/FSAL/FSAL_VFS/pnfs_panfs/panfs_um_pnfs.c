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
#include <stdio.h>
#include <sys/ioctl.h>

/* Implementing this */
#include "panfs_um_pnfs.h"
/* With this */
#include "panfs_pnfs_ioctl.h"

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

nfsstat4 panfs_um_getdeviceinfo(
		int fd,
		struct pan_ioctl_xdr *da_addr_body,
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

//	_XDR_2_ioctlxdr_out(da_addr_body, &pgi.da_addr_body);
	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_DEVICEINFO, &pgi);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	return pgi.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutget(
		int fd,
		struct pan_ioctl_xdr *loc_body,
		uint64_t clientid,
		void    *recall_file_info,
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

struct pan_ioctl_xdr *ioctl_xdr = &pli.loc_body;
printf("%s: alloc_len=%d, buff=%p len=%d\n", __func__,
	ioctl_xdr->xdr_alloc_len, ioctl_xdr->xdr_buff, ioctl_xdr->xdr_len);

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTGET, &pli);
	if (ret) {
		printf("%s: PAN_FS_CLIENT_PNFS_LAYOUTGET => %d\n",
			__func__, errno);
		return NFS4ERR_SERVERFAULT;
	}

	return pli.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutreturn(
		int fd,
		struct pan_ioctl_xdr *lrf_body,
		const struct fsal_layoutreturn_arg *arg)
{
	struct pan_layoutreturn_ioctl  plri = {
		.hdr.size = sizeof(plri),
		.lrf_body = *lrf_body ,
		.arg = arg,
	};
	int ret;

// 	_XDR_2_ioctlxdr_in(lrf_body, &plri.lrf_body);
	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTRETURN, &plri);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	return plri.hdr.nfsstat;
}

nfsstat4 panfs_um_layoutcommit(
		int fd,
		struct pan_ioctl_xdr *lou_body,
		const struct fsal_layoutcommit_arg *arg,
		struct fsal_layoutcommit_res *res)
{
	struct pan_layoutcommit_ioctl plci = {
		.hdr.size = sizeof(plci) ,
		.lou_body = *lou_body,
		.arg = arg,
		.res = res,
	};
	int ret;

// 	_XDR_2_ioctlxdr_in(lou_body, &plci.lou_body);
	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTCOMMIT, &plci);
	if (ret)
		return NFS4ERR_SERVERFAULT;

	return plci.hdr.nfsstat;
}

int panfs_um_recieve_layoutrecall(
		int fd,
		struct pan_cb_layoutrecall_event *events,
		int max_events,
		int *num_events)
{
	struct pan_cb_layoutrecall_ioctl pcli = {
		.hdr.size = sizeof(pcli) ,
		.events = events,
		.max_events = max_events,
	};
	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_LAYOUTRECALL, &pcli);
	*num_events = pcli.num_events;
	if (ret)
		return ret;

	return pcli.hdr.nfsstat;
}

int panfs_um_cancel_recalls(
		int fd,
		int debug_magic)
{
	struct pan_cancel_recalls_ioctl pcri = {
		.hdr.size = sizeof(pcri) ,
		.debug_magic = debug_magic,
	};

	int ret;

	ret = ioctl(fd, PAN_FS_CLIENT_PNFS_CANCEL_RECALLS, &pcri);
	if (ret)
		return ret;

	return pcri.hdr.nfsstat;
}
