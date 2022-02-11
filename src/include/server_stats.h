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

void server_stats_nfs_done(nfs_request_t *reqdata, int rc, bool dup);

#ifdef _USE_9P
void server_stats_9p_done(u8 msgtype, struct _9p_request_data *req9p);
#endif

void server_stats_io_done(size_t requested,
			  size_t transferred, bool success, bool is_write);
void server_stats_compound_done(int num_ops, int status);
void server_stats_nfsv4_op_done(int proto_op,
				struct timespec *start_time, int status);
void server_stats_transport_done(struct gsh_client *client,
				uint64_t rx_bytes, uint64_t rx_pkt,
				uint64_t rx_err, uint64_t tx_bytes,
				uint64_t tx_pkt, uint64_t tx_err);

/* For delegations */
void inc_grants(struct gsh_client *client);
void dec_grants(struct gsh_client *client);
void inc_revokes(struct gsh_client *client);
void inc_recalls(struct gsh_client *client);
void inc_failed_recalls(struct gsh_client *client);

#endif				/* !SERVER_STATS_H */
/** @} */
