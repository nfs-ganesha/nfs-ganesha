/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup Server statistics management
 * @{
 */

/**
 * @file server_stats.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Server statistics
 */

#ifndef SERVER_STATS_H
#define SERVER_STATS_H

#include <sys/types.h>

extern const char *mem_stat_names[];

/*
 * @brief Ganesha per-component memory statistics
 *
 * Always counted; there is no runtime enable/disable for capture.
 */
struct gsh_mem_stats {
	/* Lifetime: monotonic for the process; never cleared by reset. */
	uint64_t lifetime_alloc_calls;
	uint64_t lifetime_free_calls;
	uint64_t lifetime_alloc_bytes;
	uint64_t lifetime_freed_bytes;

	/*
	 * current_active_bytes is signed: an alloc/free mem_comp mismatch
	 * (freeing more than was ever allocated under this comp) is
	 * allowed to go negative instead of clamping to 0, so it stays
	 * visible in the value itself, not just in the LogWarn that fires
	 * alongside it. In a bug-free run this equals
	 * (lifetime_alloc_bytes - lifetime_freed_bytes).
	 */
	int64_t current_active_bytes;
	/* Lifetime high-water of current_active_bytes. */
	int64_t peak_active_bytes;
};

struct mem_stats_info {
	const char *mem_stat_name; /* stat name */
};

/*
 * If adding any new value to gsh_mem_stats, increment
 * MAX_MEMORY_STATS_FIELD_COUNT.
 */
#define MAX_MEMORY_STATS_FIELD_COUNT 6

void server_stats_nfs_done(nfs_request_t *reqdata, int rc, bool dup);

#ifdef _USE_9P
void server_stats_9p_done(u8 msgtype, struct _9p_request_data *req9p);
#endif

void server_stats_io_done(size_t requested, size_t transferred, bool success,
			  bool is_write);
void server_stats_compound_done(int num_ops, int status);
void server_stats_nfsv4_op_done(int proto_op, struct timespec *start_time,
				int status);
void server_stats_transport_done(struct gsh_client *client, uint64_t rx_bytes,
				 uint64_t rx_pkt, uint64_t rx_err,
				 uint64_t tx_bytes, uint64_t tx_pkt,
				 uint64_t tx_err);

/* For delegations */
void inc_grants(struct gsh_client *client);
void dec_grants(struct gsh_client *client);
void inc_revokes(struct gsh_client *client);
void inc_recalls(struct gsh_client *client);
void inc_failed_recalls(struct gsh_client *client);

void gsh_mem_stats_update_alloc(void *p, mem_components_t comp,
				const char *file, int line,
				const char *function);
void gsh_mem_stats_update_free(void *p, mem_components_t comp,
			       const char *file, int line,
			       const char *function);
int64_t gsh_mem_stats_get_stat_by_index_and_comp(uint32_t index,
						 mem_components_t comp);
/** comp is gshMC[] index: 0 = libntirpc, 1+ = Ganesha mem_components_t. */
const char *gsh_mem_stats_get_mem_comp_str(mem_components_t comp);
void gsh_log_mem_stats(void);

#endif /* !SERVER_STATS_H */
/** @} */
