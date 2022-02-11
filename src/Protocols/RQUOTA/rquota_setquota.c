// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright CEA/DAM/DIF  2010
 *  Author: Philippe Deniel (philippe.deniel@cea.fr)
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
#include <sys/file.h>
#include <os/quota.h>
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "mount.h"
#include "rquota.h"
#include "nfs_proto_functions.h"
#include "export_mgr.h"
#include "nfs_creds.h"

static int do_rquota_setquota(char *quota_path, int quota_type,
			      int quota_id,
			      sq_dqblk * quota_dqblk,
			      struct svc_req *req,
			      setquota_rslt * qres);

/**
 * @brief The Rquota setquota function, for all versions.
 *
 * The RQUOTA setquota function, for all versions.
 *
 * @param[in]  arg     quota args
 * @param[in]  req     contains quota version
 * @param[out] res     returned quota (modified)
 *
 */
int rquota_setquota(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	char *quota_path;
	int quota_id;
	int quota_type = USRQUOTA;
	struct sq_dqblk *quota_dqblk;
	setquota_rslt *qres = &res->res_rquota_setquota;

	LogFullDebug(COMPONENT_NFSPROTO,
		     "REQUEST PROCESSING: Calling RQUOTA_SETQUOTA");

	/* check rquota version and extract arguments */
	if (req->rq_msg.cb_vers == EXT_RQUOTAVERS) {
		quota_path = arg->arg_ext_rquota_setquota.sqa_pathp;
		quota_id = arg->arg_ext_rquota_setquota.sqa_id;
		quota_type = arg->arg_ext_rquota_setquota.sqa_type;
		quota_dqblk = &arg->arg_ext_rquota_setquota.sqa_dqblk;
	} else {
		quota_path = arg->arg_rquota_setquota.sqa_pathp;
		quota_id = arg->arg_rquota_setquota.sqa_id;
		quota_dqblk = &arg->arg_rquota_setquota.sqa_dqblk;
	}

	return do_rquota_setquota(quota_path, quota_type,
				  quota_id, quota_dqblk, req, qres);
}                               /* rquota_setquota */

static int do_rquota_setquota(char *quota_path, int quota_type,
			      int quota_id,
			      sq_dqblk *quota_dqblk,
			      struct svc_req *req,
			      setquota_rslt *qres)
{
	fsal_status_t fsal_status;
	fsal_quota_t fsal_quota_in;
	fsal_quota_t fsal_quota_out;
	struct gsh_export *exp = NULL;
	char *qpath;
	char path[MAXPATHLEN];

	qres->status = Q_EPERM;

	qpath = check_handle_lead_slash(quota_path, path,
					MAXPATHLEN);
	if (qpath == NULL)
		return NFS_REQ_OK;

	/*  Find the export for the dirname (using as well Path, Pseudo, or Tag)
	 */
	if (qpath[0] != '/') {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by tag for %s",
			     qpath);
		exp = get_gsh_export_by_tag(qpath);
	} else if (nfs_param.core_param.mount_path_pseudo) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by pseudo for %s",
			     qpath);
		exp = get_gsh_export_by_pseudo(qpath, false);
	} else {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Searching for export by path for %s",
			     qpath);
		exp = get_gsh_export_by_path(qpath, false);
	}

	if (exp == NULL) {
		/* No export found, return ACCESS error. */
		LogEvent(COMPONENT_NFSPROTO,
			 "Export entry for %s not found", qpath);

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

	memset(&fsal_quota_in, 0, sizeof(fsal_quota_t));
	memset(&fsal_quota_out, 0, sizeof(fsal_quota_t));

	fsal_quota_in.bhardlimit = quota_dqblk->rq_bhardlimit;
	fsal_quota_in.bsoftlimit = quota_dqblk->rq_bsoftlimit;
	fsal_quota_in.curblocks = quota_dqblk->rq_curblocks;
	fsal_quota_in.fhardlimit = quota_dqblk->rq_fhardlimit;
	fsal_quota_in.fsoftlimit = quota_dqblk->rq_fsoftlimit;
	fsal_quota_in.btimeleft = quota_dqblk->rq_btimeleft;
	fsal_quota_in.ftimeleft = quota_dqblk->rq_ftimeleft;

	fsal_status = exp->fsal_export->exp_ops.set_quota(
				exp->fsal_export, CTX_FULLPATH(op_ctx),
				quota_type, quota_id, &fsal_quota_in,
				&fsal_quota_out);

	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_NO_QUOTA)
			qres->status = Q_NOQUOTA;
		return NFS_REQ_OK;
	}

	/* is success */

	qres->setquota_rslt_u.sqr_rquota.rq_active = TRUE;
	qres->setquota_rslt_u.sqr_rquota.rq_bhardlimit =
	    fsal_quota_out.bhardlimit;
	qres->setquota_rslt_u.sqr_rquota.rq_bsoftlimit =
	    fsal_quota_out.bsoftlimit;
	qres->setquota_rslt_u.sqr_rquota.rq_curblocks =
	    fsal_quota_out.curblocks;
	qres->setquota_rslt_u.sqr_rquota.rq_fhardlimit =
	    fsal_quota_out.fhardlimit;
	qres->setquota_rslt_u.sqr_rquota.rq_fsoftlimit =
	    fsal_quota_out.fsoftlimit;
	qres->setquota_rslt_u.sqr_rquota.rq_btimeleft =
	    fsal_quota_out.btimeleft;
	qres->setquota_rslt_u.sqr_rquota.rq_ftimeleft =
	    fsal_quota_out.ftimeleft;
	qres->status = Q_OK;

	return NFS_REQ_OK;
}				/* do_rquota_setquota */

/**
 * @brief Frees the result structure allocated for rquota_setquota
 *
 * @param[in,out] res Pointer to the result structure.
 *
 */
void rquota_setquota_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
