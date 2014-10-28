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
#include "cache_inode.h"
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
	cache_entry_t *dir_entry = NULL;
	/* The name found */
	cache_entry_t *file_entry = NULL;
	/* Status code from Cache inode */
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

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

	LogFullDebug(COMPONENT_NFS_V4,
		     "name=%s",
		     name);

	/* Do the lookup in the FSAL */
	file_entry = NULL;
	dir_entry = data->current_entry;

	/* Sanity check: dir_entry should be ACTUALLY a directory */

	cache_status =
	    cache_inode_lookup(dir_entry, name, &file_entry);

	if (cache_status != CACHE_INODE_SUCCESS) {
		res_LOOKUP4->status = nfs4_Errno(cache_status);
		goto out;
	}

	/* Get attr_lock for looking at junction_export */
	PTHREAD_RWLOCK_rdlock(&file_entry->attr_lock);

	if (file_entry->type == DIRECTORY &&
	    file_entry->object.dir.junction_export != NULL) {
		/* Handle junction */
		cache_entry_t *entry = NULL;

		/* Attempt to get a reference to the export across the
		 * junction.
		 */
		if (!get_gsh_export_ref(file_entry->object.dir.junction_export,
					false)) {
			/* If we could not get a reference, return stale.
			 * Release attr_lock
			 */
			PTHREAD_RWLOCK_unlock(&file_entry->attr_lock);
			LogDebug(COMPONENT_EXPORT,
				 "NFS4ERR_STALE on LOOKUP of %s", name);
			res_LOOKUP4->status = NFS4ERR_STALE;
			goto out;
		}

		/* Release any old export reference */
		if (op_ctx->export != NULL)
			put_gsh_export(op_ctx->export);

		/* Stash the new export in the compound data. */
		op_ctx->export = file_entry->object.dir.junction_export;
		op_ctx->fsal_export = op_ctx->export->fsal_export;

		/* Release attr_lock */
		PTHREAD_RWLOCK_unlock(&file_entry->attr_lock);

		/* Build credentials */
		res_LOOKUP4->status = nfs4_export_check_access(data->req);

		/* Test for access error (export should not be visible). */
		if (res_LOOKUP4->status == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client doesn't
			 * have access to this export, return NFS4ERR_NOENT to
			 * hide it. It was not visible in READDIR response.
			 */
			LogDebug(COMPONENT_EXPORT,
				 "NFS4ERR_ACCESS Hiding Export_Id %d Path %s with NFS4ERR_NOENT",
				 op_ctx->export->export_id,
				 op_ctx->export->fullpath);
			res_LOOKUP4->status = NFS4ERR_NOENT;
			goto out;
		}

		if (res_LOOKUP4->status != NFS4_OK) {
			LogMajor(COMPONENT_EXPORT,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get FSAL credentials for %s, id=%d",
				 op_ctx->export->fullpath,
				 op_ctx->export->export_id);
			goto out;
		}

		cache_status =
		    nfs_export_get_root_entry(op_ctx->export, &entry);

		if (cache_status != CACHE_INODE_SUCCESS) {
			LogMajor(COMPONENT_EXPORT,
				 "PSEUDO FS JUNCTION TRAVERSAL: Failed to get root for %s, id=%d, status = %s",
				 op_ctx->export->fullpath,
				 op_ctx->export->export_id,
				 cache_inode_err_str(cache_status));

			res_LOOKUP4->status = nfs4_Errno(cache_status);
			goto out;
		}

		LogDebug(COMPONENT_EXPORT,
			 "PSEUDO FS JUNCTION TRAVERSAL: Crossed to %s, id=%d for name=%s",
			 op_ctx->export->fullpath,
			 op_ctx->export->export_id, name);

		/* Swap in the entry on the other side of the junction. */
		if (file_entry)
			cache_inode_put(file_entry);

		file_entry = entry;
	} else {
		/* Release attr_lock since it wasn't a junction. */
		PTHREAD_RWLOCK_unlock(&file_entry->attr_lock);
	}

	/* Convert it to a file handle */
	if (!nfs4_FSALToFhandle(&data->currentFH,
				file_entry->obj_handle,
				op_ctx->export)) {
		res_LOOKUP4->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/* Keep the pointer within the compound data */
	set_current_entry(data, file_entry, false);
	file_entry = NULL;

	/* Return successfully */
	res_LOOKUP4->status = NFS4_OK;

 out:

	/* Release reference on file_entry if we didn't utilze it. */
	if (file_entry)
		cache_inode_put(file_entry);

	if (name)
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
	return;
}				/* nfs4_op_lookup_Free */
