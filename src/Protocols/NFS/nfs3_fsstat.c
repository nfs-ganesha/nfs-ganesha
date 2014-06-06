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
 * @file  nfs3_fsstat.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
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
 * @brief The NFSPROC3_FSSTAT
 *
 * Implements the NFSPROC3_FSSTAT.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_fsstat(nfs_arg_t *arg,
		nfs_worker_data_t *worker,
		struct svc_req *req, nfs_res_t *res)
{
	fsal_dynamicfsinfo_t dynamicinfo;
	cache_inode_status_t cache_status;
	cache_entry_t *entry = NULL;
	int rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];
		nfs_FhandleToStr(req->rq_vers, &(arg->arg_fsstat3.fsroot), NULL,
				 str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs3_fsstat handle: %s",
			 str);
	}

	/* to avoid setting it on each error case */
	res->res_fsstat3.FSSTAT3res_u.resfail.obj_attributes.attributes_follow =
	    FALSE;

	entry = nfs3_FhandleToCache(&arg->arg_fsstat3.fsroot,
				    &res->res_fsstat3.status,
				    &rc);

	if (entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	/* Get statistics and convert from cache */
	cache_status = cache_inode_statfs(entry,
					  &dynamicinfo);

	if (cache_status == CACHE_INODE_SUCCESS) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Fsstat --> dynamicinfo.total_bytes "
			     "= %zu dynamicinfo.free_bytes = %zu "
			     "dynamicinfo.avail_bytes = %zu",
			     dynamicinfo.total_bytes, dynamicinfo.free_bytes,
			     dynamicinfo.avail_bytes);
		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Fsstat --> " "dynamicinfo.total_files = %"
			     PRIu64 " dynamicinfo.free_files = %" PRIu64
			     " dynamicinfo.avail_files = %" PRIu64,
			     dynamicinfo.total_files, dynamicinfo.free_files,
			     dynamicinfo.avail_files);

		nfs_SetPostOpAttr(entry,
				  &(res->res_fsstat3.FSSTAT3res_u.resok.
				    obj_attributes));

		res->res_fsstat3.FSSTAT3res_u.resok.tbytes =
		    dynamicinfo.total_bytes;
		res->res_fsstat3.FSSTAT3res_u.resok.fbytes =
		    dynamicinfo.free_bytes;
		res->res_fsstat3.FSSTAT3res_u.resok.abytes =
		    dynamicinfo.avail_bytes;
		res->res_fsstat3.FSSTAT3res_u.resok.tfiles =
		    dynamicinfo.total_files;
		res->res_fsstat3.FSSTAT3res_u.resok.ffiles =
		    dynamicinfo.free_files;
		res->res_fsstat3.FSSTAT3res_u.resok.afiles =
		    dynamicinfo.avail_files;
		/* volatile FS */
		res->res_fsstat3.FSSTAT3res_u.resok.invarsec = 0;

		res->res_fsstat3.status = NFS3_OK;

		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Fsstat --> tbytes=%llu "
			     "fbytes=%llu abytes=%llu",
			     res->res_fsstat3.FSSTAT3res_u.resok.tbytes,
			     res->res_fsstat3.FSSTAT3res_u.resok.fbytes,
			     res->res_fsstat3.FSSTAT3res_u.resok.abytes);

		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs_Fsstat --> tfiles=%llu "
			     "fffiles=%llu afiles=%llu",
			     res->res_fsstat3.FSSTAT3res_u.resok.tfiles,
			     res->res_fsstat3.FSSTAT3res_u.resok.ffiles,
			     res->res_fsstat3.FSSTAT3res_u.resok.afiles);

		rc = NFS_REQ_OK;
		goto out;
	}

	/* At this point we met an error */
	if (nfs_RetryableError(cache_status)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_fsstat3.status = nfs3_Errno(cache_status);
	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (entry)
		cache_inode_put(entry);

	return rc;
}				/* nfs3_fsstat */

/**
 * @brief Free the result structure allocated for nfs3_fsstat
 *
 * This function frees the result structure allocated for nfs3_fsstat.
 *
 * @param[in] res Result structure
 *
 */
void nfs3_fsstat_free(nfs_res_t *res)
{
	/* Nothing to do here */
	return;
}
