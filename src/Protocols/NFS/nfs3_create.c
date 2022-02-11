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
 * version 3 of the License, or (at your option) any later version.
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
 * @file  nfs3_create.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
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
#include "fsal_convert.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFSPROC3_CREATE
 *
 * Implements the NFSPROC3_CREATE function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_create(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	const char *file_name = arg->arg_create3.where.name;
	struct fsal_obj_handle *file_obj = NULL;
	struct fsal_obj_handle *parent_obj = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	struct fsal_attrlist sattr, attrs;
	fsal_status_t fsal_status = {0, 0};
	int rc = NFS_REQ_OK;
	fsal_verifier_t verifier;
	enum fsal_create_mode createmode;

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_create3.where.dir,
			  " name: %s",
			 file_name ? file_name : "");

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	memset(&sattr, 0, sizeof(struct fsal_attrlist));

	/* to avoid setting it on each error case */
	res->res_create3.CREATE3res_u.resfail.dir_wcc.before.attributes_follow =
	    FALSE;
	res->res_create3.CREATE3res_u.resfail.dir_wcc.after.attributes_follow =
	    FALSE;

	parent_obj = nfs3_FhandleToCache(&arg->arg_create3.where.dir,
					   &res->res_create3.status,
					   &rc);

	if (parent_obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* get directory attributes before action (for V3 reply) */
	nfs_SetPreOpAttr(parent_obj, &pre_parent);

	/* Sanity checks: new file name must be non-null; parent must
	   be a directory. */
	if (parent_obj->type != DIRECTORY) {
		res->res_create3.status = NFS3ERR_NOTDIR;
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
		res->res_create3.status = NFS3ERR_DQUOT;
		rc = NFS_REQ_OK;
		goto out;
	}

	if (file_name == NULL || *file_name == '\0') {
		fsal_status = fsalstat(ERR_FSAL_INVAL, 0);
		goto out_fail;
	}

	/* Check if asked attributes are correct */
	if (arg->arg_create3.how.mode == GUARDED
	    || arg->arg_create3.how.mode == UNCHECKED) {
		if (nfs3_Sattr_To_FSALattr(
		     &sattr,
		     &arg->arg_create3.how.createhow3_u.obj_attributes) == 0) {
			res->res_create3.status = NFS3ERR_INVAL;
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	if (!(sattr.valid_mask & ATTR_MODE)) {
		/* Make sure mode is set. */
		sattr.mode = 0600;
		sattr.valid_mask |= ATTR_MODE;
	}

	/* Set the createmode */
	createmode = nfs3_createmode_to_fsal(arg->arg_create3.how.mode);

	if (createmode == FSAL_EXCLUSIVE) {
		/* Set the verifier if EXCLUSIVE */
		memcpy(verifier,
		       &arg->arg_create3.how.createhow3_u.verf,
		       sizeof(fsal_verifier_t));
	}

	/* If owner or owner_group are set, and the credential was
	 * squashed, then we must squash the set owner and owner_group.
	 */
	squash_setattr(&sattr);

	/* Stateless open, assume Read/Write. */
	fsal_status = fsal_open2(parent_obj,
				 NULL,
				 FSAL_O_RDWR,
				 createmode,
				 file_name,
				 &sattr,
				 verifier,
				 &file_obj,
				 &attrs);

	if (FSAL_IS_ERROR(fsal_status))
		goto out_fail;

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	/* Build file handle and set Post Op Fh3 structure */
	if (!nfs3_FSALToFhandle(
	     true,
	     &res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle,
	     file_obj, op_ctx->ctx_export)) {
		res->res_create3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	res->res_create3.CREATE3res_u.resok.obj.handle_follows = TRUE;

	/* Build entry attributes */
	nfs_SetPostOpAttr(file_obj,
			  &res->res_create3.CREATE3res_u.resok.obj_attributes,
			  &attrs);

	nfs_SetWccData(&pre_parent, parent_obj,
		       &res->res_create3.CREATE3res_u.resok.dir_wcc);

	res->res_create3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (file_obj)
		file_obj->obj_ops->put_ref(file_obj);

	if (parent_obj)
		parent_obj->obj_ops->put_ref(parent_obj);

	return rc;

 out_fail:

	/* Release the attributes. */
	fsal_release_attrs(&attrs);

	if (nfs_RetryableError(fsal_status.major)) {
		rc = NFS_REQ_DROP;
	} else {
		res->res_create3.status = nfs3_Errno_status(fsal_status);

		nfs_SetWccData(&pre_parent, parent_obj,
			       &res->res_create3.CREATE3res_u.resfail.dir_wcc);
	}
	goto out;
}				/* nfs3_create */

/**
 * @brief Free the result structure allocated for nfs3_create.
 *
 * Thsi function frees the result structure allocated for nfs3_create.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_create_free(nfs_res_t *res)
{
	nfs_fh3 *handle =
		&res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle;

	if ((res->res_create3.status == NFS3_OK)
	    && (res->res_create3.CREATE3res_u.resok.obj.handle_follows)) {
		gsh_free(handle->data.data_val);
	}
}
