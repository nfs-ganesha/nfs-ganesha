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
#include "fsal_convert.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFSPROC3_CREATE
 *
 * Implements the NFSPROC3_CREATE function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_create(nfs_arg_t *arg,
		nfs_worker_data_t *worker,
		struct svc_req *req, nfs_res_t *res)
{
	const char *file_name = arg->arg_create3.where.name;
	uint32_t mode = 0;
	cache_entry_t *file_entry = NULL;
	cache_entry_t *parent_entry = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	struct attrlist sattr;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	int rc = NFS_REQ_OK;
	fsal_status_t fsal_status;
	/* Client provided verifier, split into two pieces */
	uint32_t verf_hi = 0, verf_lo = 0;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_vers, &(arg->arg_create3.where.dir),
				 NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_create handle: "
			 "%s name: %s", str, file_name ? file_name : "");
	}

	memset(&sattr, 0, sizeof(struct attrlist));

	/* to avoid setting it on each error case */
	res->res_create3.CREATE3res_u.resfail.dir_wcc.before.attributes_follow =
	    FALSE;
	res->res_create3.CREATE3res_u.resfail.dir_wcc.after.attributes_follow =
	    FALSE;

	parent_entry = nfs3_FhandleToCache(&arg->arg_create3.where.dir,
					   &res->res_create3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* get directory attributes before action (for V3 reply) */
	nfs_SetPreOpAttr(parent_entry, &pre_parent);

	/* Sanity checks: new file name must be non-null; parent must
	   be a directory. */
	if (parent_entry->type != DIRECTORY) {
		res->res_create3.status = NFS3ERR_NOTDIR;
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
		res->res_create3.status = NFS3ERR_DQUOT;
		rc = NFS_REQ_OK;
		goto out;
	}

	if (file_name == NULL || *file_name == '\0') {
		cache_status = CACHE_INODE_INVALID_ARGUMENT;
		goto out_fail;
	}

	/* Check if asked attributes are correct */
	if (arg->arg_create3.how.mode == GUARDED
	    || arg->arg_create3.how.mode == UNCHECKED) {
		if (arg->arg_create3.how.createhow3_u.obj_attributes.mode.
		    set_it) {
			mode =
			    unix2fsal_mode(arg->arg_create3.how.createhow3_u.
					   obj_attributes.mode.set_mode3_u.
					   mode);
		}

		if (nfs3_Sattr_To_FSALattr(
		     &sattr,
		     &arg->arg_create3.how.createhow3_u.obj_attributes) == 0) {
			res->res_create3.status = NFS3ERR_INVAL;
			rc = NFS_REQ_OK;
			goto out;
		}

		/* Mode is managed in cache_inode_create,
		   there is no need to manage it */
		FSAL_UNSET_MASK(sattr.mask, ATTR_MODE);
	} else if (arg->arg_create3.how.mode == EXCLUSIVE) {
		const char *verf =
		    (const char *)&(arg->arg_create3.how.createhow3_u.verf);
		/* If we knew all our FSALs could store a 64 bit
		   atime, we could just use that and there would be
		   no need to split the verifier up. */
		memcpy(&verf_hi, verf, sizeof(uint32_t));
		memcpy(&verf_lo, verf + sizeof(uint32_t), sizeof(uint32_t));

		cache_inode_create_set_verifier(&sattr, verf_hi, verf_lo);
	}

	cache_status = cache_inode_create(parent_entry,
					  file_name,
					  REGULAR_FILE,
					  mode,
					  NULL,
					  &file_entry);

	/* Complete failure */
	if (((cache_status != CACHE_INODE_SUCCESS)
	     && (cache_status != CACHE_INODE_ENTRY_EXISTS))
	    || (file_entry == NULL)) {
		goto out_fail;
	}

	if (cache_status == CACHE_INODE_ENTRY_EXISTS) {
		if (arg->arg_create3.how.mode == GUARDED) {
			goto out_fail;
		} else if (arg->arg_create3.how.mode == EXCLUSIVE
			   && !cache_inode_create_verify(file_entry,
							 verf_hi,
							 verf_lo)) {
			goto out_fail;
		}

		/* If the object exists already size is the only attribute we
		 * set.
		 */
		if (FSAL_TEST_MASK(sattr.mask, ATTR_SIZE)
		    && (sattr.filesize == 0)) {
			FSAL_CLEAR_MASK(sattr.mask);
			FSAL_SET_MASK(sattr.mask, ATTR_SIZE);
		} else {
			FSAL_CLEAR_MASK(sattr.mask);
		}

		/* Clear error code */
		cache_status = CACHE_INODE_SUCCESS;
	}

	/* Are there any attributes left to set? */
	if (sattr.mask) {
		/* If owner or owner_group are set, and the credential was
		 * squashed, then we must squash the set owner and owner_group.
		 */
		squash_setattr(&sattr);

		if ((sattr.mask & (ATTR_ATIME | ATTR_MTIME | ATTR_CTIME |
				   ATTR_SIZE))
		    || ((sattr.mask & ATTR_OWNER)
			&& (op_ctx->creds->caller_uid != sattr.owner))
		    || ((sattr.mask & ATTR_GROUP)
			&& (op_ctx->creds->caller_gid != sattr.group))) {
			/* A call to cache_inode_setattr is required */
			cache_status = cache_inode_setattr(file_entry,
							   &sattr,
							   false);

			if (cache_status != CACHE_INODE_SUCCESS)
				goto out_fail;
		}
	}

	/* Build file handle */
	res->res_create3.status =
	    nfs3_AllocateFH(&res->res_create3.CREATE3res_u.resok.obj.
			    post_op_fh3_u.handle);

	if (res->res_create3.status != NFS3_OK) {
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	if (!nfs3_FSALToFhandle(
	     &(res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle),
	     file_entry->obj_handle,
	     op_ctx->export)) {
		gsh_free(res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.
			 handle.data.data_val);

		res->res_create3.status = NFS3ERR_BADHANDLE;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set Post Op Fh3 structure */
	res->res_create3.CREATE3res_u.resok.obj.handle_follows = TRUE;

	/* Build entry attributes */
	nfs_SetPostOpAttr(file_entry,
			  &res->res_create3.CREATE3res_u.resok.obj_attributes);

	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_create3.CREATE3res_u.resok.dir_wcc);

	res->res_create3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (file_entry)
		cache_inode_put(file_entry);

	if (parent_entry)
		cache_inode_put(parent_entry);

	return rc;

 out_fail:
	if (nfs_RetryableError(cache_status)) {
		rc = NFS_REQ_DROP;
	} else {
		res->res_create3.status = nfs3_Errno(cache_status);

		nfs_SetWccData(&pre_parent, parent_entry,
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
	if ((res->res_create3.status == NFS3_OK)
	    && (res->res_create3.CREATE3res_u.resok.obj.handle_follows)) {
		gsh_free(res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.
			 handle.data.data_val);
	}
}
