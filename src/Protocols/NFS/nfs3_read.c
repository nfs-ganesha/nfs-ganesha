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
 * @file  nfs3_read.c
 * @brief Everything you need to read.
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
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "server_stats.h"
#include "export_mgr.h"

static void nfs_read_ok(struct svc_req *req,
			nfs_res_t *res,
			char *data, uint32_t read_size, cache_entry_t *entry,
			int eof)
{
	if ((read_size == 0) && (data != NULL)) {
		gsh_free(data);
		data = NULL;
	}

	/* Build Post Op Attributes */
	nfs_SetPostOpAttr(entry,
			  &res->res_read3.READ3res_u.resok.file_attributes);

	res->res_read3.READ3res_u.resok.eof = eof;
	res->res_read3.READ3res_u.resok.count = read_size;
	res->res_read3.READ3res_u.resok.data.data_val = data;
	res->res_read3.READ3res_u.resok.data.data_len = read_size;

	res->res_read3.status = NFS3_OK;
}

/**
 *
 * @brief The NFSPROC3_READ
 *
 * Implements the NFSPROC3_READ function.
 *
 * @param[in]  arg     NFS arguments union
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

int nfs3_read(nfs_arg_t *arg,
	      nfs_worker_data_t *worker,
	      struct svc_req *req, nfs_res_t *res)
{
	cache_entry_t *entry;
	pre_op_attr pre_attr;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	size_t size = 0;
	size_t read_size = 0;
	uint64_t offset = 0;
	void *data = NULL;
	bool eof_met = false;
	int rc = NFS_REQ_OK;
	bool sync = false;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		offset = arg->arg_read3.offset;
		size = arg->arg_read3.count;

		nfs_FhandleToStr(req->rq_vers, &arg->arg_read3.file, NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Read handle: %s "
			 "start: %" PRIu64 " len: %" PRIu64, str, offset, size);
	}

	/* to avoid setting it on each error case */
	res->res_read3.READ3res_u.resfail.file_attributes.attributes_follow =
	    FALSE;
	/* initialize for read of size 0 */
	res->res_read3.READ3res_u.resok.eof = FALSE;
	res->res_read3.READ3res_u.resok.count = 0;
	res->res_read3.READ3res_u.resok.data.data_val = NULL;
	res->res_read3.READ3res_u.resok.data.data_len = 0;
	res->res_read3.status = NFS3_OK;
	entry = nfs3_FhandleToCache(&arg->arg_read3.file,
				    &res->res_read3.status, &rc);

	if (entry == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(entry, &pre_attr);

	/** @todo this is racy, use cache_inode_lock_trust_attrs and
	 *        cache_inode_access_no_mutex
	 */
	if (entry->obj_handle->attributes.owner != op_ctx->creds->caller_uid) {
		cache_status =
		    cache_inode_access(entry, FSAL_READ_ACCESS);

		if (cache_status == CACHE_INODE_FSAL_EACCESS) {
			/* Test for execute permission */
			cache_status =
			    cache_inode_access(entry,
					       FSAL_MODE_MASK_SET(FSAL_X_OK) |
					       FSAL_ACE4_MASK_SET
					       (FSAL_ACE_PERM_EXECUTE));
		}

		if (cache_status != CACHE_INODE_SUCCESS) {
			res->res_read3.status = nfs3_Errno(cache_status);
			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* Sanity check: read only from a regular file */
	if (entry->type != REGULAR_FILE) {
		if (entry->type == DIRECTORY)
			res->res_read3.status = NFS3ERR_ISDIR;
		else
			res->res_read3.status = NFS3ERR_INVAL;

		rc = NFS_REQ_OK;
		goto out;
	}

	/* Extract the argument from the request */
	offset = arg->arg_read3.offset;
	size = arg->arg_read3.count;

	/* do not exceed maxium READ offset if set */
	if (op_ctx->export->MaxOffsetRead < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Read offset=%" PRIu64 " count=%zd "
			     "MaxOffSet=%" PRIu64, offset, size,
			     op_ctx->export->MaxOffsetRead);

		if ((offset + size) > op_ctx->export->MaxOffsetRead) {
			LogEvent(COMPONENT_NFSPROTO,
				 "A client tryed to violate max "
				 "file size %" PRIu64 " for exportid #%hu",
				 op_ctx->export->MaxOffsetRead,
				 op_ctx->export->export_id);

			res->res_read3.status = NFS3ERR_INVAL;

			nfs_SetPostOpAttr(entry,
					  &res->res_read3.READ3res_u.resfail.
					  file_attributes);

			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* We should not exceed the FSINFO rtmax field for the size */
	if (size > op_ctx->export->MaxRead) {
		/* The client asked for too much, normally this should
		   not happen because the client is calling nfs_Fsinfo
		   at mount time and so is aware of the server maximum
		   write size */
		size = op_ctx->export->MaxRead;
	}

	if (size == 0) {
		nfs_read_ok(req, res, NULL, 0, entry, 0);
		rc = NFS_REQ_OK;
		goto out;
	} else {
		data = gsh_malloc(size);
		if (data == NULL) {
			rc = NFS_REQ_DROP;
			goto out;
		}

		cache_status = cache_inode_rdwr(entry,
						CACHE_INODE_READ,
						offset,
						size,
						&read_size,
						data,
						&eof_met,
						&sync);

		if (cache_status == CACHE_INODE_SUCCESS) {
			nfs_read_ok(req, res, data, read_size,
				    entry, eof_met);
			rc = NFS_REQ_OK;
			goto out;
		}
		gsh_free(data);
	}

	/* If we are here, there was an error */
	if (nfs_RetryableError(cache_status)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_read3.status = nfs3_Errno(cache_status);

	nfs_SetPostOpAttr(entry,
			  &res->res_read3.READ3res_u.resfail.file_attributes);

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (entry)
		cache_inode_put(entry);

	server_stats_io_done(size, read_size,
			     (rc == NFS_REQ_OK) ? true : false,
			     false);
	return rc;
}				/* nfs3_read */

/**
 * @brief Free the result structure allocated for nfs3_read.
 *
 * This function frees the result structure allocated for nfs3_read.
 *
 * @param[in,out] res Result structure
 */
void nfs3_read_free(nfs_res_t *res)
{
	if ((res->res_read3.status == NFS3_OK)
	    && (res->res_read3.READ3res_u.resok.data.data_len != 0)) {
		gsh_free(res->res_read3.READ3res_u.resok.data.data_val);
	}
}
