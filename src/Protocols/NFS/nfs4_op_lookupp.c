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
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
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
	cache_entry_t *dir_entry;
	cache_entry_t *file_entry;
	cache_inode_status_t cache_status;

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
	if (data->current_entry->type == DIRECTORY
	    && data->current_entry ==
	    data->req_ctx->export->export.exp_root_cache_inode) {
		/* Handle reverse junction */
		LogDebug(COMPONENT_NFS_V4_PSEUDO,
			 "Handling reverse junction from Export_Id %d Path %s Parent=%p",
			 data->export->id,
			 data->export->fullpath,
			 data->export->exp_parent_exp);

		if (data->export->exp_parent_exp == NULL) {
			/* lookupp on the root on the pseudofs should return
			 * NFS4ERR_NOENT (RFC3530, page 166)
			 */
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return res_LOOKUPP4->status;
		}

		/* Remember the dir_entry representing the junction and
		 * set it as the current entry with a reference for proper
		 * cleanup if there is an error.
		 *
		 * Note that we will actually lookup the junction's
		 * parent. We NEVER return a handle to the junction inode
		 * itself.
		 */
		dir_entry = data->export->exp_junction_inode;
		set_current_entry(data, dir_entry, true);

		/* Release any old export reference */
		if (data->req_ctx->export != NULL)
			put_gsh_export(data->req_ctx->export);

		/* Get a reference to the export and stash it in
		 * compound data.
		 */
		get_gsh_export_ref(data->export->exp_parent_exp);

		data->req_ctx->export = data->export->exp_parent_exp;
		data->export = &data->req_ctx->export->export;

		/* Build credentials */
		res_LOOKUPP4->status = nfs4_MakeCred(data);

		/* Test for access error (export should not be visible). */
		if (res_LOOKUPP4->status == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client doesn't
			 * have access to this export, return NFS4ERR_NOENT to
			 * hide it. It was not visible in READDIR response.
			 */
			LogDebug(COMPONENT_NFS_V4_PSEUDO,
				 "NFS4ERR_ACCESS Hiding Export_Id %d Path %s with NFS4ERR_NOENT",
				 data->export->id, data->export->fullpath);
			res_LOOKUPP4->status = NFS4ERR_NOENT;
			return res_LOOKUPP4->status;
		}
	}

	cache_status =
	    cache_inode_lookupp(dir_entry, data->req_ctx, &file_entry);

	if (file_entry != NULL) {
		/* Convert it to a file handle */
		if (!nfs4_FSALToFhandle(&data->currentFH,
					file_entry->obj_handle)) {
			res_LOOKUPP4->status = NFS4ERR_SERVERFAULT;
			cache_inode_put(file_entry);
			return res_LOOKUPP4->status;
		}

		/* Keep the pointer within the compound data */
		set_current_entry(data, file_entry, true);

		/* Return successfully */
		res_LOOKUPP4->status = NFS4_OK;
	} else {
		/* Unable to look up parent for some reason.
		 * Return error.
		 */
		set_current_entry(data, NULL, false);
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
