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
 * @file  nfs3_getattr.c
 * @brief Implements the NFSv3 GETATTR proc
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
 * @brief Get attributes for a file
 *
 * Get attributes for a file. Implements NFSPROC3_GETATTR.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int nfs3_getattr(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj = NULL;
	int rc = NFS_REQ_OK;
	struct attrlist attrs;
	fsal_status_t status;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &(arg->arg_getattr3.object),
				 NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling NFS3_GETATTR handle: %s",
			 str);
	}

	fsal_prepare_attrs(&attrs, ATTRS_NFS3);

	obj = nfs3_FhandleToCache(&arg->arg_getattr3.object,
				    &res->res_getattr3.status,
				    &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Getattr returning %d",
			     rc);
		goto out;
	}

	status = obj->obj_ops->getattrs(obj, &attrs);

	if (FSAL_IS_ERROR(status)) {
		res->res_getattr3.status = nfs3_Errno_status(status);

		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Getattr set failed status v3");

		rc = NFS_REQ_OK;
		goto out;
	}

	nfs3_FSALattr_To_Fattr(
			obj, &attrs,
			&res->res_getattr3.GETATTR3res_u.resok.obj_attributes);

	res->res_getattr3.status = NFS3_OK;

	LogFullDebug(COMPONENT_NFSPROTO, "nfs_Getattr succeeded");
	rc = NFS_REQ_OK;

 out:

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	return rc;

}

/**
 * @brief Free the result structure allocated for nfs3_getattr.
 *
 * @param[in,out] resp Result structure
 *
 */
void nfs3_getattr_free(nfs_res_t *resp)
{
	/* Nothing to do here */
}
