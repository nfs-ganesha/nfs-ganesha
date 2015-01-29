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
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "export_mgr.h"

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
int nfs4_op_lookupp(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	LOOKUPP4res * const res_LOOKUPP4 = &resp->nfs_resop4_u.oplookupp;
	cache_entry_t *dir_entry = NULL;
	cache_entry_t *file_entry;
	cache_inode_status_t cache_status;
	struct gsh_export *original_export = op_ctx->export;

	resp->resop = NFS4_OP_LOOKUPP;
	res_LOOKUPP4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_LOOKUPP4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_LOOKUPP4->status != NFS4_OK)
		return res_LOOKUPP4->status;

	/* Preparing for cache_inode_lookup ".." */
	file_entry = NULL;
	dir_entry = data->current_entry;

	/* If Filehandle points to the root of the current export, then backup
	 * through junction into the containing export.
	 */
	if (data->current_entry->type != DIRECTORY)
		goto not_junction;

	PTHREAD_RWLOCK_rdlock(&original_export->lock);

	if (data->current_entry == original_export->exp_root_cache_inode) {
		struct gsh_export *parent_exp = NULL;

		/* Handle reverse junction */
		LogDebug(COMPONENT_EXPORT,
			 "Handling reverse junction from Export_Id %d Path %s Parent=%p",
			 original_export->export_id,
			 original_export->fullpath,
			 original_export->exp_parent_exp);

		if (original_export->exp_parent_exp == NULL) {
			/* lookupp on the root on the pseudofs should return
			 * NFS4ERR_NOENT (RFC3530, page 166)
			 */
			PTHREAD_RWLOCK_unlock(&original_export->lock);
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return res_LOOKUPP4->status;
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

		/* Get the junction inode into dir_entry and parent_exp
		 * for reference.
		 */
		dir_entry = original_export->exp_junction_inode;
		parent_exp = original_export->exp_parent_exp;

		/* Check if there is a problem with the export and try and
		 * get a reference to the parent export.
		 */
		if (dir_entry == NULL ||
		    !get_gsh_export_ref(parent_exp, false)) {
			/* Export is in the process of dying */
			PTHREAD_RWLOCK_unlock(&original_export->lock);
			LogCrit(COMPONENT_EXPORT,
				"Reverse junction from Export_Id %d Path %s Parent=%p is stale",
				original_export->export_id,
				original_export->fullpath,
				parent_exp);
			res_LOOKUPP4->status = NFS4ERR_STALE;
			return res_LOOKUPP4->status;
		}

		if (cache_inode_lru_ref(dir_entry, LRU_FLAG_NONE) !=
		    CACHE_INODE_SUCCESS) {
			/* junction inode has gone stale. */
			PTHREAD_RWLOCK_unlock(&original_export->lock);
			LogCrit(COMPONENT_EXPORT,
				"Reverse junction from Export_Id %d Path %s Parent=%p is stale",
				original_export->export_id,
				original_export->fullpath,
				parent_exp);
			res_LOOKUPP4->status = NFS4ERR_STALE;
			return res_LOOKUPP4->status;
		}

		/* Set up dir_entry as current entry with an LRU reference
		 * while still holding the lock.
		 */
		set_current_entry(data, dir_entry);

		/* Stash parent export in opctx while still holding the lock.
		 */
		op_ctx->export = parent_exp;
		op_ctx->fsal_export = op_ctx->export->fsal_export;

		/* Now we are safely transitioned to the parent export and can
		 * release the lock.
		 */
		PTHREAD_RWLOCK_unlock(&original_export->lock);

		/* Release old export reference that was held by opctx. */
		put_gsh_export(original_export);

		/* Build credentials */
		res_LOOKUPP4->status = nfs4_export_check_access(data->req);

		/* Test for access error (export should not be visible). */
		if (res_LOOKUPP4->status == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client doesn't
			 * have access to this export, return NFS4ERR_NOENT to
			 * hide it. It was not visible in READDIR response.
			 */
			LogDebug(COMPONENT_EXPORT,
				 "NFS4ERR_ACCESS Hiding Export_Id %d Path %s with NFS4ERR_NOENT",
				 parent_exp->export_id,
				 parent_exp->fullpath);
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return res_LOOKUPP4->status;
		}
	} else {
		/* Release the lock taken above */
		PTHREAD_RWLOCK_unlock(&original_export->lock);
	}

not_junction:

	cache_status = cache_inode_lookupp(dir_entry, &file_entry);

	if (file_entry != NULL) {
		/* Convert it to a file handle */
		if (!nfs4_FSALToFhandle(&data->currentFH,
					file_entry->obj_handle,
					op_ctx->export)) {
			res_LOOKUPP4->status = NFS4ERR_SERVERFAULT;
			cache_inode_put(file_entry);
			return res_LOOKUPP4->status;
		}

		/* Keep the pointer within the compound data */
		set_current_entry(data, file_entry);

		/* Return successfully */
		res_LOOKUPP4->status = NFS4_OK;
	} else {
		/* Unable to look up parent for some reason.
		 * Return error.
		 */
		set_current_entry(data, NULL);
		res_LOOKUPP4->status = nfs4_Errno(cache_status);
	}

	return res_LOOKUPP4->status;
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
	return;
}				/* nfs4_op_lookupp_Free */
