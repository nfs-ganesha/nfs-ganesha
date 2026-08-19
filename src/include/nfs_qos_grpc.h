/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright (C) 2025, IBM
 * Contributor : Suhas Athani <sathani@redhat.com>
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
 * -------------
 */

#ifndef NFS_QOS_GRPC_H
#define NFS_QOS_GRPC_H

#include "config.h"

#ifdef ENABLE_QOS

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "ip_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

struct grpc_qos_bw_limits {
	bool enabled;
	uint64_t read_bw;
	uint64_t write_bw;
};

struct grpc_qos_iops_limits {
	bool enabled;
	uint64_t read_iops;
	uint64_t write_iops;
};

struct grpc_qos_token_limits {
	uint64_t max_tokens;
	uint64_t token_renewal;
};

bool grpc_qos_get_export_bandwidth(uint16_t export_id,
				   struct grpc_qos_bw_limits *out,
				   bool *success, char *errmsg,
				   size_t errmsg_len);
bool grpc_qos_set_export_bandwidth(uint16_t export_id, uint64_t read_bw,
				   uint64_t write_bw, bool *success,
				   char *errmsg, size_t errmsg_len);

bool grpc_qos_get_export_tokens(uint16_t export_id,
				struct grpc_qos_token_limits *out,
				bool *success, char *errmsg, size_t errmsg_len);
bool grpc_qos_set_export_tokens(uint16_t export_id, uint64_t max_tokens,
				uint64_t token_renewal, bool *success,
				char *errmsg, size_t errmsg_len);

bool grpc_qos_get_export_iops(uint16_t export_id,
			      struct grpc_qos_iops_limits *out, bool *success,
			      char *errmsg, size_t errmsg_len);
bool grpc_qos_set_export_iops(uint16_t export_id, uint64_t read_iops,
			      uint64_t write_iops, bool *success, char *errmsg,
			      size_t errmsg_len);

bool grpc_qos_enable_export_bw_control(uint16_t export_id, bool *success,
				       char *errmsg, size_t errmsg_len);
bool grpc_qos_disable_export_bw_control(uint16_t export_id, bool *success,
					char *errmsg, size_t errmsg_len);

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_QOS */

#endif /* NFS_QOS_GRPC_H */
