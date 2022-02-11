// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright ZTE Corporation, 2020
 * Author: Muyao Luo (luo.muyao@zte.com.cn)
 *
 * --------------------------
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
#include "mount.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"
#include "nfs_proto_data.h"
#include "nfsacl.h"


/**
 * @brief The NFSACL getacl function.
 *
 * @param[in]  arg     NFS GETACL arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */
int nfsacl_getacl(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
#ifdef USE_NFSACL3
	struct fsal_obj_handle *obj = NULL;
	int rc = NFS_REQ_OK;
	struct fsal_attrlist *attrs;
	fsal_status_t status;
	acl_t acl = NULL;
	acl_t d_acl = NULL;
	posix_acl *encode_acl = NULL;
	posix_acl *encode_df_acl = NULL;

	attrs = &res->res_getacl.getaclres_u.resok.attr.attr3_u.obj_attributes;

	LogNFSACL_Operation(COMPONENT_NFSPROTO, req, &arg->arg_getacl.fhandle,
			  "");

	fsal_prepare_attrs(attrs, ATTRS_NFS3_ACL);

	obj = nfs3_FhandleToCache(&arg->arg_getacl.fhandle,
					&res->res_getacl.status,
					&rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		LogFullDebug(COMPONENT_NFSPROTO,
				 "nfs_Getacl returning %d",
				 rc);
		goto out;
	}

	/*Get fsal attr&acl*/
	status = obj->obj_ops->getattrs(obj, attrs);

	if (FSAL_IS_ERROR(status)) {
		res->res_getacl.status = nfs3_Errno_status(status);

		LogFullDebug(COMPONENT_NFSPROTO,
			 "nfsacl_Getacl set failed status v3");

		rc = NFS_REQ_OK;
		goto out;
	}

	/*Set attributes_follow*/
	res->res_getacl.getaclres_u.resok.attr.attributes_follow = TRUE;

	/* Set Mask*/
	if (arg->arg_getacl.mask &
		~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT)) {
		res->res_getacl.status = nfs3_Errno_status(status);

		LogFullDebug(COMPONENT_NFSPROTO,
			 "Invalid args");

		rc = NFS_REQ_OK;
		goto out;
	}
	res->res_getacl.getaclres_u.resok.mask = arg->arg_getacl.mask;

	/*Get access acl*/
	if (arg->arg_getacl.mask & (NFS_ACL|NFS_ACLCNT)) {
		acl = fsal_acl_2_posix_acl(attrs->acl, ACL_TYPE_ACCESS);
		if (acl == NULL) {
			LogFullDebug(COMPONENT_NFSPROTO,
				"Access ACL is NULL");
			res->res_getacl.getaclres_u.resok.acl_access = NULL;
			res->res_getacl.getaclres_u.resok.acl_access_count = 0;
		} else if (acl_valid(acl) != 0) {
			LogWarn(COMPONENT_FSAL,
					"failed to convert fsal acl to Access posix acl");
			status = fsalstat(ERR_FSAL_FAULT, 0);
			goto error;
		} else {
			encode_acl = encode_posix_acl(acl,
				ACL_TYPE_ACCESS, attrs);
			if (acl == NULL) {
				LogFullDebug(COMPONENT_NFSPROTO,
					"encode_posix_acl return NULL");
				status = fsalstat(ERR_FSAL_FAULT, 0);
				goto error;
			}
			res->res_getacl.getaclres_u.resok.acl_access =
				encode_acl;
			res->res_getacl.getaclres_u.resok.acl_access_count =
				encode_acl->count;
		}
	}

	/*Get default acl*/
	if (arg->arg_getacl.mask & (NFS_DFACL|NFS_DFACLCNT)) {
		d_acl = fsal_acl_2_posix_acl(attrs->acl, ACL_TYPE_DEFAULT);
		if (d_acl == NULL) {
			LogFullDebug(COMPONENT_NFSPROTO,
				"Default ACL is NULL");
			res->res_getacl.getaclres_u.resok.acl_default = NULL;
			res->res_getacl.getaclres_u.resok.acl_default_count = 0;
		} else if (acl_valid(d_acl) != 0) {
			LogWarn(COMPONENT_FSAL,
					"failed to convert fsal acl to Default posix acl");
			status = fsalstat(ERR_FSAL_FAULT, 0);
			goto error;
		} else {
			encode_df_acl = encode_posix_acl(d_acl,
				ACL_TYPE_DEFAULT, attrs);
			if (acl == NULL) {
				LogFullDebug(COMPONENT_NFSPROTO,
					"encode_posix_acl return NULL");
				status = fsalstat(ERR_FSAL_FAULT, 0);
				goto error;
			}
			res->res_getacl.getaclres_u.resok.acl_default =
				encode_df_acl;
			res->res_getacl.getaclres_u.resok.acl_default_count =
				encode_df_acl->count;
		}
	}

	nfs3_Fixup_FSALattr(obj, attrs);

	res->res_getacl.status = NFS3_OK;

	LogFullDebug(COMPONENT_NFSPROTO, "nfs_Getacl succeeded");
	rc = NFS_REQ_OK;

 out:

	/* Done with the attrs (NFSv3 doesn't use ANY of the reffed attributes)
	 */
	fsal_release_attrs(attrs);

	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	if (acl)
		acl_free(acl);
	if (d_acl)
		acl_free(d_acl);

	return rc;

 error:
	rc = NFS_REQ_OK;
	res->res_getacl.status = nfs3_Errno_status(status);
	goto out;
#else
	return 0;
#endif				/* USE_NFSACL3 */
}				/* nfsacl_getacl */

/**
 * @brief Free the result structure allocated for nfsacl_getacl
 *
 *
 * @param[in,out] res Pointer to the result structure.
 */
void nfsacl_getacl_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
