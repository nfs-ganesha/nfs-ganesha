// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Naresh Chillarege<Naresh.Chillarege@ibm.com>
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

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include "log.h"
#include "gsh_rpc.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "cqos.h"
#include "nfs_cluster_qos.h"

/**
 * @brief CQOS notification
 *
 * @param[in]  args
 * @param[in]  req
 * @param[out] res
 */

int cqos_rpc_msg_recv(nfs_arg_t *args, struct svc_req *req, nfs_res_t *res)
{
	cluster_qos_msg *arg = &args->arg_cqos_msg;
	sockaddr_t server_addr_ipv4;
	sockaddr_t *server_addr_conv;

	/* If the client socket is IPv4, then it is wrapped into a
	 * ::ffff:a.b.c.d IPv6 address. We check this here.
	 * This kind of address is shaped like this:
	 * |---------------------------------------------------------------|
	 * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
	 * |---------------------------------------------------------------|
	 * |            0          |        FFFF       |    IPv4 address   |
	 * |---------------------------------------------------------------|
	 */
	server_addr_conv = convert_ipv6_to_ipv4(COMPONENT_QOS,
						op_ctx->caller_addr,
						&server_addr_ipv4);

	if (server_addr_conv == &server_addr_ipv4)
		memcpy(&arg->node_addr, server_addr_conv, sizeof(sockaddr_t));
	else
		memcpy(&arg->node_addr, op_ctx->caller_addr,
		       sizeof(sockaddr_t));

	cluster_qos_process(arg);

	return NFS_REQ_OK;
}

/**
 * cqos_rpc_msg_Free: Frees the result structure allocated for cqos_rpc_msg_recv
 *
 * Frees the result structure allocated for cqos_rpc_msg_recv. Does Nothing in
 * fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void cqos_rpc_msg_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
