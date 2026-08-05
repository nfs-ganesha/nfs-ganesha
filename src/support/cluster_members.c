// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file cluster_members.c
 * @brief Runtime update/show helpers for NFS_CORE_PARAM Cluster_Members
 *
 * Protects the list with nfs_core_lock.
 * Lock order: nfs_core_lock outside nsm_mutex.
 */

#include "config.h"
#include <string.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#include "cluster_members.h"
#include "client_mgr.h"
#include "common_utils.h"
#include "config_parsing.h"
#include "gsh_config.h"
#include "log.h"
#include "nfs_core.h"

#ifdef _INTERNAL_STATD
#include "nsm.h"
#endif

void remove_self_cluster_members(void)
{
	struct ifaddrs *ifap, *ifa;

	if (glist_empty(&NFS_pcp.cluster_members))
		return;

	if (getifaddrs(&ifap) != 0) {
		int err = errno;

		LogFatal(COMPONENT_CONFIG, "getifaddrs failed %s",
			 strerror(err));
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		sockaddr_t *sa = (sockaddr_t *)ifa->ifa_addr;
		struct base_client_entry *bce;

		if (sa == NULL)
			continue;

		if (sa->ss_family != AF_INET && sa->ss_family != AF_INET6)
			continue;

		bce = client_match(COMPONENT_CONFIG,
				   " for Cluster_Members list", sa,
				   &NFS_pcp.cluster_members, NULL);

		if (bce != NULL) {
			char ip[SOCK_NAME_MAX];
			struct display_buffer dspbuf = { sizeof(ip), ip, ip };

			display_sockip(&dspbuf, sa);

			LogInfo(COMPONENT_CONFIG,
				"Moving %s from Cluster_Members list to Cluster Self list",
				ip);

			glist_del(&bce->cle_list);
			glist_add_tail(&NFS_pcp.cluster_self, &bce->cle_list);
		}
	}

	freeifaddrs(ifap);
}

/**
 * @brief Classify token as a single host IPv4/IPv6 address for Cluster_Members
 */
static bool cluster_member_term_type(const char *token, enum term_type *hint,
				     char **errormsg)
{
	CIDR *cidr;

	if (strchr(token, '/') != NULL) {
		*errormsg = "Cluster_Members does not allow CIDRs";
		return false;
	}

	cidr = cidr_from_str(COMPONENT_CONFIG, token, MEM_COMP_CONFIG);
	if (cidr == NULL) {
		*errormsg = "Cluster_Members entry is not a valid IP";
		return false;
	}

	switch (cidr->ip_addr.ss_family) {
	case AF_INET:
		*hint = TERM_V4ADDR;
		break;
	case AF_INET6:
		*hint = TERM_V6ADDR;
		break;
	default:
		cidr_free(cidr, MEM_COMP_CONFIG);
		*errormsg = "Unsupported address family for Cluster_Members";
		return false;
	}

	cidr_free(cidr, MEM_COMP_CONFIG);
	return true;
}

bool set_cluster_members(char **ips, size_t count, char **errormsg)
{
	struct glist_head temp_list;
	struct config_error_type err_type;
	static char errbuf[LOG_BUFF_LEN];
	size_t i;
	int rc;

	if (errormsg == NULL)
		return false;

	*errormsg = "OK";

	if (count > 0 && ips == NULL) {
		*errormsg = "Invalid Cluster_Members argument";
		return false;
	}

	glist_init(&temp_list);
	init_error_type_static(&err_type, errbuf, sizeof(errbuf));
	errbuf[0] = '\0';

	/* Build replacement list first; leave live lists untouched on error. */
	for (i = 0; i < count; i++) {
		enum term_type hint;

		if (ips[i] == NULL || ips[i][0] == '\0') {
			*errormsg = "Empty Cluster_Members address";
			FreeClientList(&temp_list, FreeCoreClient);
			return false;
		}

		if (!cluster_member_term_type(ips[i], &hint, errormsg)) {
			FreeClientList(&temp_list, FreeCoreClient);
			return false;
		}

		rc = add_client(COMPONENT_CONFIG, &temp_list, ips[i], hint,
				MEM_COMP_CONFIG, NULL, &err_type, NULL, NULL,
				NULL);
		if (rc > 0) {
			if (errbuf[0] != '\0')
				*errormsg = errbuf;
			else
				*errormsg =
					"Failed to add Cluster_Members address";
			FreeClientList(&temp_list, FreeCoreClient);
			return false;
		}
	}

	PTHREAD_RWLOCK_wrlock(&nfs_core_lock);

	/* Verify Cluster_Memners doesn't change from empty to non-empty or
	 * reverse. It is valid for the list to ONLY be a single address that is
	 * the node's address as a way of reducing from clustered to
	 * non-cluustered, but the list can't be entirely removed.
	 */
	if (glist_empty(&temp_list) &&
	    (!glist_empty(&NFS_pcp.cluster_members) ||
	     !glist_empty(&NFS_pcp.cluster_self))) {
		*errormsg =
			"Change of Cluster_Members from non-empty to empty is invalid";
		PTHREAD_RWLOCK_unlock(&nfs_core_lock);
		FreeClientList(&temp_list, FreeCoreClient);
		return false;
	}

	if (!glist_empty(&temp_list) && glist_empty(&NFS_pcp.cluster_members) &&
	    glist_empty(&NFS_pcp.cluster_self)) {
		*errormsg =
			"Change of Cluster_Members from empty to non-empty is invalid";
		PTHREAD_RWLOCK_unlock(&nfs_core_lock);
		FreeClientList(&temp_list, FreeCoreClient);
		return false;
	}

	FreeClientList(&NFS_pcp.cluster_members, FreeCoreClient);
	FreeClientList(&NFS_pcp.cluster_self, FreeCoreClient);

	glist_splice_tail(&NFS_pcp.cluster_members, &temp_list);

	remove_self_cluster_members();

#ifdef ENABLE_CLUSTER_QOS
	/*
	 * Cluster QoS global structures needs to be updated with
	 * cluster_members details
	 */
	/** @todo FSF:
	 * populate_cqos_hosts();
	 */
#endif

	Log_ClientList_Level(COMPONENT_CONFIG, NIV_INFO, "Cluster_Members",
			     &NFS_pcp.cluster_members);

	PTHREAD_RWLOCK_unlock(&nfs_core_lock);

	*errormsg = "";
	return true;
}

bool show_cluster_members(char ***ips_out, size_t *count_out, char **errormsg)
{
	struct glist_head *glist;
	size_t count = 0;
	size_t idx = 0;
	char **ips;

	if (ips_out == NULL || count_out == NULL || errormsg == NULL)
		return false;

	*ips_out = NULL;
	*count_out = 0;
	*errormsg = "";

	PTHREAD_RWLOCK_rdlock(&nfs_core_lock);

	glist_for_each(glist, &NFS_pcp.cluster_members)
		count++;

	glist_for_each(glist, &NFS_pcp.cluster_self)
		count++;

	if (count == 0) {
		PTHREAD_RWLOCK_unlock(&nfs_core_lock);
		return true;
	}

	ips = gsh_calloc(count, sizeof(*ips), MEM_COMP_MANAGE);

	glist_for_each(glist, &NFS_pcp.cluster_members) {
		struct base_client_entry *bce;
		const char *str;

		bce = glist_entry(glist, struct base_client_entry, cle_list);
		str = bce->str != NULL ? bce->str : "";
		ips[idx++] = gsh_strdup(str, MEM_COMP_MANAGE);
	}

	glist_for_each(glist, &NFS_pcp.cluster_self) {
		struct base_client_entry *bce;
		const char *str;

		bce = glist_entry(glist, struct base_client_entry, cle_list);
		str = bce->str != NULL ? bce->str : "";
		ips[idx++] = gsh_strdup(str, MEM_COMP_MANAGE);
	}

	PTHREAD_RWLOCK_unlock(&nfs_core_lock);

	*ips_out = ips;
	*count_out = count;
	return true;
}

void free_cluster_members_list(char **ips, size_t count)
{
	size_t i;

	if (ips == NULL)
		return;

	for (i = 0; i < count; i++)
		gsh_free(ips[i], MEM_COMP_MANAGE);

	gsh_free(ips, MEM_COMP_MANAGE);
}

#ifdef USE_GRPC
bool grpc_set_cluster_members(char **ips, size_t count, bool *success,
			      char *errmsg, size_t errmsg_len)
{
	char *errormsg = NULL;
	bool ok;

#ifdef _INTERNAL_STATD
	ok = set_cluster_members(ips, count, &errormsg);
#else
	ok = false;
	errormsg = "Cluster_Members runtime update requires INTERNAL_STATD";
#endif
	if (success)
		*success = ok;
	if (errmsg && errmsg_len > 0) {
		if (errormsg)
			strlcpy(errmsg, errormsg, errmsg_len);
		else
			errmsg[0] = '\0';
	}
	return ok;
}

bool grpc_show_cluster_members(char ***ips_out, size_t *count_out,
			       bool *success, char *errmsg, size_t errmsg_len)
{
	char *errormsg = NULL;
	bool ok;

#ifdef _INTERNAL_STATD
	ok = show_cluster_members(ips_out, count_out, &errormsg);
#else
	ok = false;
	errormsg = "Cluster_Members runtime show requires INTERNAL_STATD";
	if (ips_out)
		*ips_out = NULL;
	if (count_out)
		*count_out = 0;
#endif
	if (success)
		*success = ok;
	if (errmsg && errmsg_len > 0) {
		if (errormsg)
			strlcpy(errmsg, errormsg, errmsg_len);
		else
			errmsg[0] = '\0';
	}
	return ok;
}
#endif /* USE_GRPC */
