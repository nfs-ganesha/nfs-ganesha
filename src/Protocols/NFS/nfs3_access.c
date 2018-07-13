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
 * @file  nfs3_access.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"

/**
 * Implements NFSPROC3_ACCESS.
 *
 * This function implements NFSPROC3_ACCESS.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_access(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	fsal_errors_t fsal_errors;
	struct fsal_obj_handle *entry = NULL;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		sprint_fhandle3(str, &(arg->arg_access3.object));
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling NFS3_ACCESS handle: %s",
			 str);
	}

	/* to avoid setting it on each error case */
	res->res_access3.ACCESS3res_u.resfail.obj_attributes.attributes_follow =
	    FALSE;

	/* Convert file handle into a vnode */
	entry = nfs3_FhandleToCache(&(arg->arg_access3.object),
				    &(res->res_access3.status),
				    &rc);

	if (entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Perform the 'access' call */
	fsal_errors =
	    nfs_access_op(entry, arg->arg_access3.access,
			  &res->res_access3.ACCESS3res_u.resok.access, NULL);

	if (fsal_errors == ERR_FSAL_NO_ERROR ||
	    fsal_errors == ERR_FSAL_ACCESS) {
		/* Build Post Op Attributes */
		nfs_SetPostOpAttr(
			entry,
			&res->res_access3.ACCESS3res_u.resok.obj_attributes,
			NULL);

		res->res_access3.status = NFS3_OK;
		rc = NFS_REQ_OK;
		goto out;
	}

	/* If we are here, there was an error */
	if (nfs_RetryableError(fsal_errors)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_access3.status = nfs3_Errno(fsal_errors);
	nfs_SetPostOpAttr(entry,
			  &res->res_access3.ACCESS3res_u.resfail.obj_attributes,
			  NULL);
 out:

	if (entry)
		entry->obj_ops->put_ref(entry);

	return rc;
}				/* nfs3_access */

/**
 * @brief Free the result structure allocated for nfs3_access.
 *
 * this function frees the result structure allocated for nfs3_access.
 *
 * @param[in,out] res Result structure.
 *
 */
void nfs3_access_free(nfs_res_t *res)
{
	/* Nothing to do */
}
