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
 * @file  nfs3_symlink.c
 * @brief Everything you need for NFSv3 SYMLINK
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
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFSPROC3_SYMLINK
 *
 * Implements the NFSPROC3_SYMLINK function.
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

int nfs3_symlink(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	const char *symlink_name = arg->arg_symlink3.where.name;
	char *target_path = arg->arg_symlink3.symlink.symlink_data;
	struct fsal_obj_handle *symlink_obj = NULL;
	struct fsal_obj_handle *parent_obj;
	pre_op_attr pre_parent;
	fsal_status_t fsal_status;
	int rc = NFS_REQ_OK;
	struct attrlist sattr, attrs;
	SYMLINK3resfail *resfail = &res->res_symlink3.SYMLINK3res_u.resfail;
	SYMLINK3resok *resok = &res->res_symlink3.SYMLINK3res_u.resok;

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	memset(&sattr, 0, sizeof(sattr));

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &arg->arg_symlink3.where.dir,
				 NULL,
				 str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Symlink handle: %s name: %s target: %s",
			 str, symlink_name, target_path);
	}

	/* to avoid setting it on each error case */
	resfail->dir_wcc.before.attributes_follow = false;
	resfail->dir_wcc.after.attributes_follow = false;

	parent_obj = nfs3_FhandleToCache(&arg->arg_symlink3.where.dir,
					   &res->res_symlink3.status,
					   &rc);

	if (parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_obj, &pre_parent);

	if (parent_obj->type != DIRECTORY) {
		res->res_symlink3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* if quota support is active, then we should check is the
	 * FSAL allows inode creation or not
	 */
	fsal_status =
	    op_ctx->fsal_export->exp_ops.check_quota(op_ctx->fsal_export,
						   op_ctx->ctx_export->fullpath,
						   FSAL_QUOTA_INODES);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_symlink3.status = NFS3ERR_DQUOT;
		rc = NFS_REQ_OK;
		goto out;
	}

	if (symlink_name == NULL || *symlink_name == '\0' || target_path == NULL
	    || *target_path == '\0') {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out_fail;
	}

	/* Some clients (like the Spec NFS benchmark) set
	 * attributes with the NFSPROC3_SYMLINK request
	 */
	if (!nfs3_Sattr_To_FSALattr(
			&sattr,
			&arg->arg_symlink3.symlink.symlink_attributes)) {
		res->res_symlink3.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		goto out;
	}

	squash_setattr(&sattr);

	if (!(sattr.valid_mask & ATTR_MODE)) {
		/* Make sure mode is set. */
		sattr.mode = 0777;
		sattr.valid_mask |= ATTR_MODE;
	}

	/* Make the symlink */
	fsal_status = fsal_create(parent_obj, symlink_name, SYMBOLIC_LINK,
				  &sattr, target_path, &symlink_obj, &attrs);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status))
		goto out_fail;


	if (!nfs3_FSALToFhandle(true, &resok->obj.post_op_fh3_u.handle,
				symlink_obj, op_ctx->ctx_export)) {
		res->res_symlink3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	resok->obj.handle_follows = TRUE;

	/* Build entry attributes */
	nfs_SetPostOpAttr(symlink_obj, &resok->obj_attributes, &attrs);

	/* Build Weak Cache Coherency data */
	nfs_SetWccData(&pre_parent, parent_obj, &resok->dir_wcc);

	res->res_symlink3.status = NFS3_OK;
	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_symlink3.status = nfs3_Errno_status(fsal_status);

	nfs_SetWccData(&pre_parent, parent_obj, &resfail->dir_wcc);

	if (nfs_RetryableError(fsal_status.major))
		rc = NFS_REQ_DROP;

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs);

	/* return references */
	if (parent_obj)
		parent_obj->obj_ops->put_ref(parent_obj);

	if (symlink_obj)
		symlink_obj->obj_ops->put_ref(symlink_obj);

	return rc;
}				/* nfs3_symlink */

/**
 * @brief Free the result structure allocated for nfs3_symlink.
 *
 * This function frees the result structure allocated for nfs3_symlink.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_symlink_free(nfs_res_t *res)
{
	SYMLINK3resok *resok = &res->res_symlink3.SYMLINK3res_u.resok;

	if (res->res_symlink3.status == NFS3_OK && resok->obj.handle_follows)
		gsh_free(resok->obj.post_op_fh3_u.handle.data.data_val);
}
