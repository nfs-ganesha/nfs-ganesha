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
 * @file  nfs3_lookup.c
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

/**
 *
 * @brief The NFS3_LOOKUP
 *
 * Implements the NFS3_LOOKUP function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_lookup(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj_dir = NULL;
	struct fsal_obj_handle *obj_file = NULL;
	fsal_status_t fsal_status;
	char *name = NULL;
	int rc = NFS_REQ_OK;
	struct fsal_attrlist attrs;
	LOOKUP3resfail *resfail = &res->res_lookup3.LOOKUP3res_u.resfail;
	LOOKUP3resok *resok = &res->res_lookup3.LOOKUP3res_u.resok;

	/* We have the option of not sending attributes, so set ATTR_RDATTR_ERR.
	 */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3 | ATTR_RDATTR_ERR);

	LogNFS3_Operation(COMPONENT_NFSPROTO, req, &arg->arg_lookup3.what.dir,
			  " name: %s", name);

	/* to avoid setting it on each error case */
	resfail->dir_attributes.attributes_follow = FALSE;

	obj_dir = nfs3_FhandleToCache(&arg->arg_lookup3.what.dir,
				      &res->res_lookup3.status,
				      &rc);

	if (obj_dir == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	name = arg->arg_lookup3.what.name;

	fsal_status = fsal_lookup(obj_dir, name, &obj_file, &attrs);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* If we are here, there was an error */
		if (nfs_RetryableError(fsal_status.major)) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		res->res_lookup3.status = nfs3_Errno_status(fsal_status);
		nfs_SetPostOpAttr(obj_dir, &resfail->dir_attributes, NULL);
	} else {
		/* Build FH */
		if (nfs3_FSALToFhandle(true, &resok->object, obj_file,
				       op_ctx->ctx_export)) {
			/* Build entry attributes */
			nfs_SetPostOpAttr(obj_file, &resok->obj_attributes,
					  &attrs);

			/* Build directory attributes */
			nfs_SetPostOpAttr(obj_dir, &resok->dir_attributes,
					  NULL);
			res->res_lookup3.status = NFS3_OK;
		} else {
			res->res_lookup3.status = NFS3ERR_BADHANDLE;
		}
	}

	rc = NFS_REQ_OK;

 out:

	/* Release the attributes. */
	fsal_release_attrs(&attrs);

	/* return references */
	if (obj_dir)
		obj_dir->obj_ops->put_ref(obj_dir);

	if (obj_file)
		obj_file->obj_ops->put_ref(obj_file);

	return rc;
}				/* nfs3_lookup */

/**
 * @brief Free the result structure allocated for nfs3_lookup.
 *
 * This function frees the result structure allocated for nfs3_lookup.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_lookup_free(nfs_res_t *res)
{
	if (res->res_lookup3.status == NFS3_OK) {
		gsh_free(
		    res->res_lookup3.LOOKUP3res_u.resok.object.data.data_val);
	}
}
