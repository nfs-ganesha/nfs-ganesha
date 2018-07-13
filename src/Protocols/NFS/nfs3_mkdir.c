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
 * @file  nfs3_mkdir.c
 * @brief Evrythinhg you need to handle NFSv3 MKDIR
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
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFSPROC3_MKDIR
 *
 * Implements the NFSPROC3_MKDIR.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_mkdir(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	const char *dir_name = arg->arg_mkdir3.where.name;
	struct fsal_obj_handle *dir_obj = NULL;
	struct fsal_obj_handle *parent_obj = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	fsal_status_t fsal_status = {0, 0};
	int rc = NFS_REQ_OK;
	struct attrlist sattr, attrs;
	MKDIR3resfail *resfail = &res->res_mkdir3.MKDIR3res_u.resfail;
	MKDIR3resok *resok = &res->res_mkdir3.MKDIR3res_u.resok;

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	memset(&sattr, 0, sizeof(sattr));

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &(arg->arg_mkdir3.where.dir),
				 NULL, str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling NFS3_MKDIR handle: %s name: %s",
			 str, dir_name);
	}

	/* to avoid setting it on each error case */
	resfail->dir_wcc.before.attributes_follow = FALSE;
	resfail->dir_wcc.after.attributes_follow = FALSE;

	parent_obj = nfs3_FhandleToCache(&arg->arg_mkdir3.where.dir,
					   &res->res_mkdir3.status,
					   &rc);

	if (parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Sanity checks */
	if (parent_obj->type != DIRECTORY) {
		res->res_mkdir3.status = NFS3ERR_NOTDIR;

		rc = NFS_REQ_OK;
		goto out;
	}

	/* if quota support is active, then we should check is the
	   FSAL allows inode creation or not */
	fsal_status =
	    op_ctx->fsal_export->exp_ops.check_quota(op_ctx->fsal_export,
						   op_ctx->ctx_export->fullpath,
						   FSAL_QUOTA_INODES);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_mkdir3.status = NFS3ERR_DQUOT;

		rc = NFS_REQ_OK;
		goto out;
	}

	if (dir_name == NULL || *dir_name == '\0') {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out_fail;
	}

	if (nfs3_Sattr_To_FSALattr(&sattr, &arg->arg_mkdir3.attributes) == 0) {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out_fail;
	}

	squash_setattr(&sattr);

	if (!(sattr.valid_mask & ATTR_MODE)) {
		/* Make sure mode is set. */
		sattr.mode = 0;
		sattr.valid_mask |= ATTR_MODE;
	}

	/* Try to create the directory */
	fsal_status = fsal_create(parent_obj, dir_name, DIRECTORY, &sattr, NULL,
				  &dir_obj, &attrs);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status))
		goto out_fail;

	/* Build file handle */
	if (!nfs3_FSALToFhandle(true,
				&resok->obj.post_op_fh3_u.handle,
				dir_obj,
				op_ctx->ctx_export)) {
		res->res_mkdir3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	resok->obj.handle_follows = true;

	/* Build entry attributes */
	nfs_SetPostOpAttr(dir_obj, &resok->obj_attributes, &attrs);

	/* Build Weak Cache Coherency data */
	nfs_SetWccData(&pre_parent, parent_obj, &resok->dir_wcc);

	res->res_mkdir3.status = NFS3_OK;
	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_mkdir3.status = nfs3_Errno_status(fsal_status);
	nfs_SetWccData(&pre_parent, parent_obj, &resfail->dir_wcc);

	if (nfs_RetryableError(fsal_status.major))
		rc = NFS_REQ_DROP;

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs);

	/* return references */
	if (dir_obj)
		dir_obj->obj_ops->put_ref(dir_obj);

	if (parent_obj)
		parent_obj->obj_ops->put_ref(parent_obj);

	return rc;
}

/**
 * @brief Free the result structure allocated for nfs3_mkdir.
 *
 * This function frees the result structure allocated for nfs3_mkdir.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_mkdir_free(nfs_res_t *res)
{
	nfs_fh3 *handle =
		&res->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.handle;

	if ((res->res_mkdir3.status == NFS3_OK)
	    && (res->res_mkdir3.MKDIR3res_u.resok.obj.handle_follows)) {
		gsh_free(handle->data.data_val);
	}
}
