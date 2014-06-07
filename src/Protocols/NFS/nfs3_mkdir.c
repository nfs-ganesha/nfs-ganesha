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
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
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
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_mkdir(nfs_arg_t *arg,
	       nfs_worker_data_t *worker,
	       struct svc_req *req, nfs_res_t *res)
{
	const char *dir_name = arg->arg_mkdir3.where.name;
	uint32_t mode = 0;
	cache_entry_t *dir_entry = NULL;
	cache_entry_t *parent_entry = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	int rc = NFS_REQ_OK;
	fsal_status_t fsal_status;
	struct attrlist sattr;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_vers, &(arg->arg_mkdir3.where.dir),
				 NULL, str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_mkdir handle: %s "
			 "name: %s", str, dir_name);
	}

	/* to avoid setting it on each error case */
	res->res_mkdir3.MKDIR3res_u.resfail.dir_wcc.before.attributes_follow =
	    FALSE;
	res->res_mkdir3.MKDIR3res_u.resfail.dir_wcc.after.attributes_follow =
	    FALSE;

	parent_entry = nfs3_FhandleToCache(&arg->arg_mkdir3.where.dir,
					   &res->res_mkdir3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Sanity checks */
	if (parent_entry->type != DIRECTORY) {
		res->res_mkdir3.status = NFS3ERR_NOTDIR;

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
		res->res_mkdir3.status = NFS3ERR_DQUOT;

		rc = NFS_REQ_OK;
		goto out;
	}

	if (dir_name == NULL || *dir_name == '\0') {
		cache_status = CACHE_INODE_INVALID_ARGUMENT;
		goto out_fail;
	}

	if (arg->arg_mkdir3.attributes.mode.set_it)
		mode = arg->arg_mkdir3.attributes.mode.set_mode3_u.mode;
	else
		mode = 0;

	/* Try to create the directory */
	cache_status =
	    cache_inode_create(parent_entry, dir_name, DIRECTORY, mode, NULL,
			       &dir_entry);

	if (cache_status != CACHE_INODE_SUCCESS)
		goto out_fail;

	memset(&sattr, 0, sizeof(sattr));

	if (nfs3_Sattr_To_FSALattr(&sattr, &arg->arg_mkdir3.attributes) == 0) {
		cache_status = CACHE_INODE_INVALID_ARGUMENT;
		goto out_fail;
	}

	/*Set attributes if required */
	squash_setattr(&sattr);

	if ((sattr.mask & (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME))
	    || ((sattr.mask & ATTR_OWNER)
		&& (op_ctx->creds->caller_uid != sattr.owner))
	    || ((sattr.mask & ATTR_GROUP)
		&& (op_ctx->creds->caller_gid != sattr.group))) {
		cache_status =
		    cache_inode_setattr(dir_entry, &sattr, false);

		if (cache_status != CACHE_INODE_SUCCESS)
			goto out_fail;
	}

	MKDIR3resok *d3ok = &res->res_mkdir3.MKDIR3res_u.resok;

	/* Build file handle */
	res->res_mkdir3.status =
	    nfs3_AllocateFH(&d3ok->obj.post_op_fh3_u.handle);

	if (res->res_mkdir3.status != NFS3_OK) {
		rc = NFS_REQ_OK;
		goto out;
	}

	if (!nfs3_FSALToFhandle(&d3ok->obj.post_op_fh3_u.handle,
				dir_entry->obj_handle,
				 op_ctx->export)) {
		gsh_free(d3ok->obj.post_op_fh3_u.handle.data.data_val);
		res->res_mkdir3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	d3ok->obj.handle_follows = true;

	/* Build entry attributes */
	nfs_SetPostOpAttr(dir_entry, &d3ok->obj_attributes);

	/* Build Weak Cache Coherency data */
	nfs_SetWccData(&pre_parent, parent_entry, &d3ok->dir_wcc);

	res->res_mkdir3.status = NFS3_OK;
	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_mkdir3.status = nfs3_Errno(cache_status);
	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_mkdir3.MKDIR3res_u.resfail.dir_wcc);

	if (nfs_RetryableError(cache_status))
		rc = NFS_REQ_DROP;

 out:
	/* return references */
	if (dir_entry)
		cache_inode_put(dir_entry);

	if (parent_entry)
		cache_inode_put(parent_entry);

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
	if ((res->res_mkdir3.status == NFS3_OK)
	    && (res->res_mkdir3.MKDIR3res_u.resok.obj.handle_follows)) {
		gsh_free(res->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.
			 handle.data.data_val);
	}
}
