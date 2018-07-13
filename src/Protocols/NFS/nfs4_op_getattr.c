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
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

static inline bool check_fs_locations(struct fsal_obj_handle *obj)
{
	fsal_status_t st;
	fs_locations4 fs_locs;
	fs_location4 fs_loc;
	component4 fs_server;
	char server[MAXHOSTNAMELEN];

	nfs4_pathname4_alloc(&fs_locs.fs_root, NULL);
	fs_server.utf8string_len = sizeof(server);
	fs_server.utf8string_val = server;
	fs_loc.server.server_len = 1;
	fs_loc.server.server_val = &fs_server;
	nfs4_pathname4_alloc(&fs_loc.rootpath, NULL);
	fs_locs.locations.locations_len = 1;
	fs_locs.locations.locations_val = &fs_loc;

	/* For now allow for one fs locations, fs_locations() should set:
	   root and update its length, can not be bigger than MAXPATHLEN
	   path and update its length, can not be bigger than MAXPATHLEN
	   server and update its length, can not be bigger than MAXHOSTNAMELEN
	*/
	st = obj->obj_ops->fs_locations(obj, &fs_locs);

	nfs4_pathname4_free(&fs_locs.fs_root);
	nfs4_pathname4_free(&fs_loc.rootpath);

	return !FSAL_IS_ERROR(st);
}

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
int nfs4_op_getattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	GETATTR4args * const arg_GETATTR4 = &op->nfs_argop4_u.opgetattr;
	GETATTR4res * const res_GETATTR4 = &resp->nfs_resop4_u.opgetattr;
	attrmask_t mask;
	struct attrlist attrs;

	/* This is a NFS4_OP_GETTAR */
	resp->resop = NFS4_OP_GETATTR;
	res_GETATTR4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_GETATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETATTR4->status != NFS4_OK)
		return res_GETATTR4->status;

	/* Sanity check: if no attributes are wanted, nothing is to be
	 * done.  In this case NFS4_OK is to be returned */
	if (arg_GETATTR4->attr_request.bitmap4_len == 0) {
		res_GETATTR4->status = NFS4_OK;
		return res_GETATTR4->status;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access_Bitmap(&arg_GETATTR4->attr_request,
					    FATTR4_ATTR_READ)) {
		res_GETATTR4->status = NFS4ERR_INVAL;
		return res_GETATTR4->status;
	}

	res_GETATTR4->status =
	    bitmap4_to_attrmask_t(&arg_GETATTR4->attr_request, &mask);

	if (res_GETATTR4->status != NFS4_OK)
		return res_GETATTR4->status;

	/* Add mode to what we actually ask for so we can do fslocations
	 * test.
	 */
	fsal_prepare_attrs(&attrs, mask | ATTR_MODE);

	nfs4_bitmap4_Remove_Unsupported(&arg_GETATTR4->attr_request);

	res_GETATTR4->status = file_To_Fattr(
			data, mask, &attrs,
			&res_GETATTR4->GETATTR4res_u.resok4.obj_attributes,
			&arg_GETATTR4->attr_request);

	if (data->current_obj->type == DIRECTORY &&
	    is_sticky_bit_set(data->current_obj, &attrs) &&
	    !attribute_is_set(&arg_GETATTR4->attr_request,
			      FATTR4_FS_LOCATIONS) &&
	    check_fs_locations(data->current_obj))
		res_GETATTR4->status = NFS4ERR_MOVED;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return res_GETATTR4->status;
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
