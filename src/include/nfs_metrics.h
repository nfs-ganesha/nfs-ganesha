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
 * @file nfs_metrics.h
 * @brief NFS metrics functions
 */

#ifndef NFS_METRICS_H
#define NFS_METRICS_H

#include "monitoring.h"
#include "nfsv41.h"
#include "nfs23.h"

void nfs_metrics__nfs4_op_completed(nfs_opnum4, nfsstat4, nsecs_elapsed_t);
void nfs_metrics__nfs4_compound_completed(
	nfsstat4, nsecs_elapsed_t, int num_ops);
void nfs_metrics__rpc_received(void);
void nfs_metrics__rpc_completed(void);
void nfs_metrics__rpcs_in_flight(int64_t value);
void nfs_metrics__init(void);

/*
 * The following two functions generate the following dynamic metrics,
 * exported both as total and per export.
 *
 * - Total request count.
 * - Total request count by success / failure status.
 * - Total bytes sent.
 * - Total bytes received.
 * - Request size in bytes as histogram.
 * - Response size in bytes as histogram.
 * - Latency in ms as histogram.
 */

void nfs_metrics__nfs3_request(const uint32_t proc,
			     const nsecs_elapsed_t request_time,
			     const nfsstat3 status,
			     const export_id_t export_id,
			     const char *client_ip);

void nfs_metrics__nfs4_request(const uint32_t op,
			     const nsecs_elapsed_t request_time,
			     const nfsstat4 status,
			     const export_id_t export_id,
			     const char *client_ip);

#endif /* !NFS_METRICS_H */
