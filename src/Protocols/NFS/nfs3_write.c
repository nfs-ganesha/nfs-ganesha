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
 * @file  nfs3_write.c
 * @brief Everything you need for NFSv3 WRITE.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
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
#include "server_stats.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFSPROC3_WRITE
 *
 * Implements the NFSPROC3_WRITE function.
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

int nfs3_write(nfs_arg_t *arg,
	       nfs_worker_data_t *worker,
	       struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *entry;
	pre_op_attr pre_attr = {
		.attributes_follow = false
	};
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	size_t size = 0;
	size_t written_size = 0;
	uint64_t offset = 0;
	void *data = NULL;
	bool eof_met = false;
	bool sync = false;
	int rc = NFS_REQ_OK;
	fsal_status_t fsal_status;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR], *stables = "";

		offset = arg->arg_write3.offset;
		size = arg->arg_write3.count;

		switch (arg->arg_write3.stable) {
		case UNSTABLE:
			stables = "UNSTABLE";
			break;
		case DATA_SYNC:
			stables = "DATA_SYNC";
			sync = true;
			break;
		case FILE_SYNC:
			stables = "FILE_SYNC";
			sync = true;
			break;
		}

		nfs_FhandleToStr(req->rq_vers,
				 &arg->arg_write3.file,
				 NULL,
				 str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Write handle: %s "
			 "start: %" PRIx64 " len: %" PRIx64 " %s", str, offset,
			 size, stables);
	}

	/* to avoid setting it on each error case */
	res->res_write3.WRITE3res_u.resfail.file_wcc.before.attributes_follow =
	    false;
	res->res_write3.WRITE3res_u.resfail.file_wcc.after.attributes_follow =
	    false;

	entry = nfs3_FhandleToCache(&arg->arg_write3.file,
				    &res->res_write3.status,
				    &rc);

	if (entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(entry, &pre_attr);

	/** @todo this is racy, use cache_inode_lock_trust_attrs and
	 *        cache_inode_access_no_mutex
	 */
	if (entry->obj_handle->attributes.owner != op_ctx->creds->caller_uid) {
		cache_status = cache_inode_access(entry,
						  FSAL_WRITE_ACCESS);

		if (cache_status != CACHE_INODE_SUCCESS) {
			res->res_write3.status = nfs3_Errno(cache_status);
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* Sanity check: write only a regular file */
	if (entry->type != REGULAR_FILE) {
		if (entry->type == DIRECTORY)
			res->res_write3.status = NFS3ERR_ISDIR;
		else
			res->res_write3.status = NFS3ERR_INVAL;

		rc = NFS_REQ_OK;
		goto out;
	}

	/* if quota support is active, then we should check is the
	   FSAL allows inode creation or not */
	fsal_status =
	    op_ctx->fsal_export->ops->check_quota(op_ctx->fsal_export,
						   op_ctx->export->fullpath,
						   FSAL_QUOTA_BLOCKS);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_write3.status = NFS3ERR_DQUOT;
		rc = NFS_REQ_OK;
		goto out;
	}

	offset = arg->arg_write3.offset;
	size = arg->arg_write3.count;

	if (size > arg->arg_write3.data.data_len) {
		/* should never happen */
		res->res_write3.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		goto out;
	}

	data = arg->arg_write3.data.data_val;

	/* Do not exceed maxium WRITE offset if set */
	if (op_ctx->export->MaxOffsetWrite < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Write offset=%" PRIu64 " count=%" PRIu64
			     " MaxOffSet=%" PRIu64, offset, size,
			     op_ctx->export->MaxOffsetWrite);

		if ((offset + size) > op_ctx->export->MaxOffsetWrite) {
			LogEvent(COMPONENT_NFSPROTO,
				 "A client tryed to violate max "
				 "file size %" PRIu64 " for exportid #%hu",
				 op_ctx->export->MaxOffsetWrite,
				 op_ctx->export->export_id);

			res->res_write3.status = NFS3ERR_INVAL;

			res->res_write3.status = nfs3_Errno(cache_status);

			nfs_SetWccData(NULL, entry,
				       &res->res_write3.WRITE3res_u.resfail.
				       file_wcc);

			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* We should take care not to exceed FSINFO wtmax field for the size */
	if (size > op_ctx->export->MaxWrite) {
		/* The client asked for too much data, we must restrict him */
		size = op_ctx->export->MaxWrite;
	}

	if (size == 0) {
		cache_status = CACHE_INODE_SUCCESS;
		written_size = 0;
	} else {
		/* An actual write is to be made, prepare it */
		cache_status =
		    cache_inode_rdwr(entry, CACHE_INODE_WRITE, offset, size,
				     &written_size, data, &eof_met, &sync);
		if (cache_status == CACHE_INODE_SUCCESS) {
			/* Build Weak Cache Coherency data */
			nfs_SetWccData(NULL, entry,
				       &res->res_write3.WRITE3res_u.resok.
				       file_wcc);

			/* Set the written size */
			res->res_write3.WRITE3res_u.resok.count = written_size;

			/* How do we commit data ? */
			if (sync)
				res->res_write3.WRITE3res_u.resok.committed =
				    FILE_SYNC;
			else
				res->res_write3.WRITE3res_u.resok.committed =
				    UNSTABLE;

			/* Set the write verifier */
			memcpy(res->res_write3.WRITE3res_u.resok.verf,
			       NFS3_write_verifier,
			       sizeof(writeverf3));

			res->res_write3.status = NFS3_OK;

			rc = NFS_REQ_OK;
			goto out;
		}
	}

	LogFullDebug(COMPONENT_NFSPROTO,
		     "failed write: cache_status=%s",
		     cache_inode_err_str(cache_status));

	/* If we are here, there was an error */
	if (nfs_RetryableError(cache_status)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_write3.status = nfs3_Errno(cache_status);

	nfs_SetWccData(NULL, entry,
		       &res->res_write3.WRITE3res_u.resfail.file_wcc);

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (entry)
		cache_inode_put(entry);

	server_stats_io_done(size, written_size,
			     (rc == NFS_REQ_OK) ? true : false,
			     true);
	return rc;

}				/* nfs3_write */

/**
 * @brief Frees the result structure allocated for nfs3_write.
 *
 * Frees the result structure allocated for nfs3_write.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_write_free(nfs_res_t *res)
{
	return;
}
