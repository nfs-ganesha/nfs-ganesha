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
 * --------------------------------------- */
/**
 * @file    nfs4_op_getattr.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "sal_functions.h"

/**
 * @brief Gets attributes for an entry in the FSAL.
 *
 * Impelments the NFS4_OP_GETATTR operation, which gets attributes for
 * an entry in the FSAL.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 365
 *
 */
enum nfs_req_result nfs4_op_getattr(struct nfs_argop4 *op,
				    compound_data_t *data,
				    struct nfs_resop4 *resp)
{
	GETATTR4args * const arg_GETATTR4 = &op->nfs_argop4_u.opgetattr;
	GETATTR4res * const res_GETATTR4 = &resp->nfs_resop4_u.opgetattr;
	attrmask_t mask;
	struct fsal_attrlist attrs;
	bool current_obj_is_referral = false;
	fattr4 *obj_attributes =
		&res_GETATTR4->GETATTR4res_u.resok4.obj_attributes;
	nfs_client_id_t *deleg_client = NULL;
	struct fsal_obj_handle *obj = data->current_obj;

	/* This is a NFS4_OP_GETTAR */
	resp->resop = NFS4_OP_GETATTR;

	/* Do basic checks on a filehandle */
	res_GETATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETATTR4->status != NFS4_OK)
		goto out;

	/* Sanity check: if no attributes are wanted, nothing is to be
	 * done.  In this case NFS4_OK is to be returned */
	if (arg_GETATTR4->attr_request.bitmap4_len == 0) {
		res_GETATTR4->status = NFS4_OK;
		goto out;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access_Bitmap(&arg_GETATTR4->attr_request,
					    FATTR4_ATTR_READ)) {
		res_GETATTR4->status = NFS4ERR_INVAL;
		goto out;
	}

	res_GETATTR4->status =
	    bitmap4_to_attrmask_t(&arg_GETATTR4->attr_request, &mask);

	if (res_GETATTR4->status != NFS4_OK)
		goto out;

	/* Add mode to what we actually ask for so we can do fslocations
	 * test.
	 */
	fsal_prepare_attrs(&attrs, mask | ATTR_MODE);

	nfs4_bitmap4_Remove_Unsupported(&arg_GETATTR4->attr_request);

	if (obj->type == REGULAR_FILE) {
		/* As per rfc 7530, section:10.4.3
		 * The server needs to employ special handling for a GETATTR
		 * where the target is a file that has an OPEN_DELEGATE_WRITE
		 * delegation in effect.
		 *
		 * The server may use CB_GETATTR to fetch the right attributes
		 * from the client holding the delegation or may simply recall
		 * the delegation. Till then send EDELAY error.
		 */
		STATELOCK_lock(obj);

		if (is_write_delegated(obj, &deleg_client) &&
		    deleg_client &&
		    (deleg_client->gsh_client != op_ctx->client)) {
			res_GETATTR4->status =
					handle_deleg_getattr(obj, deleg_client);

			if (res_GETATTR4->status != NFS4_OK) {
				STATELOCK_unlock(obj);
				goto out;
			} else {
				cbgetattr_t *cbgetattr = NULL;

				/* CB_GETATTR response handler must have updated
				 * the attributes in md-cache. reset cbgetattr
				 * state and fall through. st_lock is held till
				 * we finish ending response*/
				cbgetattr = &obj->state_hdl->file.cbgetattr;
				cbgetattr->state = CB_GETATTR_NONE;
			}
		}

		/* release st_lock */
		STATELOCK_unlock(obj);
	}

	res_GETATTR4->status = file_To_Fattr(
			data, mask, &attrs,
			obj_attributes,
			&arg_GETATTR4->attr_request);

	current_obj_is_referral = obj->obj_ops->is_referral(
					obj, &attrs, false);

	/*
	 * If it is a referral point, return the FATTR4_RDATTR_ERROR if
	 * requested along with the requested restricted attrs.
	 */
	if (res_GETATTR4->status == NFS4_OK &&
	    current_obj_is_referral) {
		bool fill_rdattr_error = true;
		bool fslocations_requested = attribute_is_set(
						&arg_GETATTR4->attr_request,
						FATTR4_FS_LOCATIONS);

		if (!fslocations_requested) {
			if (!attribute_is_set(&arg_GETATTR4->attr_request,
						FATTR4_RDATTR_ERROR)) {
				fill_rdattr_error = false;
			}
		}

		if (fill_rdattr_error) {
			struct xdr_attrs_args args;

			memset(&args, 0, sizeof(args));
			args.attrs = &attrs;
			args.fsid = data->current_obj->fsid;
			get_mounted_on_fileid(data, &args.mounted_on_fileid);

			if (nfs4_Fattr_Fill_Error(data, obj_attributes,
						  NFS4ERR_MOVED,
						  &arg_GETATTR4->attr_request,
						  &args)
			    != 0) {
				/* Report an error. */
				res_GETATTR4->status = NFS4ERR_SERVERFAULT;
			}
		} else {
			/* Report the referral. */
			res_GETATTR4->status = NFS4ERR_MOVED;
		}
	}

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	if (res_GETATTR4->status == NFS4_OK) {
		/* Fill in and check response size and make sure it fits. */
		data->op_resp_size = sizeof(nfsstat4) +
			res_GETATTR4->GETATTR4res_u.resok4.obj_attributes
			.attr_vals.attrlist4_len;

		res_GETATTR4->status =
			check_resp_room(data, data->op_resp_size);
	}

out:

	if (deleg_client)
		dec_client_id_ref(deleg_client);

	if (res_GETATTR4->status != NFS4_OK) {
		/* The attributes that may have been allocated will not be
		 * consumed. Since the response array was allocated with
		 * gsh_calloc, the buffer pointer is always NULL or valid.
		 */
		nfs4_Fattr_Free(obj_attributes);

		/* Indicate the failed response size. */
		data->op_resp_size = sizeof(nfsstat4);
	}

	return nfsstat4_to_nfs_req_result(res_GETATTR4->status);
}				/* nfs4_op_getattr */

/**
 * @brief Free memory allocated for GETATTR result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_GETATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_getattr_Free(nfs_resop4 *res)
{
	GETATTR4res *resp = &res->nfs_resop4_u.opgetattr;

	if (resp->status == NFS4_OK)
		nfs4_Fattr_Free(&resp->GETATTR4res_u.resok4.obj_attributes);
}				/* nfs4_op_getattr_Free */
