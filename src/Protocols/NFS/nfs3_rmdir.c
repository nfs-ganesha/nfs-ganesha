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
 * @file  nfs3_rmdir.c
 * @brief Everything you need for NFSv3 RMDIR
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFSPROC3_RMDIR
 *
 * Implements the NFSPROC3_RMDIR function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_rmdir(nfs_arg_t *arg,
	       nfs_worker_data_t *worker,
	       struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *parent_entry = NULL;
	cache_entry_t *child_entry = NULL;
	pre_op_attr pre_parent = {
		.attributes_follow = false
	};
	cache_inode_status_t cache_status;
	const char *name = arg->arg_rmdir3.object.name;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_vers,
				 &arg->arg_rmdir3.object.dir,
				 NULL,
				 str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_rmdir handle: %s "
			 "name: %s", str, name);
	}

	/* Convert file handle into a pentry */
	/* to avoid setting it on each error case */
	res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.before.attributes_follow =
	    FALSE;
	res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.after.attributes_follow =
	    FALSE;

	parent_entry = nfs3_FhandleToCache(&arg->arg_rmdir3.object.dir,
					   &res->res_rmdir3.status,
					   &rc);

	if (parent_entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(parent_entry, &pre_parent);

	/* Sanity checks: directory name must be non-null; parent
	 * must be a directory.
	 */
	if (parent_entry->type != DIRECTORY) {
		res->res_rmdir3.status = NFS3ERR_NOTDIR;
		rc = NFS_REQ_OK;
		goto out;
	}

	if ((name == NULL) || (*name == '\0')) {
		cache_status = CACHE_INODE_INVALID_ARGUMENT;
		goto out_fail;
	}

	/* Lookup to the entry to be removed to check that it is a
	 * directory
	 */
	cache_status = cache_inode_lookup(parent_entry,
					  name,
					  &child_entry);

	if (child_entry != NULL) {
		/* Sanity check: make sure we are about to remove a
		 * directory
		 */
		if (child_entry->type != DIRECTORY) {
			res->res_rmdir3.status = NFS3ERR_NOTDIR;
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	cache_status = cache_inode_remove(parent_entry, name);

	if (cache_status != CACHE_INODE_SUCCESS)
		goto out_fail;

	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_rmdir3.RMDIR3res_u.resok.dir_wcc);

	res->res_rmdir3.status = NFS3_OK;

	rc = NFS_REQ_OK;

	goto out;

 out_fail:
	res->res_rmdir3.status = nfs3_Errno(cache_status);
	nfs_SetWccData(&pre_parent, parent_entry,
		       &res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc);

	/* If we are here, there was an error */
	if (nfs_RetryableError(cache_status))
		rc = NFS_REQ_DROP;

 out:
	/* return references */
	if (child_entry)
		cache_inode_put(child_entry);

	if (parent_entry)
		cache_inode_put(parent_entry);

	return rc;
}				/* nfs3_rmdir */

/**
 * @brief Free the result structure allocated for nfs3_rmdir
 *
 * This function frees the result structure allocated for nfs3_rmdir.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_rmdir_free(nfs_res_t *res)
{
	return;
}
