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
 * @file nfs4_op_setattr.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "fsal.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "sal_functions.h"
#include "nfs_creds.h"

/**
 * @brief The NFS4_OP_SETATTR operation.
 *
 * This functions handles the NFS4_OP_SETATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */

enum nfs_req_result nfs4_op_setattr(struct nfs_argop4 *op,
				    compound_data_t *data,
				    struct nfs_resop4 *resp)
{
	SETATTR4args * const arg_SETATTR4 = &op->nfs_argop4_u.opsetattr;
	SETATTR4res * const res_SETATTR4 = &resp->nfs_resop4_u.opsetattr;
	struct fsal_attrlist sattr;
	fsal_status_t fsal_status = {0, 0};
	const char *tag = "SETATTR";
	state_t *state_found = NULL;
	state_t *state_open = NULL;
	const time_t S_NSECS = 1000000000UL;

	resp->resop = NFS4_OP_SETATTR;
	res_SETATTR4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_SETATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_SETATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (!nfs_get_grace_status(false)) {
		res_SETATTR4->status = NFS4ERR_GRACE;
		return NFS_REQ_ERROR;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access
	    (&arg_SETATTR4->obj_attributes, FATTR4_ATTR_WRITE)) {
		res_SETATTR4->status = NFS4ERR_INVAL;
		goto done;
	}

	/* Ask only for supported attributes */
	if (!nfs4_Fattr_Supported(&arg_SETATTR4->obj_attributes)) {
		res_SETATTR4->status = NFS4ERR_ATTRNOTSUPP;
		goto done;
	}

	/* Convert the fattr4 in the request to a fsal sattr structure */
	res_SETATTR4->status =
		nfs4_Fattr_To_FSAL_attr(&sattr,
					&arg_SETATTR4->obj_attributes,
					data);

	if (res_SETATTR4->status != NFS4_OK)
		goto done;

	/* Trunc may change Xtime so we have to start with trunc and
	 * finish by the mtime and atime
	 */
	if ((FSAL_TEST_MASK(sattr.valid_mask, ATTR_SIZE))
	     || (FSAL_TEST_MASK(sattr.valid_mask, ATTR4_SPACE_RESERVED))) {
		/* Setting the size of a directory is prohibited */
		if (data->current_filetype == DIRECTORY) {
			res_SETATTR4->status = NFS4ERR_ISDIR;
			goto done;
		}

		/* Object should be a file */
		if (data->current_obj->type != REGULAR_FILE) {
			res_SETATTR4->status = NFS4ERR_INVAL;
			goto done;
		}

		/* Check stateid correctness and get pointer to state */
		res_SETATTR4->status =
		    nfs4_Check_Stateid(&arg_SETATTR4->stateid,
				       data->current_obj,
				       &state_found,
				       data,
				       STATEID_SPECIAL_ANY,
				       0,
				       false,
				       tag);

		if (res_SETATTR4->status != NFS4_OK)
			goto done;

		/* NB: After this point, if state_found == NULL, then
		 * the stateid is all-0 or all-1
		 */
		if (state_found != NULL) {
			switch (state_found->state_type) {
			case STATE_TYPE_SHARE:
				state_open = state_found;
				/* Note this causes an extra refcount, but it
				 * simplifies logic below.
				 */
				inc_state_t_ref(state_open);
				break;

			case STATE_TYPE_LOCK:
				state_open =
				    state_found->state_data.lock.openstate;
				inc_state_t_ref(state_open);
				break;

			case STATE_TYPE_DELEG:
				state_open = NULL;
				break;

			default:
				res_SETATTR4->status = NFS4ERR_BAD_STATEID;
				goto done;
			}

			/* This is a size operation, this means that
			 * the file MUST have been opened for writing
			 */
			if (state_open != NULL &&
			    (state_open->state_data.share.share_access &
			     OPEN4_SHARE_ACCESS_WRITE) == 0) {
				/* Bad open mode, return NFS4ERR_OPENMODE */
				res_SETATTR4->status = NFS4ERR_OPENMODE;
				goto done;
			}
		} else {
			/* Special stateid, no open state, check to
			 * see if any share conflicts
			 */
			state_open = NULL;
		}
	}

	/* Set the atime and mtime (ctime is not setable) */

	/* A carry into seconds considered invalid */
	if (sattr.atime.tv_nsec >= S_NSECS) {
		res_SETATTR4->status = NFS4ERR_INVAL;
		goto done;
	}

	if (sattr.mtime.tv_nsec >= S_NSECS) {
		res_SETATTR4->status = NFS4ERR_INVAL;
		goto done;
	}

	/* If owner or owner_group are set, and the credential was
	 * squashed, then we must squash the set owner and owner_group.
	 */
	squash_setattr(&sattr);

	/* If a SETATTR comes with an open stateid, and size is being
	 * set, then the open MUST be for write (checked above), so
	 * is_open_write is simple at this stage, it's just a check that
	 * we have an open owner.
	 */
	fsal_status = fsal_setattr(data->current_obj,
				   false,
				   state_found,
				   &sattr);

	/* Release the attributes (may release an explicit or inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_SETATTR4->status = nfs4_Errno_status(fsal_status);
		goto done;
	}

	/* Set the replyed structure */
	res_SETATTR4->attrsset = arg_SETATTR4->obj_attributes.attrmask;

	/* Exit with no error */
	res_SETATTR4->status = NFS4_OK;

 done:
	nfs_put_grace_status();

	if (state_found != NULL)
		dec_state_t_ref(state_found);

	if (state_open != NULL)
		dec_state_t_ref(state_open);

	return nfsstat4_to_nfs_req_result(res_SETATTR4->status);
}				/* nfs4_op_setattr */

/**
 * @brief Free memory allocated for SETATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_SETATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_setattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
