// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "export_mgr.h"
#include "client_mgr.h"
#include "fsal_convert.h"
#include "nfs_file_handle.h"
#include "pnfs_utils.h"

static int nfs4_ds_putfh(compound_data_t *data)
{
	struct file_handle_v4 *v4_handle =
		(struct file_handle_v4 *)data->currentFH.nfs_fh4_val;
	struct fsal_pnfs_ds *pds;
	struct gsh_buffdesc fh_desc;
	bool changed = true;

	LogFullDebug(COMPONENT_FILEHANDLE, "NFS4 Handle 0x%X export id %d",
		v4_handle->fhflags1, ntohs(v4_handle->id.exports));

	/* Find any existing server by the "id" from the handle,
	 * before releasing the old DS (to prevent thrashing).
	 */
	pds = pnfs_ds_get(ntohs(v4_handle->id.servers));
	if (pds == NULL) {
		LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			   "NFS4 Request from client (%s) has invalid server identifier %d",
			   op_ctx->client
				?  op_ctx->client->hostaddr_str
				: "unknown",
			   ntohs(v4_handle->id.servers));

		return NFS4ERR_STALE;
	}

	/* If old CurrentFH had a related server, note if there is a change,
	 * the reference to the old fsal_pnfs_ds will be released below.
	 */
	if (op_ctx->ctx_pnfs_ds != NULL) {
		changed = ntohs(v4_handle->id.servers)
			!= op_ctx->ctx_pnfs_ds->id_servers;
	}

	/* If old CurrentFH had a related export, note the change, the reference
	 * to the old export will be released below.
	 */
	if (op_ctx->ctx_export != NULL) {
		changed |= op_ctx->ctx_export != pds->mds_export;
	}

	/* Take export reference if any */
	if (pds->mds_export != NULL)
		get_gsh_export_ref(pds->mds_export);

	/* Set up the op_context with fsal_pnfs_ds, and export if any.
	 * Will also clean out any old export or fsal_pnfs_ds, dropping
	 * references if any.
	 */
	set_op_context_pnfs_ds(pds);

	/* Clear out current entry for now */
	set_current_entry(data, NULL);

	if (changed) {
		int status;
		/* permissions may have changed */
		status = pds->s_ops.ds_permissions(pds, data->req);
		if (status != NFS4_OK)
			return status;
	}

	fh_desc.len = v4_handle->fs_len;
	fh_desc.addr = &v4_handle->fsopaque;

	/* Leave the current_entry as NULL, but indicate a
	 * regular file.
	 */
	data->current_filetype = REGULAR_FILE;

	return pds->s_ops.make_ds_handle(pds, &fh_desc, &data->current_ds,
					 v4_handle->fhflags1);
}

static int nfs4_mds_putfh(compound_data_t *data)
{
	struct file_handle_v4 *v4_handle =
		(struct file_handle_v4 *)data->currentFH.nfs_fh4_val;
	struct gsh_export *exporting;
	char fhbuf[NFS4_FHSIZE];
	struct fsal_export *export;
	struct gsh_buffdesc fh_desc;
	struct fsal_obj_handle *new_hdl;
	fsal_status_t fsal_status = { 0, 0 };
	bool changed = true;

	LogFullDebug(COMPONENT_FILEHANDLE,
		     "NFS4 Handle flags 0x%X export id %d",
		v4_handle->fhflags1, ntohs(v4_handle->id.exports));
	LogFullDebugOpaque(COMPONENT_FILEHANDLE, "NFS4 FSAL Handle %s",
			   LEN_FH_STR, v4_handle->fsopaque, v4_handle->fs_len);

	/* Find any existing export by the "id" from the handle,
	 * before releasing the old export (to prevent thrashing).
	 */
	exporting = get_gsh_export(ntohs(v4_handle->id.exports));
	if (exporting == NULL) {
		LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			   "NFS4 Request from client (%s) has invalid export identifier %d",
			   op_ctx->client
				?  op_ctx->client->hostaddr_str
				: "unknown",
			   ntohs(v4_handle->id.exports));

		return NFS4ERR_STALE;
	}

	/* If old CurrentFH had a related export, check if it changed. The
	 * reference will be released below.
	 */
	if (op_ctx->ctx_export != NULL) {
		changed = ntohs(v4_handle->id.exports) !=
				 op_ctx->ctx_export->export_id;
	}

	/* Clear out current entry for now */
	set_current_entry(data, NULL);

	/* update _ctx fields needed by nfs4_export_check_access and release
	 * any old ctx_export reference. Will also clean up any old
	 * fsal_pnfs_ds that was attached.
	 */
	set_op_context_export(exporting);
	export = exporting->fsal_export;

	if (changed) {
		int status;

		status = nfs4_export_check_access(data->req);
		if (status != NFS4_OK) {
			LogFullDebug(COMPONENT_FILEHANDLE,
				     "Export check access failed %s",
				     nfsstat4_to_str(status));
			return status;
		}
	}

	/*
	 * FIXME: the wire handle can obviously be no larger than NFS4_FHSIZE,
	 * but there is no such limit on a host handle. Here, we assume that as
	 * the size limit. Eventually it might be nice to call into the FSAL to
	 * ask how large a buffer it needs for a host handle.
	 */
	memcpy(fhbuf, &v4_handle->fsopaque, v4_handle->fs_len);
	fh_desc.len = v4_handle->fs_len;
	fh_desc.addr = fhbuf;

	/* adjust the handle opaque into a cache key */
	fsal_status = export->exp_ops.wire_to_host(export,
						   FSAL_DIGEST_NFSV4,
						   &fh_desc,
						   v4_handle->fhflags1);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogFullDebug(COMPONENT_FILEHANDLE,
			     "wire_to_host failed %s",
			     msg_fsal_err(fsal_status.major));
		return nfs4_Errno_status(fsal_status);
	}

	fsal_status = export->exp_ops.create_handle(export, &fh_desc,
						    &new_hdl, NULL);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_FILEHANDLE,
			 "could not get create_handle object error %s",
			 msg_fsal_err(fsal_status.major));
		return nfs4_Errno_status(fsal_status);
	}

	/* Set the current entry using the ref from get */
	set_current_entry(data, new_hdl);

	/* Put our ref */
	new_hdl->obj_ops->put_ref(new_hdl);

	LogFullDebug(COMPONENT_FILEHANDLE,
		     "File handle is of type %s(%d)",
		     object_file_type_to_str(data->current_filetype),
		     data->current_filetype);

	return NFS4_OK;
}

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

enum nfs_req_result nfs4_op_putfh(struct nfs_argop4 *op, compound_data_t *data,
				  struct nfs_resop4 *resp)
{
	/* Convenience alias for args */
	PUTFH4args * const arg_PUTFH4 = &op->nfs_argop4_u.opputfh;
	/* Convenience alias for resopnse */
	PUTFH4res * const res_PUTFH4 = &resp->nfs_resop4_u.opputfh;

	resp->resop = NFS4_OP_PUTFH;

	/* First check the handle.  If it is rubbish, we go no further
	 */
	res_PUTFH4->status = nfs4_Is_Fh_Invalid(&arg_PUTFH4->object);
	if (res_PUTFH4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* If no currentFH were set, allocate one */
	if (data->currentFH.nfs_fh4_val == NULL)
		nfs4_AllocateFH(&data->currentFH);

	/* Copy the filehandle from the arg structure */
	data->currentFH.nfs_fh4_len = arg_PUTFH4->object.nfs_fh4_len;
	memcpy(data->currentFH.nfs_fh4_val, arg_PUTFH4->object.nfs_fh4_val,
	       arg_PUTFH4->object.nfs_fh4_len);

	/* The export and fsalid should be updated, but DS handles
	 * don't support metadata operations.  Thus, we can't call into
	 * mdcache to populate the metadata cache.
	 */
	if (nfs4_Is_Fh_DSHandle(&data->currentFH))
		res_PUTFH4->status = nfs4_ds_putfh(data);
	else
		res_PUTFH4->status = nfs4_mds_putfh(data);

	return nfsstat4_to_nfs_req_result(res_PUTFH4->status);
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
	/* Nothing to be done */
}
