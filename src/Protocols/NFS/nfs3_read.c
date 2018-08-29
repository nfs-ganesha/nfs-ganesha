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

static void nfs_read_ok(nfs_res_t *res, char *data, uint32_t read_size,
			struct fsal_obj_handle *obj, int eof)
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

struct nfs3_read_data {
	nfs_res_t *res;		/**< Results for read */
	int rc;			/**< Return code */
};

/**
 * @brief Callback for NFS3 read done
 *
 * @param[in] obj		Object being acted on
 * @param[in] ret		Return status of call
 * @param[in] read_data		Data for read call
 * @param[in] caller_data	Data for caller
 */
static void nfs3_read_cb(struct fsal_obj_handle *obj, fsal_status_t ret,
			  void *read_data, void *caller_data)
{
	struct nfs3_read_data *data = caller_data;
	struct fsal_io_arg *read_arg = read_data;
	READ3resfail *resfail = &data->res->res_read3.READ3res_u.resfail;
	int i;

	/* Fixup FSAL_SHARE_DENIED status */
	if (ret.major == ERR_FSAL_SHARE_DENIED)
		ret = fsalstat(ERR_FSAL_LOCKED, 0);

	if (!FSAL_IS_ERROR(ret)) {
		nfs_read_ok(data->res, read_arg->iov[0].iov_base,
			    read_arg->io_amount, obj, read_arg->end_of_file);
		data->rc = NFS_REQ_OK;
		if (!read_arg->end_of_file) {
			/** @todo FSF: add a config option for this behavior?
			*/
			/*
			 * NFS requires to set the EOF flag for all reads that
			 * reach the EOF, i.e., even the ones returning data.
			 * Most FSALs don't set the flag in this case. The only
			 * client that cares about this is ESXi. Other clients
			 * will just see a short read and continue reading and
			 * then get the EOF flag as 0 bytes are returned.
			 */
			struct attrlist attrs;
			fsal_status_t status;

			fsal_prepare_attrs(&attrs, ATTR_SIZE);
			status = obj->obj_ops->getattrs(obj, &attrs);

			if (FSAL_IS_SUCCESS(status)) {
				read_arg->end_of_file = (read_arg->offset +
							 read_arg->io_amount)
							>= attrs.filesize;
			}

			/* Done with the attrs */
			fsal_release_attrs(&attrs);
		}
		goto out;
	}

	for (i = 0; i < read_arg->iov_count; ++i) {
		gsh_free(read_arg->iov[i].iov_base);
	}

	/* If we are here, there was an error */
	if (nfs_RetryableError(ret.major)) {
		data->rc = NFS_REQ_DROP;
		goto out;
	}

	data->res->res_read3.status = nfs3_Errno_status(ret);

	nfs_SetPostOpAttr(obj, &resfail->file_attributes, NULL);

	data->rc = NFS_REQ_OK;

 out:
	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	server_stats_io_done(read_arg->iov[0].iov_len, read_arg->io_amount,
			     (data->rc == NFS_REQ_OK) ?  true : false, false);
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
	void *data = NULL;
	uint64_t MaxRead = atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxRead);
	uint64_t MaxOffsetRead =
		atomic_fetch_uint64_t(&op_ctx->ctx_export->MaxOffsetRead);
	READ3resfail *resfail = &res->res_read3.READ3res_u.resfail;
	struct nfs3_read_data read_data;
	struct fsal_io_arg *read_arg = alloca(sizeof(*read_arg) +
						sizeof(struct iovec));

	read_data.rc = NFS_REQ_OK;

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		read_arg->offset = arg->arg_read3.offset;
		size = arg->arg_read3.count;

		nfs_FhandleToStr(req->rq_msg.cb_vers, &arg->arg_read3.file,
				 NULL, str);
		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Read handle: %s start: %"
			 PRIu64 " len: %zu",
			 str, read_arg->offset, size);
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
				    &res->res_read3.status, &read_data.rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		server_stats_io_done(size, 0, false, false);
		goto putref;
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
		read_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* Sanity check: read only from a regular file */
	if (obj->type != REGULAR_FILE) {
		if (obj->type == DIRECTORY)
			res->res_read3.status = NFS3ERR_ISDIR;
		else
			res->res_read3.status = NFS3ERR_INVAL;

		read_data.rc = NFS_REQ_OK;
		goto putref;
	}

	/* Extract the argument from the request */
	read_arg->offset = arg->arg_read3.offset;
	size = arg->arg_read3.count;

	/* do not exceed maxium READ offset if set */
	if (MaxOffsetRead < UINT64_MAX) {
		LogFullDebug(COMPONENT_NFSPROTO,
			     "Read offset=%" PRIu64
			     " count=%zd MaxOffSet=%" PRIu64,
			     read_arg->offset, size, MaxOffsetRead);

		if ((read_arg->offset + size) > MaxOffsetRead) {
			LogEvent(COMPONENT_NFSPROTO,
				 "A client tryed to violate max file size %"
				 PRIu64 " for exportid #%hu",
				 MaxOffsetRead,
				 op_ctx->ctx_export->export_id);

			res->res_read3.status = NFS3ERR_FBIG;

			nfs_SetPostOpAttr(obj, &resfail->file_attributes, NULL);

			read_data.rc = NFS_REQ_OK;
			goto putref;
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
		nfs_read_ok(res, NULL, 0, obj, 0);
		read_data.rc = NFS_REQ_OK;
		goto putref;
	}

	data = gsh_malloc(size);

	/* Check for delegation conflict. */
	if (state_deleg_conflict(obj, false)) {
		res->res_read3.status = NFS3ERR_JUKEBOX;
		read_data.rc = NFS_REQ_OK;
		gsh_free(data);
		goto putref;
	}

	read_arg->info = NULL;
	/** @todo for now pass NULL state */
	read_arg->state = NULL;
	read_arg->iov_count = 1;
	read_arg->iov[0].iov_len = size;
	read_arg->iov[0].iov_base = data;
	read_arg->io_amount = 0;
	read_arg->end_of_file = false;

	read_data.res = res;

	/* Do the actual read */
	obj->obj_ops->read2(obj, true, nfs3_read_cb, read_arg, &read_data);
	return read_data.rc;

putref:
	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	server_stats_io_done(size, 0,
			     (read_data.rc == NFS_REQ_OK) ? true : false,
			     false);
	return read_data.rc;
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
