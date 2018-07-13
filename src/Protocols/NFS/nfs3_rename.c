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
 * @file  nfs3_rename.c
 * @brief Everything you need for NFSv3 RENAME
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "client_mgr.h"

/**
 *
 * @brief The NFSPROC3_RENAME
 *
 * Implements the NFSPROC3_RENAME function.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_rename(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	const char *entry_name = arg->arg_rename3.from.name;
	const char *new_entry_name = arg->arg_rename3.to.name;
	struct fsal_obj_handle *parent_obj = NULL;
	struct fsal_obj_handle *new_parent_obj = NULL;
	fsal_status_t fsal_status;
	int to_exportid = 0;
	int from_exportid = 0;
	int rc = NFS_REQ_OK;
	RENAME3resfail *resfail = &res->res_rename3.RENAME3res_u.resfail;
	RENAME3resok *resok = &res->res_rename3.RENAME3res_u.resok;

	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	pre_op_attr pre_new_parent = {
		.attributes_follow = false
	};

	if (isDebug(COMPONENT_NFSPROTO)) {
		char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &arg->arg_rename3.from.dir,
				 NULL,
				 strfrom);

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &arg->arg_rename3.to.dir,
				 NULL,
				 strto);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Rename from handle: %s name %s to handle: %s name: %s",
			 strfrom, entry_name, strto, new_entry_name);
	}

	/* to avoid setting it on each error case */
	resfail->fromdir_wcc.before.attributes_follow = FALSE;
	resfail->fromdir_wcc.after.attributes_follow = FALSE;
	resfail->todir_wcc.before.attributes_follow = FALSE;
	resfail->todir_wcc.after.attributes_follow = FALSE;

	/* Get the exportids for the two handles. */
	to_exportid = nfs3_FhandleToExportId(&(arg->arg_rename3.to.dir));
	from_exportid = nfs3_FhandleToExportId(&(arg->arg_rename3.from.dir));

	/* Validate the to_exportid */
	if (to_exportid < 0 || from_exportid < 0) {
		LogInfo(COMPONENT_DISPATCH,
			"NFS%d RENAME Request from client %s has badly formed handle for to dir",
			req->rq_msg.cb_vers,
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");

		/* Bad handle, report to client */
		res->res_rename3.status = NFS3ERR_BADHANDLE;
		goto out;
	}

	/* Both objects have to be in the same filesystem */
	if (to_exportid != from_exportid) {
		res->res_rename3.status = NFS3ERR_XDEV;
		goto out;
	}

	/* Convert fromdir file handle into a FSAL obj */
	parent_obj = nfs3_FhandleToCache(&arg->arg_rename3.from.dir,
					   &res->res_rename3.status,
					   &rc);

	if (parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_obj, &pre_parent);

	/* Convert todir file handle into a FSAL obj */
	new_parent_obj = nfs3_FhandleToCache(&arg->arg_rename3.to.dir,
					       &res->res_rename3.status,
					       &rc);

	if (new_parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(new_parent_obj, &pre_new_parent);

	if (entry_name == NULL || *entry_name == '\0' || new_entry_name == NULL
	    || *new_entry_name == '\0') {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out_fail;
	}

	fsal_status = fsal_rename(parent_obj, entry_name, new_parent_obj,
				  new_entry_name);

	if (FSAL_IS_ERROR(fsal_status))
		goto out_fail;

	res->res_rename3.status = NFS3_OK;

	nfs_SetWccData(&pre_parent, parent_obj, &resok->fromdir_wcc);

	nfs_SetWccData(&pre_new_parent, new_parent_obj, &resok->todir_wcc);

	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_rename3.status = nfs3_Errno_status(fsal_status);

	nfs_SetWccData(&pre_parent, parent_obj, &resfail->fromdir_wcc);

	nfs_SetWccData(&pre_new_parent, new_parent_obj, &resfail->todir_wcc);

	/* If we are here, there was an error */
	if (nfs_RetryableError(fsal_status.major))
		rc = NFS_REQ_DROP;

 out:
	if (parent_obj)
		parent_obj->obj_ops->put_ref(parent_obj);

	if (new_parent_obj)
		new_parent_obj->obj_ops->put_ref(new_parent_obj);

	return rc;
}

/**
 * @brief Free the result structure allocated for nfs3_rename.
 *
 * This function frees the result structure allocated for nfs3_rename.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_rename_free(nfs_res_t *res)
{
	/* Nothing to do here */
}
