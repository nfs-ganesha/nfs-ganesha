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
 * @file  nfs3_link.c
 * @brief everything that is needed to handle NFS PROC3 LINK.
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
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"

/**
 *
 * @brief The NFSPROC3_LINK
 *
 * The NFSPROC3_LINK.
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

int nfs3_link(nfs_arg_t *arg,
	      nfs_worker_data_t *worker,
	      struct svc_req *req, nfs_res_t *res)
{
	const char *link_name = arg->arg_link3.link.name;
	cache_entry_t *target_entry = NULL;
	cache_entry_t *parent_entry = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	short to_exportid = 0;
	short from_exportid = 0;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_vers,
				 &(arg->arg_link3.file),
				 NULL,
				 strfrom);

		nfs_FhandleToStr(req->rq_vers,
				 &(arg->arg_link3.link.dir),
				 NULL,
				 strto);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_link handle: %s to "
			 "handle: %s name: %s", strfrom, strto, link_name);
	}

	/* to avoid setting it on each error case */
	res->res_link3.LINK3res_u.resfail.file_attributes.attributes_follow =
	    FALSE;
	res->res_link3.LINK3res_u.resfail.linkdir_wcc.before.attributes_follow =
	    FALSE;
	res->res_link3.LINK3res_u.resfail.linkdir_wcc.after.attributes_follow =
	    FALSE;

	/* Get the exportids for the two handles. */
	to_exportid = nfs3_FhandleToExportId(&(arg->arg_link3.link.dir));
	from_exportid = nfs3_FhandleToExportId(&(arg->arg_link3.file));

	/* Validate the to_exportid */
	if (to_exportid < 0 || from_exportid < 0) {
		LogInfo(COMPONENT_DISPATCH,
			"NFS%d LINK Request from client %s has badly formed handle for link dir",
			req->rq_vers,
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client");

		/* Bad handle, report to client */
		res->res_link3.status = NFS3ERR_BADHANDLE;
		goto out;
	}

	/* Both objects have to be in the same filesystem */
	if (to_exportid != from_exportid) {
		res->res_link3.status = NFS3ERR_XDEV;
		goto out;
	}

	/* Get entry for parent directory */
	parent_entry = nfs3_FhandleToCache(&arg->arg_link3.link.dir,
					   &res->res_link3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_entry, &pre_parent);

	target_entry = nfs3_FhandleToCache(&arg->arg_link3.file,
					   &res->res_link3.status,
					   &rc);

	if (target_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Sanity checks: */
	if (parent_entry->type != DIRECTORY) {
		res->res_link3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	if (link_name == NULL || *link_name == '\0')
		res->res_link3.status = NFS3ERR_INVAL;
	else {
		/* Both objects have to be in the same filesystem */
		if (to_exportid != from_exportid)
			res->res_link3.status = NFS3ERR_XDEV;
		else {
			cache_status = cache_inode_link(target_entry,
							parent_entry,
							link_name);

			if (cache_status == CACHE_INODE_SUCCESS) {
				nfs_SetPostOpAttr(target_entry,
						  &(res->res_link3.LINK3res_u.
						    resok.file_attributes));

				nfs_SetWccData(&pre_parent,
					       parent_entry,
					       &(res->res_link3.LINK3res_u.
						 resok.linkdir_wcc));
				res->res_link3.status = NFS3_OK;
				rc = NFS_REQ_OK;
				goto out;
			}
		}		/* else */
	}

	/* If we are here, there was an error */
	if (nfs_RetryableError(cache_status)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_link3.status = nfs3_Errno(cache_status);
	nfs_SetPostOpAttr(target_entry,
			  &(res->res_link3.LINK3res_u.resfail.file_attributes));

	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_link3.LINK3res_u.resfail.linkdir_wcc);

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (target_entry)
		cache_inode_put(target_entry);

	if (parent_entry)
		cache_inode_put(parent_entry);

	return rc;

}				/* nfs3_link */

/**
 * @brief Free the result structure allocated for nfs3_link
 *
 * This function frees the result structure allocated for nfs3_link.
 *
 * @param[in,out] resp Result structure
 *
 */
void nfs3_link_free(nfs_res_t *resp)
{
	return;
}
