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
 * @file    nfs4_op_putfh.c
 * @brief   Routines used for managing the NFS4_OP_PUTFH operation.
 *
 * Routines used for managing the NFS4_OP_PUTFH operation.
 *
 */
#include "config.h"
#include "hashtable.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "export_mgr.h"
#include "client_mgr.h"
#include "fsal_convert.h"

/**
 * @brief The NFS4_OP_PUTFH operation
 *
 * Sets the current FH with the value given in argument.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 371
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_putfh(struct nfs_argop4 *op, compound_data_t *data,
		  struct nfs_resop4 *resp)
{
	struct fsal_export *export;
	struct gsh_export *last_export;
	struct file_handle_v4 *v4_handle;
	cache_entry_t *file_entry;

	/* Convenience alias for args */
	PUTFH4args * const arg_PUTFH4 = &op->nfs_argop4_u.opputfh;
	/* Convenience alias for resopnse */
	PUTFH4res * const res_PUTFH4 = &resp->nfs_resop4_u.opputfh;

	resp->resop = NFS4_OP_PUTFH;

	/* First check the handle.  If it is rubbish, we go no further
	 */
	res_PUTFH4->status = nfs4_Is_Fh_Invalid(&arg_PUTFH4->object);
	if (res_PUTFH4->status != NFS4_OK)
		return res_PUTFH4->status;

	/* If no currentFH were set, allocate one */
	if (data->currentFH.nfs_fh4_val == NULL) {
		res_PUTFH4->status = nfs4_AllocateFH(&(data->currentFH));
		if (res_PUTFH4->status != NFS4_OK)
			return res_PUTFH4->status;
	}

	/* Copy the filehandle from the arg structure */
	data->currentFH.nfs_fh4_len = arg_PUTFH4->object.nfs_fh4_len;

	/* Put the data in place */
	memcpy(data->currentFH.nfs_fh4_val, arg_PUTFH4->object.nfs_fh4_val,
	       arg_PUTFH4->object.nfs_fh4_len);

	/* If old CurrentFH had a related export, release reference. */
	if (op_ctx->export != NULL)
		put_gsh_export(op_ctx->export);

	last_export = op_ctx->export;

	/* Clear out current entry for now */
	set_current_entry(data, NULL, false);

	v4_handle = (struct file_handle_v4 *)data->currentFH.nfs_fh4_val;

	/* Get the exportid from the handle. */
	op_ctx->export = get_gsh_export(v4_handle->exportid);

	if (op_ctx->export == NULL) {
		LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			"NFS4 Request from client %s has invalid export %d",
			op_ctx->client
				? op_ctx->client->hostaddr_str
				: "unknown client",
			v4_handle->exportid);

		res_PUTFH4->status = NFS4ERR_STALE;
		return res_PUTFH4->status;
	}

	op_ctx->fsal_export = op_ctx->export->fsal_export;

	if (op_ctx->export != last_export) {
		res_PUTFH4->status = nfs4_MakeCred(data);

		if (res_PUTFH4->status != NFS4_OK)
			return res_PUTFH4->status;
	}

	export = op_ctx->fsal_export;

	/* The export and fsalid should be updated, but DS handles
	 * don't support metdata operations.  Thus, we can't call into
	 * cache_inode to populate the metadata cache.
	 */
	if (nfs4_Is_Fh_DSHandle(&data->currentFH)) {
		struct gsh_buffdesc fh_desc;

		fh_desc.addr = v4_handle->fsopaque;
		fh_desc.len = v4_handle->fs_len;

		/* Leave the current_entry as NULL, but indicate a
		 * regular file.
		 */
		data->current_filetype = REGULAR_FILE;

		res_PUTFH4->status =
		    export->ops->create_ds_handle(export,
						  &fh_desc,
						  &data->current_ds);

		if (res_PUTFH4->status != NFS4_OK)
			return res_PUTFH4->status;
	} else {
		cache_inode_fsal_data_t fsal_data;
		fsal_status_t fsal_status;
		cache_inode_status_t cache_status;

		fsal_data.export = export;
		fsal_data.fh_desc.len = v4_handle->fs_len;
		fsal_data.fh_desc.addr = &v4_handle->fsopaque;

		/* adjust the handle opaque into a cache key */
		fsal_status =
		    export->ops->extract_handle(export,
						FSAL_DIGEST_NFSV4,
						&fsal_data.fh_desc);

		if (FSAL_IS_ERROR(fsal_status)) {
			res_PUTFH4->status =
			    nfs4_Errno(cache_inode_error_convert(fsal_status));
			return res_PUTFH4->status;
		}
		/* Build the pentry.  Refcount +1. */
		cache_status = cache_inode_get(&fsal_data,
					       &file_entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			res_PUTFH4->status = nfs4_Errno(cache_status);
			return res_PUTFH4->status;
		}

		/* Set the current entry using the ref from get */
		set_current_entry(data, file_entry, false);

		LogFullDebug(COMPONENT_FILEHANDLE,
			     "File handle is of type %s(%d)",
			     object_file_type_to_str(data->current_filetype),
			     data->current_filetype);
	}

	return NFS4_OK;
}				/* nfs4_op_putfh */

/**
 * @brief Free memory allocated for PUTFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_PUTFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_putfh_Free(nfs_resop4 *resp)
{
	/* Nothing to be freed */
	return;
}				/* nfs4_op_putfh_Free */
