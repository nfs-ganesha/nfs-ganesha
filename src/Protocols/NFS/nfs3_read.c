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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "server_stats.h"
#include "export_mgr.h"
#include "sal_functions.h"

static void nfs_read_ok(struct svc_req *req, nfs_res_t *res, char *data,
			uint32_t read_size, struct fsal_obj_handle *obj,
			int eof)
{
	if ((read_size == 0) && (data != NULL)) {
		gsh_free(data);
		data = NULL;
	}

	/* Build Post Op Attributes */
	nfs_SetPostOpAttr(obj,
			  &res->res_read3.READ3res_u.resok.file_attributes,
			  NULL);

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
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_read(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj;
	pre_op_attr pre_attr;
	fsal_status_t fsal_status = {0, 0};
	size_t size = 0;
	size_t read_size = 0;
	uint64_t offset = 0;
	void *data = NULL;
	bool eof_met = false;
	int rc = NFS_REQ_OK;
	uint64_t MaxRead = atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxRead);
	uint64_t MaxOffsetRead =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetRead);
	READ3resfail *resfail = &res->res_read3.READ3res_u.resfail;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		offset = arg->arg_read3.offset;
		size = arg->arg_read3.count;

		nfs_FhandleToStr(req->rq_msg.cb_vers, &arg->arg_read3.file,
				 NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Read handle: %s start: %"
			 PRIu64 " len: %zu",
			 str, offset, size);
	}

	/* to avoid setting it on each error case */
	resfail->file_attributes.attributes_follow =  FALSE;

	/* initialize for read of size 0 */
	res->res_read3.READ3res_u.resok.eof = FALSE;
	res->res_read3.READ3res_u.resok.count = 0;
	res->res_read3.READ3res_u.resok.data.data_val = NULL;
	res->res_read3.READ3res_u.resok.data.data_len = 0;
	res->res_read3.status = NFS3_OK;
	obj = nfs3_FhandleToCache(&arg->arg_read3.file,
				    &res->res_read3.status, &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		goto out;
	}

	nfs_SetPreOpAttr(obj, &pre_attr);

	fsal_status =
	    obj->obj_ops->test_access(obj, FSAL_READ_ACCESS, NULL, NULL, true);

	if (fsal_status.major == ERR_FSAL_ACCESS) {
		/* Test for execute permission */
		fsal_status = fsal_access(obj,
				       FSAL_MODE_MASK_SET(FSAL_X_OK) |
				       FSAL_ACE4_MASK_SET
				       (FSAL_ACE_PERM_EXECUTE));
	}

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_read3.status = nfs3_Errno_status(fsal_status);
		rc = NFS_REQ_OK;
		goto out;
	}

	/* Sanity check: read only from a regular file */
	if (obj->type != REGULAR_FILE) {
		if (obj->type == DIRECTORY)
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
	if (MaxOffsetRead < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Read offset=%" PRIu64
			     " count=%zd MaxOffSet=%" PRIu64,
			     offset, size, MaxOffsetRead);

		if ((offset + size) > MaxOffsetRead) {
			LogEvent(COMPONENT_NFSPROTO,
				 "A client tryed to violate max file size %"
				 PRIu64 " for exportid #%hu",
				 MaxOffsetRead,
				 op_ctx->ctx_export->export_id);

			res->res_read3.status = NFS3ERR_FBIG;

			nfs_SetPostOpAttr(obj, &resfail->file_attributes, NULL);

			rc = NFS_REQ_OK;
			goto out;
		}
	}

	/* We should not exceed the FSINFO rtmax field for the size */
	if (size > MaxRead) {
		/* The client asked for too much, normally this should
		   not happen because the client is calling nfs_Fsinfo
		   at mount time and so is aware of the server maximum
		   write size */
		size = MaxRead;
	}

	if (size == 0) {
		nfs_read_ok(req, res, NULL, 0, obj, 0);
		rc = NFS_REQ_OK;
		goto out;
	} else {
		data = gsh_malloc(size);

		/* Check for delegation conflict. */
		if (state_deleg_conflict(obj, false)) {
			res->res_read3.status = NFS3ERR_JUKEBOX;
			rc = NFS_REQ_OK;
			gsh_free(data);
			goto out;
		}

		/* Call the new fsal_read2 */
		/** @todo for now pass NULL state */
		fsal_status = fsal_read2(obj,
					  true,
					  NULL,
					  offset,
					  size,
					  &read_size,
					  data,
					  &eof_met,
					  NULL);

		if (!FSAL_IS_ERROR(fsal_status)) {
			nfs_read_ok(req, res, data, read_size, obj, eof_met);
			rc = NFS_REQ_OK;
			goto out;
		}
		gsh_free(data);
	}

	/* If we are here, there was an error */
	if (nfs_RetryableError(fsal_status.major)) {
		rc = NFS_REQ_DROP;
		goto out;
	}

	res->res_read3.status = nfs3_Errno_status(fsal_status);

	nfs_SetPostOpAttr(obj, &resfail->file_attributes, NULL);

	rc = NFS_REQ_OK;

 out:
	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

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
