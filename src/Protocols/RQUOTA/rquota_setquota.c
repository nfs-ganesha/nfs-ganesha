/*
 * Copyright CEA/DAM/DIF  2010
 *  Author: Philippe Deniel (philippe.deniel@cea.fr)
 *
 * --------------------------
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <os/quota.h>		/* For USRQUOTA */
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "rquota.h"
#include "nfs_proto_functions.h"
#include "export_mgr.h"

/**
 * @brief The Rquota setquota function, for all versions.
 *
 * The RQUOTA setquota function, for all versions.
 *
 * @param[in]  parg    quota args
 * @param[in]  pexport Ignored
 * @param[in]  req_ctx Ignored
 * @param[in]  pworker Ignored
 * @param[in]  preq    Ignored
 * @param[out] pres    returned quota (modified)
 *
 */
int rquota_setquota(nfs_arg_t * parg, exportlist_t * pexport,
		    struct req_op_context *req_ctx, nfs_worker_data_t * pworker,
		    struct svc_req *preq, nfs_res_t * pres)
{
	fsal_status_t fsal_status;
	fsal_quota_t fsal_quota_in;
	fsal_quota_t fsal_quota_out;
	int quota_type = USRQUOTA;
	struct gsh_export *exp;
	char *quota_path;
	setquota_args *qarg = &parg->arg_rquota_setquota;
	setquota_rslt *qres = &pres->res_rquota_setquota;

	LogFullDebug(COMPONENT_NFSPROTO,
		     "REQUEST PROCESSING: Calling rquota_setquota");

	if (preq->rq_vers == EXT_RQUOTAVERS)
		quota_type = parg->arg_ext_rquota_setquota.sqa_type;

	qres->status = Q_EPERM;
	if (qarg->sqa_pathp[0] == '/') {
		exp = get_gsh_export_by_path(qarg->sqa_pathp);
		if (exp == NULL)
			goto out;
		quota_path = qarg->sqa_pathp;
	} else {
		exp = get_gsh_export_by_tag(qarg->sqa_pathp);
		if (exp == NULL)
			goto out;
		quota_path = exp->export.fullpath;
	}

	memset(&fsal_quota_in, 0, sizeof(fsal_quota_t));
	memset(&fsal_quota_out, 0, sizeof(fsal_quota_t));

	fsal_quota_in.bhardlimit = qarg->sqa_dqblk.rq_bhardlimit;
	fsal_quota_in.bsoftlimit = qarg->sqa_dqblk.rq_bsoftlimit;
	fsal_quota_in.curblocks = qarg->sqa_dqblk.rq_curblocks;
	fsal_quota_in.fhardlimit = qarg->sqa_dqblk.rq_fhardlimit;
	fsal_quota_in.fsoftlimit = qarg->sqa_dqblk.rq_fsoftlimit;
	fsal_quota_in.btimeleft = qarg->sqa_dqblk.rq_btimeleft;
	fsal_quota_in.ftimeleft = qarg->sqa_dqblk.rq_ftimeleft;

	fsal_status =
	    exp->export.export_hdl->ops->set_quota(exp->export.export_hdl,
						   quota_path, quota_type,
						   req_ctx, &fsal_quota_in,
						   &fsal_quota_out);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_NO_QUOTA)
			qres->status = Q_NOQUOTA;
		goto out;
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

 out:
	return NFS_REQ_OK;
}				/* rquota_setquota */

/**
 * rquota_setquota_Free: Frees the result structure allocated for rquota_setquota
 *
 * Frees the result structure allocated for rquota_setquota. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void rquota_setquota_Free(nfs_res_t * pres)
{
	return;
}
