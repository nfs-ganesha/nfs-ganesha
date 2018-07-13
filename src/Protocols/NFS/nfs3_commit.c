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
 * file    nfs3_commit.c
 * brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_commit.c : Routines used for managing the NFS4 COMPOUND functions.
 *
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
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"

/**
 * @brief Implements NFSPROC3_COMMIT
 *
 * Implements NFSPROC3_COMMIT.
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

int nfs3_commit(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj = NULL;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		sprint_fhandle3(str, &(arg->arg_commit3.file));
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling NFS3_COMMIT handle: %s",
			 str);
	}

	/* To avoid setting it on each error case */
	res->res_commit3.COMMIT3res_u.resfail.file_wcc.before.attributes_follow
		= FALSE;
	res->res_commit3.COMMIT3res_u.resfail.file_wcc.after.attributes_follow
		= FALSE;

	obj = nfs3_FhandleToCache(&arg->arg_commit3.file,
				    &res->res_commit3.status,
				    &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	fsal_status = fsal_commit(obj, arg->arg_commit3.offset,
				  arg->arg_commit3.count);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_commit3.status = nfs3_Errno_status(fsal_status);

		nfs_SetWccData(NULL, obj,
			       &res->res_commit3.COMMIT3res_u.resfail.file_wcc);

		rc = NFS_REQ_OK;
		goto out;
	}

	nfs_SetWccData(NULL, obj,
		       &(res->res_commit3.COMMIT3res_u.resok.file_wcc));

	/* Set the write verifier */
	memcpy(res->res_commit3.COMMIT3res_u.resok.verf, NFS3_write_verifier,
	       sizeof(writeverf3));
	res->res_commit3.status = NFS3_OK;

 out:

	if (obj)
		obj->obj_ops->put_ref(obj);

	return rc;
}				/* nfs3_commit */

/**
 * @brief Free the result structure allocated for nfs3_commit.
 *
 * This function frees the result structure allocated for nfs3_commit.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_commit_free(nfs_res_t *res)
{
	/* Nothing to do here */
}
