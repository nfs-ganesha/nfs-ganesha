/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * file    mnt_Export.c
 * brief   MOUNTPROC_EXPORT for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_EXPORT in V1, V3.
 *
 * Exporting client hosts and networks OK.
 *
 */
#include "config.h"
#include <arpa/inet.h>
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "fsal.h"
#include "nfs_proto_functions.h"
#include "export_mgr.h"

struct proc_state {
	exports head;
	exports tail;
	int retval;
};

static bool proc_export(struct gsh_export *export, void *arg)
{
	struct proc_state *state = arg;
	struct exportnode *new_expnode;
	struct glist_head *glist_item;
	exportlist_client_entry_t *client;
	struct groupnode *group, *grp_tail = NULL;
	char *grp_name;
	bool free_grp_name;

	state->retval = 0;

	/* If client does not have any access to the export,
	 * don't add it to the list
	 */
	op_ctx->ctx_export = export;
	op_ctx->fsal_export = export->fsal_export;
	export_check_access();
	if (!(op_ctx->export_perms->options & EXPORT_OPTION_ACCESS_MASK)) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Client is not allowed to access Export_Id %d %s",
			     export->export_id, export_path(export));

		return true;
	}

	if (!(op_ctx->export_perms->options & EXPORT_OPTION_NFSV3)) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Not exported for NFSv3, Export_Id %d %s",
			     export->export_id, export_path(export));

		return true;
	}

	new_expnode = gsh_calloc(1, sizeof(struct exportnode));
	new_expnode->ex_dir = gsh_strdup(export_path(export));

	PTHREAD_RWLOCK_rdlock(&op_ctx->ctx_export->lock);

	glist_for_each(glist_item, &export->clients) {
		client =
		    glist_entry(glist_item, exportlist_client_entry_t,
				cle_list);
		group = gsh_calloc(1, sizeof(struct groupnode));

		if (grp_tail == NULL)
			new_expnode->ex_groups = group;
		else
			grp_tail->gr_next = group;

		grp_tail = group;
		free_grp_name = false;
		switch (client->type) {
		case NETWORK_CLIENT:
			grp_name = cidr_to_str(client->client.network.cidr,
						CIDR_NOFLAGS);
			if (grp_name == NULL) {
				state->retval = errno;
				grp_name = "Invalid Network Address";
			} else {
				free_grp_name = true;
			}
			break;
		case NETGROUP_CLIENT:
			grp_name = client->client.netgroup.netgroupname;
			break;
		case GSSPRINCIPAL_CLIENT:
			grp_name = client->client.gssprinc.princname;
			break;
		case MATCH_ANY_CLIENT:
			grp_name = "*";
			break;
		case WILDCARDHOST_CLIENT:
			grp_name = client->client.wildcard.wildcard;
			break;
		default:
			grp_name = "<unknown>";
		}
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Export %s client %s",
			     export_path(export), grp_name);
		group->gr_name = gsh_strdup(grp_name);
		if (free_grp_name)
			gsh_free(grp_name);
	}

	PTHREAD_RWLOCK_unlock(&op_ctx->ctx_export->lock);

	if (state->head == NULL)
		state->head = new_expnode;
	else
		state->tail->ex_next = new_expnode;

	state->tail = new_expnode;

	return true;
}

/**
 * @brief The Mount proc EXPORT function, for all versions.
 *
 * Return a list of all exports and their allowed clients/groups/networks.
 *
 * @param[in]  arg     Ignored
 * @param[in]  req     Ignored
 * @param[out] res     Pointer to the export list
 *
 */

int mnt_Export(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct proc_state proc_state;

	/* init everything of interest to good state. */
	memset(res, 0, sizeof(nfs_res_t));
	memset(&proc_state, 0, sizeof(proc_state));

	(void)foreach_gsh_export(proc_export, false, &proc_state);
	if (proc_state.retval != 0) {
		LogCrit(COMPONENT_NFSPROTO,
			"Processing exports failed. error = \"%s\" (%d)",
			strerror(proc_state.retval), proc_state.retval);
	}
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;
	res->res_mntexport = proc_state.head;
	return NFS_REQ_OK;
}				/* mnt_Export */

/**
 * mnt_Export_Free: Frees the result structure allocated for mnt_Export.
 *
 * Frees the result structure allocated for mnt_Dump.
 *
 * @param res	[INOUT]   Pointer to the result structure.
 *
 */
void mnt_Export_Free(nfs_res_t *res)
{
	struct exportnode *exp, *next_exp;
	struct groupnode *grp, *next_grp;

	exp = res->res_mntexport;
	while (exp != NULL) {
		next_exp = exp->ex_next;
		grp = exp->ex_groups;
		while (grp != NULL) {
			next_grp = grp->gr_next;
			if (grp->gr_name != NULL)
				gsh_free(grp->gr_name);
			gsh_free(grp);
			grp = next_grp;
		}
		gsh_free(exp);
		exp = next_exp;
	}
}				/* mnt_Export_Free */
