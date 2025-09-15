/* SPDX-License-Identifier: LGPL-3.0-or-later */
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

#ifndef NFS_CLUSTER_QOS_H
#define NFS_CLUSTER_QOS_H

#ifdef ENABLE_CLUSTER_QOS

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>
#include <time.h>
#include "nfs_core.h"

#define CQOS_MIN_MSGTIME 50     /* In milli seconds */
#define CQOS_MAX_MSGTIME 500
#define CQOS_DEF_MSGTIME 200

extern struct config_block cqos_core;
extern struct glist_head cqos_hosts;
extern unsigned int cqos_initialized;

typedef struct cqos_ceph_nodes {
	struct glist_head node_list;
	sockaddr_t node_addr;
	int32_t fd;
	CLIENT *clnt;
} cqos_ceph_nodes_t;

typedef enum {
	CQOS_EXPORT_RBW = 1,
	CQOS_EXPORT_WBW,
	CQOS_EXPORT_CBW,
	CQOS_EXPORT_RIOPS,
	CQOS_EXPORT_WIOPS,
	CQOS_EXPORT_CIOPS,
	CQOS_CLIENT_RBW,
	CQOS_CLIENT_WBW,
	CQOS_CLIENT_CBW,
	CQOS_CLIENT_RIOPS,
	CQOS_CLIENT_WIOPS,
	CQOS_CLIENT_CIOPS
} cqos_ops_type_t;

typedef struct cqos_nodes_info {
	sockaddr_t node_addr;
	struct avltree_node cqos_avl_node;
	int32_t subscribed_ops;
} cqos_nodes_info_t;

int cqos_addr_cmpf(const struct avltree_node *lhs,
		   const struct avltree_node *rhs);

void cluster_qos_init(void);
void cluster_qos_process(cluster_qos_msg *cluster_qos_msg);
#endif
#endif
