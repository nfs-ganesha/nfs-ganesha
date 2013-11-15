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

#include "fsal_pnfs.h"
#include "panfs_pnfs_ioctl.h"

/**
 * @file   panfs_um_pnfs.h
 * @author Boaz Harrosh <bharrosh@panasas.com>
 *
 * @brief pNFS IOCTL wrapper library API
 *
 * This file is edited with the LINUX coding style: (Will be enforced)
 *	- Tab characters of 8 spaces wide
 *	- Lines not longer then 80 chars
 *	- etc ... (See linux Documentation/CodingStyle.txt)
 */

struct pan_ioctl_xdr;
void pan_ioctl_xdr_init(void *buff, int alloc_len, int cur_len);

nfsstat4 panfs_um_getdeviceinfo(int fd, struct pan_ioctl_xdr *da_addr_body,
				const layouttype4 type,
				const struct pnfs_deviceid *deviceid);

nfsstat4 panfs_um_layoutget(int fd, struct pan_ioctl_xdr *loc_body,
			    uint64_t clientid, void *recall_file_info,
			    const struct fsal_layoutget_arg *arg,
			    struct fsal_layoutget_res *res);

nfsstat4 panfs_um_layoutreturn(int fd, struct pan_ioctl_xdr *lrf_body,
			       const struct fsal_layoutreturn_arg *arg);

nfsstat4 panfs_um_layoutcommit(int fd, struct pan_ioctl_xdr *lou_body,
			       const struct fsal_layoutcommit_arg *arg,
			       struct fsal_layoutcommit_res *res);

int panfs_um_recieve_layoutrecall(int fd,
				  struct pan_cb_layoutrecall_event *events,
				  int max_events, int *num_events);

int panfs_um_cancel_recalls(int fd, int debug_magic);
