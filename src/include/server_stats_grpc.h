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

/*
 * gRPC-safe declarations for client statistics.
 *
 * This header is the C/C++ boundary for cltmgr I/O stats RPCs. C++ gRPC
 * handlers must include this file instead of server_stats_private.h,
 * which is not C++-safe (uses the C++ keyword "export" as a field name
 * and pulls in D-Bus types).
 *
 * Implementation lives in:
 *   - support/server_stats.c  (stats field extraction)
 *   - support/client_mgr.c    (client lookup and orchestration)
 */

#ifndef SERVER_STATS_GRPC_H
#define SERVER_STATS_GRPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Opaque forward declarations; full structs are private to server_stats.c */
struct gsh_stats;

/*
 * Portable I/O stats snapshot passed from C to C++ before protobuf fill.
 * Layout matches nfsProtoUtil.IoStats and D-Bus (tttttt) IOSTATS_REPLY.
 */
struct grpc_iostats {
	uint64_t requested;
	uint64_t transferred;
	uint64_t total_ops;
	uint64_t errors;
	uint64_t latency;
	uint64_t queue_wait;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Extract read/write stats for a client; returns false if no version
 * activity.
 */
bool server_grpc_fill_v3_iostats(struct gsh_stats *st,
				 struct grpc_iostats *read_out,
				 struct grpc_iostats *write_out);

bool server_grpc_fill_v40_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

bool server_grpc_fill_v41_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

bool server_grpc_fill_v42_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

/*
 * Per-version entry points called from nfsServiceServer.cc.
 *
 * Each function mirrors the corresponding D-Bus handler in client_mgr.c
 * (e.g. get_nfsv41_stats_io). On success, read_out/write_out and time_out
 * are filled and *success is set true with errmsg "OK". On failure,
 * *success is false and errmsg carries the D-Bus-equivalent message.
 * The return value is always true (errors are encoded in *success/errmsg,
 * matching D-Bus behaviour where the method call itself succeeds).
 */
bool grpc_cltmgr_get_v3_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v40_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v41_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v42_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_STATS_GRPC_H */
