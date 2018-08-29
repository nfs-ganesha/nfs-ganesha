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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "server_stats.h"
#include "export_mgr.h"
#include "sal_functions.h"

struct nfs3_write_data {
	nfs_res_t *res;		/**< Results for write */
	int rc;			/**< Return code */
};

/**
 * @brief Callback for NFS3 write done
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] write_data	Data for write call
 * @param[in] caller_data	Data for caller
 */
static void nfs3_write_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			  void *write_data, void *caller_data)
{
	struct nfs3_write_data *data = caller_data;
	struct fsal_io_arg *write_arg = write_data;
	WRITE3resfail *resfail = &data->res->res_write3.WRITE3res_u.resfail;
	WRITE3resok *resok = &data->res->res_write3.WRITE3res_u.resok;

	/* Fixup ERR_FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	if (FSAL_IS_ERROR(ret)) {
		/* If we are here, there was an error */
		LogFullDebug(COMPONENT_NFSPROTO,
			     "failed write: fsal_status=%s",
			     fsal_err_txt(ret));

		if (nfs_RetryableError(ret.major)) {
			data->rc = NFS_REQ_DROP;
			goto out;
		}

		data->res->res_write3.status = nfs3_Errno_status(ret);

		nfs_SetWccData(NULL, obj, &resfail->file_wcc);

		data->rc = NFS_REQ_OK;
	} else {
		/* Build Weak Cache Coherency data */
		nfs_SetWccData(NULL, obj, &resok->file_wcc);

		/* Set the written size */
		resok->count = write_arg->io_amount;

		/* How do we commit data ? */
		if (write_arg->fsal_stable)
			resok->committed = FILE_SYNC;
		else
			resok->committed = UNSTABLE;

		/* Set the write verifier */
		memcpy(resok->verf, NFS3_write_verifier, sizeof(writeverf3));

		data->res->res_write3.status = NFS3_OK;
	}

	data->rc = NFS_REQ_OK;

out:
	/* return references */
	obj->obj_ops->put_ref(obj);

	server_stats_io_done(write_arg->iov[0].iov_len, write_arg->io_amount,
			     (data->rc == NFS_REQ_OK) ? true : false,
			     true);
}

/**
 *
 * @brief The NFSPROC3_WRITE
 *
 * Implements the NFSPROC3_WRITE function.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_write(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct fsal_obj_handle *obj;
	pre_op_attr pre_attr = {
		.attributes_follow = false
	};
	fsal_status_t fsal_status = {0, 0};
	size_t size = 0;
	uint64_t MaxWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxWrite);
	uint64_t MaxOffsetWrite =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetWrite);
	struct nfs3_write_data write_data;
	WRITE3resfail *resfail = &res->res_write3.WRITE3res_u.resfail;
	struct fsal_io_arg *write_arg = alloca(sizeof(*write_arg) +
					       sizeof(struct iovec));

	write_data.rc = NFS_REQ_OK;

	write_arg->offset = arg->arg_write3.offset;
	size = arg->arg_write3.count;

	if ((arg->arg_write3.stable == DATA_SYNC) ||
	    (arg->arg_write3.stable == FILE_SYNC))
		write_arg->fsal_stable = true;
	else
		write_arg->fsal_stable = false;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR], *stables = "";

		switch (arg->arg_write3.stable) {
		case UNSTABLE:
			stables = "UNSTABLE";
			break;
		case DATA_SYNC:
			stables = "DATA_SYNC";
			break;
		case FILE_SYNC:
			stables = "FILE_SYNC";
			break;
		}

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &arg->arg_write3.file,
				 NULL,
				 str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Write handle: %s start: %"
			 PRIx64 " len: %zx %s",
			 str, write_arg->offset, size, stables);
	}

	/* to avoid setting it on each error case */
	resfail->file_wcc.before.attributes_follow = false;
	resfail->file_wcc.after.attributes_follow = false;

	obj = nfs3_FhandleToCache(&arg->arg_write3.file,
				    &res->res_write3.status,
				    &write_data.rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		return write_data.rc;
	}

	nfs_SetPreOpAttr(obj, &pre_attr);

	fsal_status =
	    obj->obj_ops->test_access(obj, FSAL_WRITE_ACCESS, NULL, NULL, true);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_write3.status = nfs3_Errno_status(fsal_status);
		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* Sanity check: write only a regular file */
	if (obj->type != REGULAR_FILE) {
		if (obj->type == DIRECTORY)
			res->res_write3.status = NFS3ERR_ISDIR;
		else
			res->res_write3.status = NFS3ERR_INVAL;

		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* if quota support is active, then we should check is the
	   FSAL allows inode creation or not */
	fsal_status =
	    op_ctx->fsal_export->exp_ops.check_quota(op_ctx->fsal_export,
						   op_ctx->ctx_export->fullpath,
						   FSAL_QUOTA_BLOCKS);

	if (FSAL_IS_ERROR(fsal_status)) {
		res->res_write3.status = NFS3ERR_DQUOT;
		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	if (size > arg->arg_write3.data.data_len) {
		/* should never happen */
		res->res_write3.status = NFS3ERR_INVAL;
		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* Do not exceed maxium WRITE offset if set */
	if (MaxOffsetWrite < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Write offset=%" PRIu64 " size=%zu MaxOffSet=%"
			     PRIu64,
			     write_arg->offset, size, MaxOffsetWrite);

		if ((write_arg->offset + size) > MaxOffsetWrite) {
			LogEvent(COMPONENT_NFSPROTO,
				 "A client tryed to violate max file size %"
				 PRIu64 " for exportid #%hu",
				 MaxOffsetWrite,
				 op_ctx->ctx_export->export_id);

			res->res_write3.status = NFS3ERR_FBIG;

			nfs_SetWccData(NULL, obj, &resfail->file_wcc);

			write_data.rc = NFS_REQ_OK;
			goto putref;
		}
	}

	/* We should take care not to exceed FSINFO wtmax field for the size */
	if (size > MaxWrite) {
		/* The client asked for too much data, we must restrict him */
		size = MaxWrite;
	}

	if (size == 0) {
		fsal_status = fsalstat(ERR_FSAL_NO_ERROR, 0);
		res->res_write3.status = NFS3_OK;
		nfs_SetWccData(NULL, obj, &resfail->file_wcc);
		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* An actual write is to be made, prepare it */

	/* Check for delegation conflict. */
	if (state_deleg_conflict(obj, true)) {
		res->res_write3.status = NFS3ERR_JUKEBOX;
		write_data.rc = NFS_REQ_OK;
		goto putref;
	}

	write_arg->info = NULL;
	/** @todo for now pass NULL state */
	write_arg->state = NULL;
	write_arg->iov_count = 1;
	write_arg->iov[0].iov_len = size;
	write_arg->iov[0].iov_base = arg->arg_write3.data.data_val;
	write_arg->io_amount = 0;

	write_data.res = res;

	obj->obj_ops->write2(obj, true, nfs3_write_cb, write_arg, &write_data);
	return write_data.rc;

 putref:
	/* return references */
	obj->obj_ops->put_ref(obj);

	server_stats_io_done(size, 0,
			     (write_data.rc == NFS_REQ_OK) ? true : false,
			     true);
	return write_data.rc;

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
	/* Nothing to do here */
}
