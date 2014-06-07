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
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
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
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_mknod(nfs_arg_t *arg,
	       nfs_worker_data_t *worker,
	       struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *parent_entry = NULL;
	pre_op_attr pre_parent;
	object_file_type_t nodetype;
	const char *file_name = arg->arg_mknod3.where.name;
	cache_inode_status_t cache_status;
	uint32_t mode = 0;
	cache_entry_t *node_entry = NULL;
	cache_inode_create_arg_t create_arg;
	int rc = NFS_REQ_OK;
	fsal_status_t fsal_status;
	struct attrlist sattr;

	memset(&create_arg, 0, sizeof(create_arg));
	memset(&sattr, 0, sizeof(sattr));

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];
		const char *fname;

		fname = (file_name == NULL || *file_name == '\0') ?
			"<empty name>" : file_name;
		sprint_fhandle3(str, &(arg->arg_mknod3.where.dir));
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_mknod handle: %s "
			 "name: %s", str, fname);
	}

	/* to avoid setting them on each error case */
	res->res_mknod3.MKNOD3res_u.resfail.dir_wcc.before.attributes_follow =
	    FALSE;
	res->res_mknod3.MKNOD3res_u.resfail.dir_wcc.after.attributes_follow =
	    FALSE;

	/* retrieve parent entry */
	parent_entry = nfs3_FhandleToCache(&arg->arg_mknod3.where.dir,
					   &res->res_mknod3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_entry, &pre_parent);

	/* Sanity checks: new node name must be non-null; parent must
	   be a directory. */

	if (parent_entry->type != DIRECTORY) {
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
		if (arg->arg_mknod3.what.mknoddata3_u.device.dev_attributes.
		    mode.set_it) {
			mode = arg->arg_mknod3.what.mknoddata3_u.device.
					dev_attributes.mode.set_mode3_u.mode;
		} else {
			mode = 0;
		}

		create_arg.dev_spec.major =
		    arg->arg_mknod3.what.mknoddata3_u.device.spec.specdata1;
		create_arg.dev_spec.minor =
		    arg->arg_mknod3.what.mknoddata3_u.device.spec.specdata2;

		if (nfs3_Sattr_To_FSALattr(&sattr,
					   &arg->arg_mknod3.what.mknoddata3_u.
						device.dev_attributes) == 0) {
			res->res_mknod3.status = NFS3ERR_INVAL;
			rc = NFS_REQ_OK;
			goto out;
		}

		break;

	case NF3FIFO:
	case NF3SOCK:
		if (arg->arg_mknod3.what.mknoddata3_u.pipe_attributes.mode.
		    set_it)
			mode = (arg->arg_mknod3.what.mknoddata3_u.
				pipe_attributes.mode.set_mode3_u.mode);
		else
			mode = 0;

		create_arg.dev_spec.major = 0;
		create_arg.dev_spec.minor = 0;

		if (nfs3_Sattr_To_FSALattr(&sattr,
					   &arg->arg_mknod3.what.mknoddata3_u.
						pipe_attributes) == 0) {
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
	fsal_status =
	    op_ctx->fsal_export->ops->check_quota(op_ctx->fsal_export,
						   op_ctx->export->fullpath,
						   FSAL_QUOTA_INODES);
	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_mknod3.status = NFS3ERR_DQUOT;
		return NFS_REQ_OK;
	}

	/* Try to create it */
	cache_status = cache_inode_create(parent_entry,
					  file_name,
					  nodetype,
					  mode,
					  &create_arg,
					  &node_entry);

	if (cache_status != CACHE_INODE_SUCCESS)
		goto out_fail;

	MKNOD3resok * const rok = &res->res_mknod3.MKNOD3res_u.resok;

	/* Build file handle */
	res->res_mknod3.status =
		nfs3_AllocateFH(&rok->obj.post_op_fh3_u.handle);

	if (res->res_mknod3.status != NFS3_OK) {
		rc = NFS_REQ_OK;
		goto out;
	}

	if (nfs3_FSALToFhandle(&rok->obj.post_op_fh3_u.handle,
			       node_entry->obj_handle,
			       op_ctx->export) == 0) {
		gsh_free(rok->obj.post_op_fh3_u.handle.data.data_val);
		res->res_mknod3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	rok->obj.handle_follows = TRUE;

	/*Set attributes if required */
	squash_setattr(&sattr);

	if ((sattr.mask & (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME))
	    || ((sattr.mask & ATTR_OWNER)
		&& (op_ctx->creds->caller_uid != sattr.owner))
	    || ((sattr.mask & ATTR_GROUP)
		&& (op_ctx->creds->caller_gid != sattr.group))) {
		cache_status = cache_inode_setattr(node_entry,
						   &sattr,
						   false);

		if (cache_status != CACHE_INODE_SUCCESS)
			goto out_fail;
	}

	/* Build entry attributes */
	nfs_SetPostOpAttr(node_entry, &rok->obj_attributes);

	/* Build Weak Cache Coherency data */
	nfs_SetWccData(&pre_parent, parent_entry, &rok->dir_wcc);

	res->res_mknod3.status = NFS3_OK;

	rc = NFS_REQ_OK;
	goto out;

 out_fail:
	res->res_mknod3.status = nfs3_Errno(cache_status);
	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_mknod3.MKNOD3res_u.resfail.dir_wcc);

	if (nfs_RetryableError(cache_status))
		rc = NFS_REQ_DROP;

 out:
	/* return references */
	if (parent_entry)
		cache_inode_put(parent_entry);

	if (node_entry)
		cache_inode_put(node_entry);

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
	if ((res->res_mknod3.status == NFS3_OK)
	    && (res->res_mknod3.MKNOD3res_u.resok.obj.handle_follows)) {
		gsh_free(res->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.
			 handle.data.data_val);
	}
}
