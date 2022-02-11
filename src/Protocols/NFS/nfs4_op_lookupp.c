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
 * @file    nfs4_op_lookupp.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "export_mgr.h"
#include "nfs_proto_functions.h"

/**
 * @brief NFS4_OP_LOOKUPP
 *
 * This function implements the NFS4_OP_LOOKUPP operation, which looks
 * up the parent of the supplied directory.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 369
 *
 */
enum nfs_req_result nfs4_op_lookupp(struct nfs_argop4 *op,
				    compound_data_t *data,
				    struct nfs_resop4 *resp)
{
	LOOKUPP4res * const res_LOOKUPP4 = &resp->nfs_resop4_u.oplookupp;
	struct fsal_obj_handle *dir_obj = NULL;
	struct fsal_obj_handle *file_obj;
	struct fsal_obj_handle *root_obj;
	fsal_status_t status;
	struct gsh_export *original_export = op_ctx->ctx_export;

	resp->resop = NFS4_OP_LOOKUPP;
	res_LOOKUPP4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_LOOKUPP4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_LOOKUPP4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Preparing for fsal_lookupp */
	file_obj = NULL;
	dir_obj = data->current_obj;

	/* If Filehandle points to the root of the current export, then backup
	 * through junction into the containing export.
	 */
	if (data->current_obj->type != DIRECTORY)
		goto not_junction;

	status = nfs_export_get_root_entry(original_export, &root_obj);
	if (FSAL_IS_ERROR(status)) {
		res_LOOKUPP4->status = nfs4_Errno_status(status);
		return NFS_REQ_ERROR;
	}

	PTHREAD_RWLOCK_rdlock(&original_export->lock);

	if (data->current_obj == root_obj) {
		struct gsh_export *parent_exp = NULL;

		/* Handle reverse junction */
		LogDebug(COMPONENT_EXPORT,
			 "Handling reverse junction from Export_Id %d Pseudo %s Parent=%p",
			 original_export->export_id,
			 CTX_PSEUDOPATH(op_ctx),
			 original_export->exp_parent_exp);

		if (original_export->exp_parent_exp == NULL) {
			/* lookupp on the root on the pseudofs should return
			 * NFS4ERR_NOENT (RFC3530, page 166)
			 */
			root_obj->obj_ops->put_ref(root_obj);
			PTHREAD_RWLOCK_unlock(&original_export->lock);
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return NFS_REQ_ERROR;
		}

		PTHREAD_RWLOCK_unlock(&original_export->lock);

		/* Clear out data->current entry outside lock
		 * so if it cascades into cleanup, we aren't holding
		 * an export lock that would cause trouble.
		 */
		set_current_entry(data, NULL);

		/* We need to protect accessing the parent information
		 * with the export lock. We use the current export's lock
		 * which is plenty, the parent can't go away without
		 * grabbing the current export's lock to clean out the
		 * parent information.
		 */
		PTHREAD_RWLOCK_rdlock(&original_export->lock);

		/* Get the junction inode into dir_obj and parent_exp
		 * for reference.
		 */
		dir_obj = original_export->exp_junction_obj;
		parent_exp = original_export->exp_parent_exp;

		/* Check if there is a problem with the export and try and
		 * get a reference to the parent export.
		 */
		if (dir_obj == NULL || parent_exp == NULL ||
		    !export_ready(parent_exp)) {
			/* Export is in the process of dying */
			root_obj->obj_ops->put_ref(root_obj);
			PTHREAD_RWLOCK_unlock(&original_export->lock);
			LogCrit(COMPONENT_EXPORT,
				"Reverse junction from Export_Id %d Pseudo %s Parent=%p is stale",
				original_export->export_id,
				CTX_PSEUDOPATH(op_ctx),
				parent_exp);
			res_LOOKUPP4->status = NFS4ERR_STALE;
			return NFS_REQ_ERROR;
		}

		/* Get reference to parent_exp for op context. */
		get_gsh_export_ref(parent_exp);

		dir_obj->obj_ops->get_ref(dir_obj);

		/* Set up dir_obj as current obj with an LRU reference
		 * while still holding the lock.
		 */
		set_current_entry(data, dir_obj);

		/* Put our ref */
		dir_obj->obj_ops->put_ref(dir_obj);

		/* We are now done with original_export->lock, nothing following
		 * depends on it being held.
		 */
		PTHREAD_RWLOCK_unlock(&original_export->lock);

		/* Release the original_export and put the parent_exp into
		 * the op context.
		 */
		set_op_context_export(parent_exp);

		/* Build credentials */
		res_LOOKUPP4->status = nfs4_export_check_access(data->req);

		/* Test for access error (export should not be visible). */
		if (res_LOOKUPP4->status == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client doesn't
			 * have access to this export, return NFS4ERR_NOENT to
			 * hide it. It was not visible in READDIR response.
			 */
			root_obj->obj_ops->put_ref(root_obj);
			LogDebug(COMPONENT_EXPORT,
				 "NFS4ERR_ACCESS Hiding Export_Id %d Pseudo %s with NFS4ERR_NOENT",
				 parent_exp->export_id,
				 CTX_PSEUDOPATH(op_ctx));
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return NFS_REQ_ERROR;
		}
	} else {
		/* Release the lock taken above */
		PTHREAD_RWLOCK_unlock(&original_export->lock);
	}

	/* Return our ref from above */
	root_obj->obj_ops->put_ref(root_obj);

not_junction:

	status = fsal_lookupp(dir_obj, &file_obj, NULL);

	if (file_obj != NULL) {
		/* Convert it to a file handle */
		if (!nfs4_FSALToFhandle(false, &data->currentFH,
						file_obj,
						op_ctx->ctx_export)) {
			res_LOOKUPP4->status = NFS4ERR_SERVERFAULT;
			file_obj->obj_ops->put_ref(file_obj);
			return NFS_REQ_ERROR;
		}

		/* Keep the pointer within the compound data */
		set_current_entry(data, file_obj);

		/* Put our ref */
		file_obj->obj_ops->put_ref(file_obj);

		/* Return successfully */
		res_LOOKUPP4->status = NFS4_OK;
	} else {
		/* Unable to look up parent for some reason.
		 * Return error.
		 */
		set_current_entry(data, NULL);
		res_LOOKUPP4->status = nfs4_Errno_status(status);
	}

	return nfsstat4_to_nfs_req_result(res_LOOKUPP4->status);
}				/* nfs4_op_lookupp */

/**
 * @brief Free memory allocated for LOOKUPP result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOOKUPP operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_lookupp_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
