// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright ZTE Corporation, 2019
 *  Author: Muyao Luo (luo.muyao@zte.com.cn)
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
#include "nfs_core.h"
#include "nfs_exports.h"
#include "mount.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "sal_functions.h"
#include "nfsacl.h"

/**
 * @brief The NFSACL v3 setacl function, for all versions.
 *
 * @param[in]  arg     NFS SETACL arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */
int nfsacl_setacl(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
#ifdef USE_NFSACL3
	struct fsal_attrlist setacl;
	struct fsal_attrlist *attrs;
	struct fsal_obj_handle *obj = NULL;
	fsal_status_t fsal_status = {0, 0};
	int rc = NFS_REQ_OK;
	int ret = 0;
	bool is_dir = FALSE;

	/* to avoid setting it on each error case */
	res->res_setacl.setaclres_u.resok.attr.attributes_follow = FALSE;

	attrs = &res->res_setacl.setaclres_u.resok.attr.attr3_u.obj_attributes;

	memset(&setacl, 0, sizeof(setacl));

	LogNFSACL_Operation(COMPONENT_NFSPROTO, req, &arg->arg_setacl.fhandle,
			  "");

	fsal_prepare_attrs(attrs, ATTRS_NFS3);

	obj = nfs3_FhandleToCache(&arg->arg_setacl.fhandle,
					&res->res_setacl.status,
					&rc);
	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		LogFullDebug(COMPONENT_NFSPROTO,
				 "nfs3_FhandleToCache failed");
		goto out;
	}

	/* Whether obj is a directory */
	if (obj->type == DIRECTORY)
		is_dir = TRUE;

	/* Conversion to FSAL ACL */
	ret = nfs3_acl_2_fsal_acl(&setacl,
				arg->arg_setacl.mask,
				arg->arg_setacl.acl_access,
				arg->arg_setacl.acl_default,
				is_dir);
	if (ret) {
		res->res_setacl.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		LogFullDebug(COMPONENT_FSAL,
				 "nfs3_acl_2_fsal_acl failed");
		goto out;
	}

	if (setacl.valid_mask != 0) {
		/* Don't allow attribute change while we are in grace period.
		 * Required for delegation reclaims and may be needed for other
		 * reclaimable states as well. No NFS4ERR_GRACE in NFS v3, so
		 * send jukebox error.
		 */
		if (!nfs_get_grace_status(false)) {
			res->res_setacl.status = NFS3ERR_JUKEBOX;
			rc = NFS_REQ_OK;
			LogFullDebug(COMPONENT_NFSPROTO,
					 "nfs_in_grace is true");
			goto out;
		}

		/* For now we don't look for states, so indicate bypass so
		 * we will get through an NLM_SHARE with deny.
		 */
		fsal_status = fsal_setattr(obj, true, NULL, &setacl);
		nfs_put_grace_status();

		if (FSAL_IS_ERROR(fsal_status)) {
			res->res_setacl.status =
				nfs3_Errno_status(fsal_status);
			LogFullDebug(COMPONENT_NFSPROTO,
					 "fsal_setacl failed");
			goto out_fail;
		}
	}

	/*Get fsal attr*/
	fsal_status = obj->obj_ops->getattrs(obj, attrs);
	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_setacl.status = nfs3_Errno_status(fsal_status);

		LogFullDebug(COMPONENT_NFSPROTO,
			 "nfsacl_Setacl get attr failed");

		rc = NFS_REQ_OK;
		goto out;
	}

	/* Set the NFS return */
	res->res_setacl.status = NFS3_OK;
	res->res_getacl.getaclres_u.resok.attr.attributes_follow = TRUE;
	LogFullDebug(COMPONENT_NFSPROTO,
		"nfsacl_Setacl set attributes_follow to TRUE");
	rc = NFS_REQ_OK;

 out:

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&setacl);

	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	LogDebug(COMPONENT_NFSPROTO,
		 "Set acl Result %s%s",
		 nfsstat3_to_str(res->res_setacl.status),
		 rc == NFS_REQ_DROP ? " Dropping response" : "");
	return rc;

 out_fail:

	if (nfs_RetryableError(fsal_status.major)) {
		/* Drop retryable request. */
		rc = NFS_REQ_DROP;
	}

	goto out;
#else
	return 0;
#endif				/* USE_NFSACL3 */

}

/**
 * @brief Frees the result structure allocated for nfsacl_setacl
 *
 * @param[in,out] res Pointer to the result structure.
 *
 */
void nfsacl_setacl_Free(nfs_res_t *res)
{
	/* Nothing to do */
}

