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
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 *  version 3 of the License, or (at your option) any later version.
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
 * ---------------------------------------
 */

/**
 * @file    nfs3_mknod.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
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
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "export_mgr.h"

/**
 * @brief Implements NFSPROC3_MKNOD
 *
 * Implements NFSPROC3_MKNOD.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_mknod(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *parent_obj = NULL;
	struct fsal_obj_handle *node_obj = NULL;
	pre_op_attr pre_parent;
	object_file_type_t nodetype;
	const char *file_name = arg->arg_mknod3.where.name;
	int rc = NFS_REQ_OK;
	fsal_status_t fsal_status;
	struct fsal_attrlist sattr, attrs;
	MKNOD3resfail *resfail = &res->res_mknod3.MKNOD3res_u.resfail;
	MKNOD3resok * const rok = &res->res_mknod3.MKNOD3res_u.resok;

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	memset(&sattr, 0, sizeof(sattr));

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_mknod3.where.dir,
			  " name: %s", file_name);

	/* to avoid setting them on each error case */
	resfail->dir_wcc.before.attributes_follow = FALSE;
	resfail->dir_wcc.after.attributes_follow = FALSE;

	/* retrieve parent entry */
	parent_obj = nfs3_FhandleToCache(&arg->arg_mknod3.where.dir,
					   &res->res_mknod3.status,
					   &rc);

	if (parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_obj, &pre_parent);

	/* Sanity checks: new node name must be non-null; parent must
	   be a directory. */

	if (parent_obj->type != DIRECTORY) {
		res->res_mknod3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	if (file_name == NULL || *file_name == '\0') {
		res->res_mknod3.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		goto out;
	}

	switch (arg->arg_mknod3.what.type) {
	case NF3CHR:
	case NF3BLK:
		if (nfs3_Sattr_To_FSALattr(
		       &sattr,
		       &arg->arg_mknod3.what.mknoddata3_u.device.dev_attributes)
		    == 0) {
			res->res_mknod3.status = NFS3ERR_INVAL;
			rc = NFS_REQ_OK;
			goto out;
		}
		sattr.rawdev.major =
		    arg->arg_mknod3.what.mknoddata3_u.device.spec.specdata1;
		sattr.rawdev.minor =
		    arg->arg_mknod3.what.mknoddata3_u.device.spec.specdata2;
		sattr.valid_mask |= ATTR_RAWDEV;

		break;

	case NF3FIFO:
	case NF3SOCK:
		if (nfs3_Sattr_To_FSALattr(
			&sattr,
			&arg->arg_mknod3.what.mknoddata3_u.pipe_attributes)
		    == 0) {
			res->res_mknod3.status = NFS3ERR_INVAL;
			rc = NFS_REQ_OK;
			goto out;
		}

		break;

	default:
		res->res_mknod3.status = NFS3ERR_BADTYPE;
		rc = NFS_REQ_OK;
		goto out;
	}

	switch (arg->arg_mknod3.what.type) {
	case NF3CHR:
		nodetype = CHARACTER_FILE;
		break;
	case NF3BLK:
		nodetype = BLOCK_FILE;
		break;
	case NF3FIFO:
		nodetype = FIFO_FILE;
		break;
	case NF3SOCK:
		nodetype = SOCKET_FILE;
		break;
	default:
		res->res_mknod3.status = NFS3ERR_BADTYPE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* if quota support is active, then we should check is the
	   FSAL allows inode creation or not */
	fsal_status = op_ctx->fsal_export->exp_ops.check_quota(
							op_ctx->fsal_export,
							CTX_FULLPATH(op_ctx),
							FSAL_QUOTA_INODES);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_mknod3.status = NFS3ERR_DQUOT;
		return NFS_REQ_OK;
	}

	squash_setattr(&sattr);

	if (!(sattr.valid_mask & ATTR_MODE)) {
		/* Make sure mode is set. */
		sattr.mode = 0;
		sattr.valid_mask |= ATTR_MODE;
	}

	/* Try to create it */
	fsal_status = fsal_create(parent_obj, file_name, nodetype, &sattr,
				  NULL, &node_obj, &attrs);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status))
		goto out_fail;

	/* Build file handle */
	if (!nfs3_FSALToFhandle(true,
				&rok->obj.post_op_fh3_u.handle,
				node_obj,
				op_ctx->ctx_export)) {
		res->res_mknod3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	rok->obj.handle_follows = TRUE;

	/* Build entry attributes */
	nfs_SetPostOpAttr(node_obj, &rok->obj_attributes, &attrs);

	/* Build Weak Cache Coherency data */
	nfs_SetWccData(&pre_parent, parent_obj, &rok->dir_wcc);

	res->res_mknod3.status = NFS3_OK;

	rc = NFS_REQ_OK;
	goto out;

 out_fail:
	res->res_mknod3.status = nfs3_Errno_status(fsal_status);
	nfs_SetWccData(&pre_parent, parent_obj, &resfail->dir_wcc);

	if (nfs_RetryableError(fsal_status.major))
		rc = NFS_REQ_DROP;

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs);

	/* return references */
	if (parent_obj)
		parent_obj->obj_ops->put_ref(parent_obj);

	if (node_obj)
		node_obj->obj_ops->put_ref(node_obj);

	return rc;
}				/* nfs3_mknod */

/**
 * @brief Free the result structure allocated for nfs3_mknod.
 *
 * This function frees the result structure allocated for nfs3_mknod.
 *
 * @param[in,out] res The result structure.
 *
 */
void nfs3_mknod_free(nfs_res_t *res)
{
	nfs_fh3 *handle =
		&res->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.handle;

	if ((res->res_mknod3.status == NFS3_OK)
	    && (res->res_mknod3.MKNOD3res_u.resok.obj.handle_follows)) {
		gsh_free(handle->data.data_val);
	}
}
