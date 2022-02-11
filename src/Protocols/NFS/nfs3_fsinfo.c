// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 *
 * ---------------------------------------
 */

/**
 * @file    nfs3_fsinfo.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"

/**
 * @brief Implement NFSPROC3_FSINFO
 *
 * This function Implements NFSPROC3_FSINFO.
 *
 * @todo ACE: This function uses a lot of constants instead of
 * retrieving information from the FSAL as it ought.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_fsinfo(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj = NULL;
	int rc = NFS_REQ_OK;
	FSINFO3resok * const FSINFO_FIELD =
		&res->res_fsinfo3.FSINFO3res_u.resok;
	fsal_dynamicfsinfo_t dynamicinfo;
	fsal_status_t fsal_status;

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_fsinfo3.fsroot,
			  "");

	/* To avoid setting it on each error case */
	res->res_fsinfo3.FSINFO3res_u.resfail.obj_attributes.attributes_follow =
	    FALSE;

	obj = nfs3_FhandleToCache(&arg->arg_fsinfo3.fsroot,
				    &res->res_fsinfo3.status,
				    &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Get statistics and convert from FSAL to get time_delta */
	fsal_status = fsal_statfs(obj, &dynamicinfo);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* At this point we met an error */
		LogFullDebug(COMPONENT_NFSPROTO,
			     "failed statfs: fsal_status=%s",
			     fsal_err_txt(fsal_status));

		if (nfs_RetryableError(fsal_status.major)) {
			/* Drop retryable errors. */
			rc = NFS_REQ_DROP;
		} else {
			res->res_fsstat3.status =
						nfs3_Errno_status(fsal_status);
			rc = NFS_REQ_OK;
		}

		goto out;
	}

	/* New fields were added to nfs_config_t to handle this
	   value. We use them */

	FSINFO_FIELD->rtmax =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxRead);
	FSINFO_FIELD->rtpref =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->PrefRead);
	/* This field is generally unused, it will be removed in V4 */
	FSINFO_FIELD->rtmult = DEV_BSIZE;

	FSINFO_FIELD->wtmax =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxWrite);
	FSINFO_FIELD->wtpref =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->PrefWrite);
	/* This field is generally unused, it will be removed in V4 */
	FSINFO_FIELD->wtmult = DEV_BSIZE;

	FSINFO_FIELD->dtpref =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->PrefReaddir);
	FSINFO_FIELD->maxfilesize =
	    op_ctx->fsal_export->exp_ops.fs_maxfilesize(op_ctx->fsal_export);
	FSINFO_FIELD->time_delta.tv_sec = dynamicinfo.time_delta.tv_sec;
	FSINFO_FIELD->time_delta.tv_nsec = dynamicinfo.time_delta.tv_nsec;

	LogFullDebug(COMPONENT_NFSPROTO,
		     "rtmax = %d | rtpref = %d | trmult = %d",
		     FSINFO_FIELD->rtmax, FSINFO_FIELD->rtpref,
		     FSINFO_FIELD->rtmult);
	LogFullDebug(COMPONENT_NFSPROTO,
		     "wtmax = %d | wtpref = %d | wrmult = %d",
		     FSINFO_FIELD->wtmax, FSINFO_FIELD->wtpref,
		     FSINFO_FIELD->wtmult);
	LogFullDebug(COMPONENT_NFSPROTO, "dtpref = %d | maxfilesize = %"PRIu64,
		     FSINFO_FIELD->dtpref, FSINFO_FIELD->maxfilesize);

	/* Allow all kinds of operations to be performed on the server
	   through NFS v3 */
	FSINFO_FIELD->properties =
	    FSF3_LINK | FSF3_SYMLINK | FSF3_HOMOGENEOUS | FSF3_CANSETTIME;

	nfs_SetPostOpAttr(obj,
			  &res->res_fsinfo3.FSINFO3res_u.resok.obj_attributes,
			  NULL);
	res->res_fsinfo3.status = NFS3_OK;

 out:

	if (obj)
		obj->obj_ops->put_ref(obj);

	return rc;
}				/* nfs3_fsinfo */

/**
 * @brief Free the result structure allocated for nfs3_fsinfo.
 *
 * This function frees the result structure allocated for nfs3_fsinfo.
 *
 * @param[in,out] res The result structure
 *
 */
void nfs3_fsinfo_free(nfs_res_t *res)
{
	/* Nothing to do here */
}
