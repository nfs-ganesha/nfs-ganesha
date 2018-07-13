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
 * @file    nfs4_op_lookup.c
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
#include "nfs_convert.h"
#include "export_mgr.h"

/**
 * @brief NFS4_OP_LOOKUP
 *
 * This function implments the NFS4_OP_LOOKUP operation, which looks
 * a filename up in the FSAL.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 368-9
 *
 */

int nfs4_op_lookup(struct nfs_argop4 *op, compound_data_t *data,
		   struct nfs_resop4 *resp)
{
	/* Convenient alias for the arguments */
	LOOKUP4args * const arg_LOOKUP4 = &op->nfs_argop4_u.oplookup;
	/* Convenient alias for the response  */
	LOOKUP4res * const res_LOOKUP4 = &resp->nfs_resop4_u.oplookup;
	/* The name to look up */
	char *name = NULL;
	/* The directory in which to look up the name */
	struct fsal_obj_handle *dir_obj = NULL;
	/* The name found */
	struct fsal_obj_handle *file_obj = NULL;
	/* Status code from fsal */
	fsal_status_t status = {0, 0};

	resp->resop = NFS4_OP_LOOKUP;
	res_LOOKUP4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_LOOKUP4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);
	if (res_LOOKUP4->status != NFS4_OK) {
		/* for some reason lookup is picky.  Just not being
		 * dir is not enough.  We want to know it is a symlink
		 */
		if (res_LOOKUP4->status == NFS4ERR_NOTDIR
		    && data->current_filetype == SYMBOLIC_LINK)
			res_LOOKUP4->status = NFS4ERR_SYMLINK;
		goto out;
	}

	/* Validate and convert the UFT8 objname to a regular string */
	res_LOOKUP4->status = nfs4_utf8string2dynamic(&arg_LOOKUP4->objname,
						      UTF8_SCAN_ALL,
						      &name);

	if (res_LOOKUP4->status != NFS4_OK)
		goto out;

	LogDebug(COMPONENT_NFS_V4, "name=%s", name);

	/* Do the lookup in the FSAL */
	file_obj = NULL;
	dir_obj = data->current_obj;

	/* Sanity check: dir_obj should be ACTUALLY a directory */

	status = fsal_lookup(dir_obj, name, &file_obj, NULL);
	if (FSAL_IS_ERROR(status)) {
		res_LOOKUP4->status = nfs4_Errno_status(status);
		goto out;
	}

	if (file_obj->type == DIRECTORY) {
		PTHREAD_RWLOCK_rdlock(&file_obj->state_hdl->state_lock);

		if (file_obj->state_hdl->dir.junction_export != NULL) {
			/* Handle junction */
			struct fsal_obj_handle *obj = NULL;

			/* Attempt to get a reference to the export across the
			 * junction.
			 */
			if (!export_ready(
				file_obj->state_hdl->dir.junction_export)) {
				/* If we could not get a reference, return
				 * stale.  Release state_lock
				 */
				LogDebug(COMPONENT_EXPORT,
					 "NFS4ERR_STALE on LOOKUP of %s", name);
				res_LOOKUP4->status = NFS4ERR_STALE;
				PTHREAD_RWLOCK_unlock(
					&file_obj->state_hdl->state_lock);
				goto out;
			}

			get_gsh_export_ref(
				file_obj->state_hdl->dir.junction_export);

			/* Release any old export reference */
			if (op_ctx->ctx_export != NULL)
				put_gsh_export(op_ctx->ctx_export);

			/* Stash the new export in the compound data. */
			op_ctx->ctx_export =
				file_obj->state_hdl->dir.junction_export;
			op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;

			PTHREAD_RWLOCK_unlock(&file_obj->state_hdl->state_lock);
			/* Build credentials */
			res_LOOKUP4->status =
				nfs4_export_check_access(data->req);

			/* Test for access error (export should not be visible).
			 */
			if (res_LOOKUP4->status == NFS4ERR_ACCESS) {
				/* If return is NFS4ERR_ACCESS then this client
				 * doesn't have access to this export, return
				 * NFS4ERR_NOENT to hide it. It was not visible
				 * in READDIR response.
				 */
				LogDebug(COMPONENT_EXPORT,
					"NFS4ERR_ACCESS Hiding Export_Id %d Pseudo %s with NFS4ERR_NOENT",
					op_ctx->ctx_export->export_id,
					op_ctx->ctx_export->pseudopath);
				res_LOOKUP4->status = NFS4ERR_NOENT;
				goto out;
			}

			if (res_LOOKUP4->status == NFS4ERR_WRONGSEC) {
				/* LogInfo already documents why */
				goto out;
			}

			if (res_LOOKUP4->status != NFS4_OK) {
				/* Should never get here,
				 * nfs4_export_check_access can only return
				 * NFS4_OK, NFS4ERR_ACCESS or NFS4ERR_WRONGSEC.
				 */
				LogMajor(COMPONENT_EXPORT,
					"PSEUDO FS JUNCTION TRAVERSAL: Failed with %s for %s, id=%d",
					nfsstat4_to_str(res_LOOKUP4->status),
					op_ctx->ctx_export->pseudopath,
					op_ctx->ctx_export->export_id);
				goto out;
			}

			status = nfs_export_get_root_entry(op_ctx->ctx_export,
							   &obj);

			if (FSAL_IS_ERROR(status)) {
				LogMajor(COMPONENT_EXPORT,
					"PSEUDO FS JUNCTION TRAVERSAL: Failed to get root for %s, id=%d, status = %s",
					op_ctx->ctx_export->pseudopath,
					op_ctx->ctx_export->export_id,
					msg_fsal_err(status.major));

				res_LOOKUP4->status = nfs4_Errno_status(status);
				goto out;
			}

			LogDebug(COMPONENT_EXPORT,
				"PSEUDO FS JUNCTION TRAVERSAL: Crossed to %s, id=%d for name=%s",
				op_ctx->ctx_export->pseudopath,
				op_ctx->ctx_export->export_id, name);

			file_obj->obj_ops->put_ref(file_obj);
			file_obj = obj;
		} else {
			PTHREAD_RWLOCK_unlock(&file_obj->state_hdl->state_lock);
		}
	}

	/* Convert it to a file handle */
	if (!nfs4_FSALToFhandle(false, &data->currentFH, file_obj,
					op_ctx->ctx_export)) {
		res_LOOKUP4->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/* Keep the pointer within the compound data */
	set_current_entry(data, file_obj);

	/* Put our ref */
	file_obj->obj_ops->put_ref(file_obj);
	file_obj = NULL;

	/* Return successfully */
	res_LOOKUP4->status = NFS4_OK;

 out:
	/* Release reference on file_obj if we didn't utilze it. */
	if (file_obj)
		file_obj->obj_ops->put_ref(file_obj);

	gsh_free(name);

	return res_LOOKUP4->status;
}				/* nfs4_op_lookup */

/**
 * @brief Free memory allocated for LOOKUP result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOOKUP operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_lookup_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
