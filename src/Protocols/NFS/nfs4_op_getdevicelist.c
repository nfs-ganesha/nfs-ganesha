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
 * @file    nfs4_op_getdevicelist.c
 * @brief   Routines used for managing the NFS4_OP_GETDEVICELIST operation.
 *
 * Routines used for managing the GETDEVICELIST operation.
 *
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "export_mgr.h"

struct cb_data {
	deviceid4 *buffer;
	size_t count;
	size_t max;
	uint64_t swexport;
};

static bool cb(void *opaque, uint64_t id)
{
	struct cb_data *data = (struct cb_data *)opaque;

	if (data->count > data->max)
		return false;

	*((uint64_t *) data->buffer[data->count]) = data->swexport;
	*((uint64_t *) (data->buffer[data->count] + sizeof(uint64_t)))
	    = nfs_htonl64(id);
	++data->count;

	return true;
}

/**
 *
 * @brief The NFS4_OP_GETDEVICELIST operation.
 *
 * This function returns a list of pNFS devices for a given
 * filesystem.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 p. 365
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_getdevicelist(struct nfs_argop4 *op, compound_data_t *data,
			  struct nfs_resop4 *resp)
{
	/* Convenience alias for arguments */
	GETDEVICELIST4args * const arg_GETDEVICELIST4 =
	    &op->nfs_argop4_u.opgetdevicelist;
	/* Convenience alias for response */
	GETDEVICELIST4res * const res_GETDEVICELIST4 =
	    &resp->nfs_resop4_u.opgetdevicelist;
	/* NFS4 return code */
	nfsstat4 nfs_status = 0;
	/* Input/output and output parameters of FSAL function */
	struct fsal_getdevicelist_res res;
	/* Structure for callback */
	struct cb_data cb_opaque;

	resp->resop = NFS4_OP_GETDEVICELIST;

	if (data->minorversion == 0)
		return res_GETDEVICELIST4->gdlr_status = NFS4ERR_INVAL;

	nfs_status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (nfs_status != NFS4_OK)
		goto out;

	memset(&res, 0, sizeof(struct fsal_getdevicelist_res));

	res.cookie = arg_GETDEVICELIST4->gdla_cookie;
	memcpy(&res.cookieverf,
	       arg_GETDEVICELIST4->gdla_cookieverf,
	       NFS4_VERIFIER_SIZE);

	cb_opaque.count = 0;
	cb_opaque.max = 32;
	cb_opaque.swexport = nfs_htonl64(op_ctx->export->export_id);

	res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.
	     gdlr_deviceid_list.gdlr_deviceid_list_val =
	     gsh_malloc(cb_opaque.max * sizeof(deviceid4));

	if (res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.
	     gdlr_deviceid_list.gdlr_deviceid_list_val == NULL) {
		nfs_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	cb_opaque.buffer =
	    res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.
	    gdlr_deviceid_list.gdlr_deviceid_list_val;

	nfs_status = op_ctx->fsal_export->ops->getdevicelist(
					op_ctx->fsal_export,
					arg_GETDEVICELIST4->gdla_layout_type,
					&cb_opaque, cb,
					&res);

	if (nfs_status != NFS4_OK) {
		gsh_free(cb_opaque.buffer);
		goto out;
	}

	res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookie =
	    res.cookie;

	memcpy(res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.
	       gdlr_cookieverf, &res.cookieverf,
	       NFS4_VERIFIER_SIZE);

	res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.gdlr_deviceid_list.
	    gdlr_deviceid_list_len = cb_opaque.count;

	res_GETDEVICELIST4->GETDEVICELIST4res_u.gdlr_resok4.gdlr_eof = res.eof;

	nfs_status = NFS4_OK;

 out:
	res_GETDEVICELIST4->gdlr_status = nfs_status;

	return res_GETDEVICELIST4->gdlr_status;
}

/**
 * @brief Free memory allocated for GETDEVICELIST result
 *
 * This function frees the memory allocates for the result of the
 * NFS4_OP_GETDEVICELIST operation.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs4_op_getdevicelist_Free(nfs_resop4 *res)
{
	GETDEVICELIST4res *resp = &res->nfs_resop4_u.opgetdevicelist;

	if (resp->gdlr_status == NFS4_OK) {
		gsh_free(resp->GETDEVICELIST4res_u.gdlr_resok4.
			 gdlr_deviceid_list.gdlr_deviceid_list_val);
	}
}				/* nfs41_op_getdevicelist_Free */
