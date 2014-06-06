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

/**
 *
 * @brief The NFS3_LOOKUP
 *
 * Implements the NFS3_LOOKUP function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_lookup(nfs_arg_t *arg,
		nfs_worker_data_t *worker,
		struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *entry_dir = NULL;
	cache_entry_t *entry_file = NULL;
	cache_inode_status_t cache_status;
	char *name = NULL;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		name = arg->arg_lookup3.what.name;

		nfs_FhandleToStr(req->rq_vers, &(arg->arg_lookup3.what.dir),
				 NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Lookup handle: %s "
			 " name: %s", str, name);
	}

	/* to avoid setting it on each error case */
	res->res_lookup3.LOOKUP3res_u.resfail.dir_attributes.attributes_follow =
	    FALSE;

	entry_dir = nfs3_FhandleToCache(&arg->arg_lookup3.what.dir,
					&res->res_lookup3.status,
					&rc);

	if (entry_dir == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	name = arg->arg_lookup3.what.name;

	cache_status = cache_inode_lookup(entry_dir,
					  name,
					  &entry_file);

	if (entry_file && (cache_status == CACHE_INODE_SUCCESS)) {
		/* Build FH */
		res->res_lookup3.LOOKUP3res_u.resok.object.data.data_val =
		    gsh_malloc(sizeof(struct alloc_file_handle_v3));

		if (res->res_lookup3.LOOKUP3res_u.resok.object.data.data_val ==
		    NULL)
			res->res_lookup3.status = NFS3ERR_INVAL;
		else {
			if (nfs3_FSALToFhandle(
				    &res->res_lookup3.LOOKUP3res_u.resok.object,
				    entry_file->obj_handle,
				    op_ctx->export)) {
				/* Build entry attributes */
				nfs_SetPostOpAttr(entry_file,
						  &(res->res_lookup3.
						    LOOKUP3res_u.resok.
						    obj_attributes));

				/* Build directory attributes */
				nfs_SetPostOpAttr(entry_dir,
						  &(res->res_lookup3.
						    LOOKUP3res_u.resok.
						    dir_attributes));
				res->res_lookup3.status = NFS3_OK;
			}
		}
	} else {
		/* If we are here, there was an error */
		if (nfs_RetryableError(cache_status)) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		res->res_lookup3.status = nfs3_Errno(cache_status);
		nfs_SetPostOpAttr(entry_dir,
				  &res->res_lookup3.LOOKUP3res_u.resfail.
				  dir_attributes);
	}

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (entry_dir)
		cache_inode_put(entry_dir);

	if (entry_file)
		cache_inode_put(entry_file);

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
		gsh_free(res->res_lookup3.LOOKUP3res_u.resok.object.data.
			 data_val);
	}
}
