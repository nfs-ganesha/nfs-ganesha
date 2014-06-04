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
 * \file    nfs4_op_remove.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

/**
 * @brief The NFS4_OP_REMOVE operation.
 *
 * This function implements the NFS4_OP_REMOVE operation in
 * NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 372-3
 */

int nfs4_op_remove(struct nfs_argop4 *op, compound_data_t *data,
		   struct nfs_resop4 *resp)
{
	REMOVE4args * const arg_REMOVE4 = &op->nfs_argop4_u.opremove;
	REMOVE4res * const res_REMOVE4 = &resp->nfs_resop4_u.opremove;
	cache_entry_t *parent_entry = NULL;
	char *name = NULL;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

	resp->resop = NFS4_OP_REMOVE;
	res_REMOVE4->status = NFS4_OK;

	/* Do basic checks on a filehandle
	 * Delete arg_REMOVE4.target in directory pointed by currentFH
	 * Make sure the currentFH is pointed a directory
	 */
	res_REMOVE4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_REMOVE4->status != NFS4_OK)
		goto out;

	/* Validate and convert the UFT8 target to a regular string */
	res_REMOVE4->status =
	    nfs4_utf8string2dynamic(&arg_REMOVE4->target, UTF8_SCAN_ALL, &name);

	if (res_REMOVE4->status != NFS4_OK)
		goto out;

	if (nfs_in_grace()) {
		res_REMOVE4->status = NFS4ERR_GRACE;
		goto out;
	}

	/* Get the parent entry (aka the current one in the compound data) */
	parent_entry = data->current_entry;

	/* We have to keep track of the 'change' file attribute
	 * for reply structure
	 */
	memset(&res_REMOVE4->REMOVE4res_u.resok4.cinfo.before,
	       0,
	       sizeof(changeid4));

	res_REMOVE4->REMOVE4res_u.resok4.cinfo.before =
	    cache_inode_get_changeid4(parent_entry);

	cache_status = cache_inode_remove(parent_entry, name);

	if (cache_status != CACHE_INODE_SUCCESS) {
		res_REMOVE4->status = nfs4_Errno(cache_status);
		goto out;
	}

	res_REMOVE4->REMOVE4res_u.resok4.cinfo.after =
	    cache_inode_get_changeid4(parent_entry);

	/* Operation was not atomic .... */
	res_REMOVE4->REMOVE4res_u.resok4.cinfo.atomic = FALSE;

	/* If you reach this point, everything was ok */

	res_REMOVE4->status = NFS4_OK;

 out:

	if (name)
		gsh_free(name);

	return res_REMOVE4->status;
}				/* nfs4_op_remove */

/**
 * @brief Free memory allocated for REMOVE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_REMOVE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_remove_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
	return;
}				/* nfs4_op_remove_Free */
