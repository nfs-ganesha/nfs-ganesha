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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"

/**
 *  @brief Helper to verify validity of export ID's
 *
 *  @l3_arg  NFS argument
 *  @req     SVC request
 *
 *  @return nfsstat3
 */
static enum nfsstat3
nfs3_verify_exportid(struct LINK3args *l3_arg, struct svc_req *req)
{
	const short to_exportid = nfs3_FhandleToExportId(&l3_arg->link.dir);
	const short from_exportid = nfs3_FhandleToExportId(&l3_arg->file);

	if (to_exportid < 0 || from_exportid < 0) {
		LogInfo(COMPONENT_DISPATCH,
			"NFS%d LINK Request from client %s has badly formed handle for link dir",
			req->rq_msg.cb_vers,
			op_ctx->client ?
					op_ctx->client->hostaddr_str :
					"unknown client");
		return NFS3ERR_BADHANDLE;
	}

	/* Both objects have to be in the same filesystem */
	if (to_exportid != from_exportid)
		return NFS3ERR_XDEV;

	return NFS3_OK;
}

/**
 *
 * @brief The NFSPROC3_LINK
 *
 * The NFSPROC3_LINK.
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
int nfs3_link(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct LINK3args *l3_arg = &arg->arg_link3;
	struct LINK3res *l3_res = &res->res_link3;
	const char *link_name = l3_arg->link.name;
	struct fsal_obj_handle *target_obj = NULL;
	struct fsal_obj_handle *parent_obj = NULL;
	pre_op_attr pre_parent = {0};
	fsal_status_t fsal_status = {0, 0};
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers, &l3_arg->file,
				 NULL, strfrom);
		nfs_FhandleToStr(req->rq_msg.cb_vers, &l3_arg->link.dir,
				 NULL, strto);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling NFS3_LINK handle: %s to handle: %s name: %s",
			 strfrom, strto, link_name);
	}

	/* to avoid setting it on each error case */
	l3_res->LINK3res_u.resfail.file_attributes.attributes_follow = FALSE;
	l3_res->LINK3res_u.resfail.linkdir_wcc.before.attributes_follow = FALSE;
	l3_res->LINK3res_u.resfail.linkdir_wcc.after.attributes_follow = FALSE;

	l3_res->status = nfs3_verify_exportid(l3_arg, req);
	if (l3_res->status != NFS3_OK)
		return rc;

	parent_obj = nfs3_FhandleToCache(&l3_arg->link.dir, &l3_res->status,
					 &rc);
	if (parent_obj == NULL)
		return rc;  /* Status and rc are set by nfs3_FhandleToCache */

	nfs_SetPreOpAttr(parent_obj, &pre_parent);

	target_obj = nfs3_FhandleToCache(&l3_arg->file, &l3_res->status, &rc);
	if (target_obj == NULL) {
		parent_obj->obj_ops->put_ref(parent_obj);
		return rc;  /* Status and rc are set by nfs3_FhandleToCache */
	}

	if (parent_obj->type != DIRECTORY) {
		l3_res->status = NFS3ERR_NOTDIR;
		goto out;
	}

	if (link_name == NULL || *link_name == '\0') {
		l3_res->status = NFS3ERR_INVAL;
		goto out;
	}

	fsal_status = fsal_link(target_obj, parent_obj, link_name);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* If we are here, there was an error */
		LogFullDebug(COMPONENT_NFSPROTO,
			     "failed link: fsal_status=%s",
			     fsal_err_txt(fsal_status));

		if (nfs_RetryableError(fsal_status.major)) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		l3_res->status = nfs3_Errno_status(fsal_status);
		nfs_SetPostOpAttr(target_obj,
				  &l3_res->LINK3res_u.resfail.file_attributes,
				  NULL);

		nfs_SetWccData(&pre_parent, parent_obj,
			       &l3_res->LINK3res_u.resfail.linkdir_wcc);
	} else {
		nfs_SetPostOpAttr(target_obj,
				  &l3_res->LINK3res_u.resok.file_attributes,
				  NULL);

		nfs_SetWccData(&pre_parent, parent_obj,
			       &l3_res->LINK3res_u.resok.linkdir_wcc);
		l3_res->status = NFS3_OK;
	}

 out:
	/* return references */
	target_obj->obj_ops->put_ref(target_obj);
	parent_obj->obj_ops->put_ref(parent_obj);

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
	/* Nothing to do here */
}
