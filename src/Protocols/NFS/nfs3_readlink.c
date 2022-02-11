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
 * @file  nfs3_readlink.c
 * @brief Everything you need for NFSv3 READLINK.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFSPROC3_READLINK.
 *
 * This function implements the NFSPROC3_READLINK function.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @see fsal_readlink
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_readlink(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj = NULL;
	fsal_status_t fsal_status;
	struct gsh_buffdesc link_buffer = {
		.addr = NULL,
		.len = 0
	};
	int rc = NFS_REQ_OK;
	READLINK3resfail *resfail = &res->res_readlink3.READLINK3res_u.resfail;
	READLINK3resok *resok = &res->res_readlink3.READLINK3res_u.resok;

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_readlink3.symlink,
			  "");

	/* to avoid setting it on each error case */
	resfail->symlink_attributes.attributes_follow = false;

	obj = nfs3_FhandleToCache(&arg->arg_readlink3.symlink,
				  &res->res_readlink3.status,
				  &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Sanity Check: the obj must be a link */
	if (obj->type != SYMBOLIC_LINK) {
		res->res_readlink3.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		goto out;
	}

	fsal_status = fsal_readlink(obj, &link_buffer);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_readlink3.status = nfs3_Errno_status(fsal_status);
		nfs_SetPostOpAttr(obj, &resfail->symlink_attributes, NULL);

		if (nfs_RetryableError(fsal_status.major))
			rc = NFS_REQ_DROP;

		goto out;
	}

	/* Reply to the client */
	resok->data = link_buffer.addr;

	nfs_SetPostOpAttr(obj, &resok->symlink_attributes, NULL);
	res->res_readlink3.status = NFS3_OK;

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	return rc;
}				/* nfs3_readlink */

/**
 * @brief Free the result structure allocated for nfs3_readlink.
 *
 * This function frees the result structure allocated for
 * nfs3_readlink.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_readlink_free(nfs_res_t *res)
{
	if (res->res_readlink3.status == NFS3_OK)
		gsh_free(res->res_readlink3.READLINK3res_u.resok.data);
}
