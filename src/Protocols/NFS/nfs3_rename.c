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
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
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

int nfs3_rename(nfs_arg_t *arg,
		nfs_worker_data_t *worker,
		struct svc_req *req, nfs_res_t *res)
{
	const char *entry_name = arg->arg_rename3.from.name;
	const char *new_entry_name = arg->arg_rename3.to.name;
	cache_entry_t *parent_entry = NULL;
	cache_entry_t *new_parent_entry = NULL;
	cache_inode_status_t cache_status;
	short to_exportid = 0;
	short from_exportid = 0;
	int rc = NFS_REQ_OK;

	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	pre_op_attr pre_new_parent = {
		.attributes_follow = false
	};

	if (isDebug(COMPONENT_NFSPROTO)) {
		char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_vers,
				 &arg->arg_rename3.from.dir,
				 NULL,
				 strfrom);

		nfs_FhandleToStr(req->rq_vers,
				 &arg->arg_rename3.to.dir,
				 NULL,
				 strto);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Rename from "
			 "handle: %s name %s to handle: %s name: %s", strfrom,
			 entry_name, strto, new_entry_name);
	}

	/* to avoid setting it on each error case */
	res->res_rename3.RENAME3res_u.resfail.fromdir_wcc.before.
	    attributes_follow = FALSE;
	res->res_rename3.RENAME3res_u.resfail.fromdir_wcc.after.
	    attributes_follow = FALSE;
	res->res_rename3.RENAME3res_u.resfail.todir_wcc.before.
	    attributes_follow = FALSE;
	res->res_rename3.RENAME3res_u.resfail.todir_wcc.after.
	    attributes_follow = FALSE;

	/* Get the exportids for the two handles. */
	to_exportid = nfs3_FhandleToExportId(&(arg->arg_rename3.to.dir));
	from_exportid = nfs3_FhandleToExportId(&(arg->arg_rename3.from.dir));

	/* Validate the to_exportid */
	if (to_exportid < 0 || from_exportid < 0) {
		LogInfo(COMPONENT_DISPATCH,
			"NFS%d RENAME Request from client %s has badly formed handle for to dir",
			req->rq_vers,
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

	/* Convert fromdir file handle into a cache_entry */
	parent_entry = nfs3_FhandleToCache(&arg->arg_rename3.from.dir,
					   &res->res_create3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_entry, &pre_parent);

	/* Convert todir file handle into a cache_entry */
	new_parent_entry = nfs3_FhandleToCache(&arg->arg_rename3.to.dir,
					       &res->res_create3.status,
					       &rc);

	if (new_parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(new_parent_entry, &pre_new_parent);

	if (entry_name == NULL || *entry_name == '\0' || new_entry_name == NULL
	    || *new_entry_name == '\0') {
		cache_status = CACHE_INODE_INVALID_ARGUMENT;
		goto out_fail;
	}

	cache_status = cache_inode_rename(parent_entry,
					  entry_name,
					  new_parent_entry,
					  new_entry_name);

	if (cache_status != CACHE_INODE_SUCCESS)
		goto out_fail;

	res->res_rename3.status = NFS3_OK;

	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_rename3.RENAME3res_u.resok.fromdir_wcc);

	nfs_SetWccData(&pre_new_parent, new_parent_entry,
		       &res->res_rename3.RENAME3res_u.resok.todir_wcc);

	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_rename3.status = nfs3_Errno(cache_status);

	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_rename3.RENAME3res_u.resfail.fromdir_wcc);

	nfs_SetWccData(&pre_new_parent, new_parent_entry,
		       &res->res_rename3.RENAME3res_u.resfail.todir_wcc);

	/* If we are here, there was an error */
	if (nfs_RetryableError(cache_status))
		rc = NFS_REQ_DROP;

 out:
	if (parent_entry)
		cache_inode_put(parent_entry);

	if (new_parent_entry)
		cache_inode_put(new_parent_entry);

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
	return;
}
