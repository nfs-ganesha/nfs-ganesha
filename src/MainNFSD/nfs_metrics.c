// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2024 Google LLC
 * Contributor : Yoni Couriel  yonic@google.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file nfs_metrics.c
 * @brief NFS metrics functions
 */

#include "nfs_metrics.h"
#include "common_utils.h"
#include "nfs_convert.h"

void nfs_metrics__nfs3_request(uint32_t proc,
				nsecs_elapsed_t request_time,
				nfsstat3 nfs_status,
				export_id_t export_id,
				const char *client_ip)
{
	const char *const version = "nfs3";
	const char *const operation = nfsproc3_to_str(proc);
	const char *const statusLabel = nfsstat3_to_str(nfs_status);

	monitoring__dynamic_observe_nfs_request(
		operation, request_time, version, statusLabel, export_id,
		client_ip);
}

void nfs_metrics__nfs4_request(uint32_t op,
				nsecs_elapsed_t request_time,
				nfsstat4 status,
				export_id_t export_id,
				const char *client_ip)
{
	const char *const version = "nfs4";
	const char *const operation = nfsop4_to_str(op);
	const char *const statusLabel = nfsstat4_to_str(status);

	monitoring__dynamic_observe_nfs_request(
		operation, request_time, version, statusLabel, export_id,
		client_ip);
}
