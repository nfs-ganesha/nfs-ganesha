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
 * @file    nfs3_pathconf.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief Implements NFSPROC3_PATHCONF
 *
 * Implements NFSPROC3_PATHCONF.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_pathconf(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj = NULL;
	int rc = NFS_REQ_OK;
	struct fsal_export *exp_hdl = op_ctx->fsal_export;
	PATHCONF3resfail *resfail = &res->res_pathconf3.PATHCONF3res_u.resfail;
	PATHCONF3resok *resok = &res->res_pathconf3.PATHCONF3res_u.resok;

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_pathconf3.object,
			  "");

	/* to avoid setting it on each error case */
	resfail->obj_attributes.attributes_follow = FALSE;

	/* Convert file handle into a fsal_handle */
	obj = nfs3_FhandleToCache(&arg->arg_pathconf3.object,
				    &res->res_pathconf3.status,
				    &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	resok->linkmax = exp_hdl->exp_ops.fs_maxlink(exp_hdl);
	resok->name_max = exp_hdl->exp_ops.fs_maxnamelen(exp_hdl);
	resok->no_trunc = exp_hdl->exp_ops.fs_supports(exp_hdl, fso_no_trunc);
	resok->chown_restricted =
	    exp_hdl->exp_ops.fs_supports(exp_hdl, fso_chown_restricted);
	resok->case_insensitive =
	    exp_hdl->exp_ops.fs_supports(exp_hdl, fso_case_insensitive);
	resok->case_preserving =
	    exp_hdl->exp_ops.fs_supports(exp_hdl, fso_case_preserving);

	/* Build post op file attributes */
	nfs_SetPostOpAttr(obj, &resok->obj_attributes, NULL);

 out:

	if (obj)
		obj->obj_ops->put_ref(obj);

	return rc;
}				/* nfs3_pathconf */

/**
 * @brief Free the result structure allocated for nfs3_pathconf.
 *
 * This function free the result structure allocated for nfs3_pathconf.
 *
 * @param[in,out] res Result structure.
 *
 */
void nfs3_pathconf_free(nfs_res_t *res)
{
	/* Nothing to do here */
}
