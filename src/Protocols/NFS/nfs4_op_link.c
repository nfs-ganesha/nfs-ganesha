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
 * \file    nfs4_op_link.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"

/**
 * @brief The NFS4_OP_LINK operation.
 *
 * This functions handles the NFS4_OP_LINK operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 367
 */

int nfs4_op_link(struct nfs_argop4 *op, compound_data_t *data,
		 struct nfs_resop4 *resp)
{
	LINK4args * const arg_LINK4 = &op->nfs_argop4_u.oplink;
	LINK4res * const res_LINK4 = &resp->nfs_resop4_u.oplink;
	cache_entry_t *dir_entry = NULL;
	cache_entry_t *file_entry = NULL;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	char *newname = NULL;

	resp->resop = NFS4_OP_LINK;
	res_LINK4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_LINK4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	res_LINK4->status = nfs4_sanity_check_saved_FH(data, -DIRECTORY, false);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	/*
	 * This operation creates a hard link, for the file
	 * represented by the saved FH, in directory represented by
	 * currentFH under the name arg_LINK4.target
	 */

	/* Validate and convert the UFT8 objname to a regular string */
	res_LINK4->status = nfs4_utf8string2dynamic(&arg_LINK4->newname,
						    UTF8_SCAN_ALL,
						    &newname);

	if (res_LINK4->status != NFS4_OK)
		goto out;

	/* get info from compound data */
	dir_entry = data->current_entry;

	res_LINK4->LINK4res_u.resok4.cinfo.before =
	    cache_inode_get_changeid4(dir_entry);

	file_entry = data->saved_entry;

	/* make the link */
	cache_status =
	    cache_inode_link(file_entry, dir_entry, newname);

	if (cache_status != CACHE_INODE_SUCCESS) {
		res_LINK4->status = nfs4_Errno(cache_status);
		goto out;
	}

	res_LINK4->LINK4res_u.resok4.cinfo.after =
	    cache_inode_get_changeid4(dir_entry);
	res_LINK4->LINK4res_u.resok4.cinfo.atomic = FALSE;

	res_LINK4->status = NFS4_OK;

 out:

	if (newname)
		gsh_free(newname);

	return res_LINK4->status;
}				/* nfs4_op_link */

/**
 * @brief Free memory allocated for LINK result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LINK operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_link_Free(nfs_resop4 *resp)
{
	return;
}				/* nfs4_op_link_Free */
