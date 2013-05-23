/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#include "export_mgr.h"

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

int
nfs4_op_putfh(struct nfs_argop4 *op,
              compound_data_t *data,
              struct nfs_resop4 *resp)
{
        /* Convenience alias for args */
        PUTFH4args *const arg_PUTFH4 = &op->nfs_argop4_u.opputfh;
        /* Convenience alias for resopnse */
        PUTFH4res *const res_PUTFH4 = &resp->nfs_resop4_u.opputfh;

        resp->resop = NFS4_OP_PUTFH;

	/* First check the handle.  If it is rubbish, we go no further
	 */
        res_PUTFH4->status = nfs4_Is_Fh_Invalid(&arg_PUTFH4->object);
	if(res_PUTFH4->status != NFS4_OK)
		return res_PUTFH4->status;
	
        /* If no currentFH were set, allocate one */
        if(data->currentFH.nfs_fh4_len == 0) {
                res_PUTFH4->status = nfs4_AllocateFH(&(data->currentFH));
                if (res_PUTFH4->status != NFS4_OK) {
                        return res_PUTFH4->status;
                }
        }

        /* Copy the filehandle from the reply structure */
        data->currentFH.nfs_fh4_len = arg_PUTFH4->object.nfs_fh4_len;

        /* Put the data in place */
        memcpy(data->currentFH.nfs_fh4_val, arg_PUTFH4->object.nfs_fh4_val,
               arg_PUTFH4->object.nfs_fh4_len);

        LogHandleNFS4("NFS4_OP_PUTFH CURRENT FH: ", &arg_PUTFH4->object);

        /* If the filehandle is not pseudo fs file handle, get the
           entry related to it, otherwise use fake values */
        if (nfs4_Is_Fh_Pseudo(&(data->currentFH))) {
		set_compound_data_for_pseudo(data);
        } else {
		struct fsal_export *export;
		struct file_handle_v4 *v4_handle
			= ((struct file_handle_v4 *)data->currentFH.nfs_fh4_val);

		/* Set up the export (and nfs4_MakeCred) for the new currentFH */
		res_PUTFH4->status = nfs4_SetCompoundExport(data);
		if(res_PUTFH4->status != NFS4_OK)
			return res_PUTFH4->status;

                /* As usual, protect existing refcounts */
                if (data->current_entry) {
                        cache_inode_put(data->current_entry);
                        data->current_entry = NULL;
                }
                if (data->current_ds) {
                        data->current_ds->ops->put(data->current_ds);
                }
                data->current_ds = NULL;
                data->current_filetype = NO_FILE_TYPE;
		export = data->req_ctx->export->export.export_hdl;

                /* The export and fsalid should be updated, but DS handles
                   don't support metdata operations.  Thus, we can't call into
                   Cache_inode to populate the metadata cache. */
                if (nfs4_Is_Fh_DSHandle(&data->currentFH)) {
                        struct gsh_buffdesc fh_desc;

                        fh_desc.addr = v4_handle->fsopaque;
                        fh_desc.len = v4_handle->fs_len;
                        data->current_entry = NULL;
                        data->current_filetype = REGULAR_FILE;
                        res_PUTFH4->status
                                = export->ops->create_ds_handle(export,
								&fh_desc,
								&data->current_ds);

                        if (res_PUTFH4->status != NFS4_OK) {
                                return res_PUTFH4->status;
                        }
                } else {
			cache_inode_fsal_data_t fsal_data;
			fsal_status_t fsal_status;

			fsal_data.export = export;
			fsal_data.fh_desc.len = v4_handle->fs_len;
			fsal_data.fh_desc.addr = &v4_handle->fsopaque;

			/* adjust the handle opaque into a cache key */
			fsal_status = export->ops->extract_handle(export,
								  FSAL_DIGEST_NFSV4,
								  &fsal_data.fh_desc);
			if(FSAL_IS_ERROR(fsal_status)) {
				res_PUTFH4->status = NFS4ERR_BADHANDLE;
                                return res_PUTFH4->status;
			}
                        /* Build the pentry.  Refcount +1. */
			cache_inode_get(&fsal_data,
					data->req_ctx,
					&data->current_entry);
                        if(data->current_entry == NULL) {
				res_PUTFH4->status = NFS4ERR_STALE;
                                return res_PUTFH4->status;
                        }
                        /* Extract the filetype */
                        data->current_filetype = data->current_entry->type;
                }
        }

        return NFS4_OK;
}                               /* nfs4_op_putfh */

/**
 * @brief Free memory allocated for PUTFH result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_PUTFH operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void
nfs4_op_putfh_Free(PUTFH4res * resp)
{
        /* Nothing to be freed */
        return;
} /* nfs4_op_putfh_Free */
