/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Copyright (C) 2026, IBM
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

/**
 * @file cluster_members.h
 * @brief Runtime Cluster_Members list management for integrated NSM
 *
 * List mutations and readers must use nfs_core_lock.
 * Lock order: nfs_core_lock outside nsm_mutex.
 */

#ifndef CLUSTER_MEMBERS_H
#define CLUSTER_MEMBERS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Move local interface addresses from cluster_members to cluster_self.
 *
 * Caller must hold nfs_core_lock for write (except during early
 * single-threaded config commit before other users exist; still safe to lock).
 */
void remove_self_cluster_members(void);

/**
 * @brief Replace the Cluster_Members list atomically.
 *
 * Empty count clears the list and disables SM_NOTIFY fan-out.
 * Only individual IPv4/IPv6 addresses are accepted (no hostnames/CIDRs).
 * On failure the previous list is left unchanged.
 *
 * Uses nfs_core_lock for write.
 *
 * @param[in]  ips      Array of IP address strings (may be NULL if count==0)
 * @param[in]  count    Number of addresses
 * @param[out] errormsg Static or caller-owned error string on failure
 *
 * @return true on success
 */
bool set_cluster_members(char **ips, size_t count, char **errormsg);

/**
 * @brief Snapshot current peer Cluster_Members (not cluster_self).
 *
 * On success, *ips_out is an array of count_out gsh_strdup'd strings that
 * the caller must free with free_cluster_members_list().
 *
 * Uses nfs_core_lock for read.
 */
bool show_cluster_members(char ***ips_out, size_t *count_out, char **errormsg);

void free_cluster_members_list(char **ips, size_t count);

#ifdef USE_GRPC
bool grpc_set_cluster_members(char **ips, size_t count, bool *success,
			      char *errmsg, size_t errmsg_len);
bool grpc_show_cluster_members(char ***ips_out, size_t *count_out,
			       bool *success, char *errmsg, size_t errmsg_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CLUSTER_MEMBERS_H */
