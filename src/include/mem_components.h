/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Nishant Puri <Nishant.Puri1@ibm.com>
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file mem_components.h
 * @brief Memory component enum — kept in its own header so that files
 *        included early in the chain (e.g. ip_utils.h via log.h) can
 *        reference mem_components_t without pulling in all of abstract_mem.h.
 */

#ifndef MEM_COMPONENTS_H
#define MEM_COMPONENTS_H

#include "abstract_atomic.h"
#include <rpc/types.h>

/*
 * Memory Component List.
 * When adding or reordering MEM_COMP_* entries, also update:
 *   - MemComponents[] in src/support/server_stats.c (.mem_comp_str labels)
 *   - @NAMES[] in src/scripts/bpftrace/mem_comp_audit.bt (same index/name)
 *
 * mem_comp_audit.bt uses stock RHEL bpftrace and cannot read this enum at
 * runtime; keep the three locations in sync manually.
 */
typedef enum {
	MEM_COMP_UNSET = 0, /* Do not touch this position */
	MEM_COMP_ACL, /* ACL / ACE data */
	MEM_COMP_CLIENT, /* All clients in client_mgr (v3, v4, 9P) */
	MEM_COMP_CLIENTID, /* NFSv4 clientid, sessions, connection state */
	MEM_COMP_CONFIG, /* ganesha.conf, parser */
	MEM_COMP_DIRENT, /* MDCACHE directory / dirent cache */
	MEM_COMP_DUP_REQ, /* Duplicate request (dupreq + TCP DRC) */
	MEM_COMP_EXPORT, /* Export list, per-export stats, export-related SAL */
	MEM_COMP_FRIDGE, /* firdge thread pool, delayed-exec scheduling */
	MEM_COMP_FSAL, /* FSAL & filesystem semantics */
	MEM_COMP_GTEST, /* Allocation and free in GTEST only */
	MEM_COMP_HASHTABLE, /* Generic hashtable node/data storage */
	MEM_COMP_IDMAPPER, /* idmapper subsystem allocations */
	MEM_COMP_IO_BUFFER, /* READ / WRITE data buffers only */
	MEM_COMP_LIBNTIRPC, /* libntirpc */
	MEM_COMP_MANAGE, /* D-Bus handlers and gRPC management APIs */
	MEM_COMP_MDCACHE, /* MDCACHE entry/handle cache */
	MEM_COMP_PROTOCOL, /* NFS / NLM / 9P / NFS4 / NFS3, nfs_res_t */
	MEM_COMP_QOS, /* QoS rate-control */
	MEM_COMP_RECOVERY, /* NFS recovery state (clid_entry, rdel_fh) */
	MEM_COMP_STATE, /* State and lock owners, delegations */
	MEM_COMP_TRANSIENT, /* Short-lived or freed within same function */
	MEM_COMP_XCOPY, /* NFSv4 COPY / COPY offload / FSAL copy buffers */
	MEM_COMP_MAX
} mem_components_t;

struct mem_component_info {
	const char *mem_comp_name; /* component name */
	const char *mem_comp_str; /* shorter, more useful name */
};

#endif /* MEM_COMPONENTS_H */
