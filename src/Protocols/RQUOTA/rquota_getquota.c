// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright CEA/DAM/DIF  2010
 *  Author: Philippe Deniel (philippe.deniel@cea.fr)
 *
 * --------------------------
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "mount.h"
#include <os/quota.h>		/* For USRQUOTA */
#include "rquota.h"
#include "nfs_proto_functions.h"
#include "export_mgr.h"
#include "nfs_creds.h"

/**
 * @brief The Rquota getquota function, for all versions.
 *
 * @param[in]  arg    Ignored
 * @param[in]  req    Ignored
 * @param[out] res    Ignored
 *
 */
int rquota_getquota(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	fsal_status_t fsal_status;
	fsal_quota_t fsal_quota;
	int quota_type = USRQUOTA;
	struct gsh_export *exp = NULL;
	char *quota_path;
	getquota_rslt *qres = &res->res_rquota_getquota;
	char path[MAXPATHLEN];
	int quota_id;

	LogFullDebug(COMPONENT_NFSPROTO,
		     "REQUEST PROCESSING: Calling RQUOTA_GETQUOTA");

	if (req->rq_msg.cb_vers == EXT_RQUOTAVERS) {
		quota_type = arg->arg_ext_rquota_getquota.gqa_type;
		quota_id = arg->arg_ext_rquota_getquota.gqa_id;
	} else {
		quota_id = arg->arg_rquota_getquota.gqa_uid;
	}
	qres->status = Q_EPERM;

	quota_path = check_handle_lead_slash(arg->arg_rquota_getquota.gqa_pathp,
					     path, MAXPATHLEN);
	if (quota_path == NULL)
		return NFS_REQ_OK;

	/*  Find the export for the dirname (using as well Path, Pseudo, or Tag)
	 */
	if (quota_path[0] != '/') {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by tag for %s",
			     quota_path);
		exp = get_gsh_export_by_tag(quota_path);
	} else if (nfs_param.core_param.mount_path_pseudo) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by pseudo for %s",
			     quota_path);
		exp = get_gsh_export_by_pseudo(quota_path, false);
	} else {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by path for %s",
			     quota_path);
		exp = get_gsh_export_by_path(quota_path, false);
	}

	if (exp == NULL) {
		/* No export found, return ACCESS error. */
		LogEvent(COMPONENT_NFSPROTO,
			 "Export entry for %s not found", quota_path);

		/* entry not found. */
		return NFS_REQ_OK;
	}

	/* Add export to op_ctx, will be released in free_args */
	set_op_context_export(exp);

	/* Get creds */
	if (nfs_req_creds(req) == NFS4ERR_ACCESS) {
		const char *client_ip = "<unknown client>";

		client_ip = op_ctx->client->hostaddr_str;
		LogInfo(COMPONENT_NFSPROTO,
			"could not get uid and gid, rejecting client %s",
			client_ip);
		return NFS_REQ_OK;
	}

	fsal_status = exp->fsal_export->exp_ops.get_quota(
				exp->fsal_export, CTX_FULLPATH(op_ctx),
				quota_type, quota_id, &fsal_quota);

	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_NO_QUOTA)
			qres->status = Q_NOQUOTA;
		return NFS_REQ_OK;
	}

	/* success */

	qres->getquota_rslt_u.gqr_rquota.rq_active = TRUE;
	qres->getquota_rslt_u.gqr_rquota.rq_bsize = fsal_quota.bsize;
	qres->getquota_rslt_u.gqr_rquota.rq_bhardlimit = fsal_quota.bhardlimit;
	qres->getquota_rslt_u.gqr_rquota.rq_bsoftlimit = fsal_quota.bsoftlimit;
	qres->getquota_rslt_u.gqr_rquota.rq_curblocks = fsal_quota.curblocks;
	qres->getquota_rslt_u.gqr_rquota.rq_curfiles = fsal_quota.curfiles;
	qres->getquota_rslt_u.gqr_rquota.rq_fhardlimit = fsal_quota.fhardlimit;
	qres->getquota_rslt_u.gqr_rquota.rq_fsoftlimit = fsal_quota.fsoftlimit;
	qres->getquota_rslt_u.gqr_rquota.rq_btimeleft = fsal_quota.btimeleft;
	qres->getquota_rslt_u.gqr_rquota.rq_ftimeleft = fsal_quota.ftimeleft;
	qres->status = Q_OK;

	return NFS_REQ_OK;
}				/* rquota_getquota */

/**
 * @brief Free the result structure allocated for rquota_getquota
 *
 *
 * @param[in,out] res Pointer to the result structure.
 */
void rquota_getquota_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
