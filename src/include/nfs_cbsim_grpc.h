/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Suhas Athani <Suhas.Athani@ibm.com>
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
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

/**
 * @file nfs_cbsim_grpc.h
 * @brief C helpers for CBSIM gRPC APIs
 *
 * Keeps nfs_rpc_callback.h (and related callback internals) out of
 * C++ translation units. gRPC server code calls these helpers instead.
 */

#ifndef NFS_CBSIM_GRPC_H
#define NFS_CBSIM_GRPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fake/force a CB_RECALL for the given client id
 *
 * gRPC equivalent of org.ganesha.nfsd.cbsim.fake_recall.
 *
 * @param[in]  client_id   NFSv4 client id
 * @param[out] success     true if the CB call was submitted
 * @param[out] errmsg      failure reason when success is false
 * @param[in]  errmsg_len  size of errmsg buffer
 */
void grpc_cbsim_fake_recall(uint64_t client_id, bool *success, char *errmsg,
			    size_t errmsg_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NFS_CBSIM_GRPC_H */
