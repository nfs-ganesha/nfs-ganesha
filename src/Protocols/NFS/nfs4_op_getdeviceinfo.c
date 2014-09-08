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
 * @file    nfs4_op_getdeviceinfo.c
 * @brief   Routines used for managing the NFS4_OP_GETDEVICEINFO operation.
 *
 * Routines used for managing the GETDEVICEINFO operation.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "fsal_pnfs.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 *
 * @brief The NFS4_OP_GETDEVICEINFO operation.
 *
 * This function returns information on a pNFS device.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 p. 365-6
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_getdeviceinfo(struct nfs_argop4 *op, compound_data_t *data,
			  struct nfs_resop4 *resp)
{
	/* Convenience alias for arguments */
	GETDEVICEINFO4args * const arg_GETDEVICEINFO4 =
	    &op->nfs_argop4_u.opgetdeviceinfo;
	/* Convenience alias for response */
	GETDEVICEINFO4res * const res_GETDEVICEINFO4 =
	    &resp->nfs_resop4_u.opgetdeviceinfo;
	/* The separated deviceid passed to the FSAL */
	struct pnfs_deviceid *deviceid;
	/* NFS4 return code */
	nfsstat4 nfs_status = 0;
	/* XDR stream into which the FSAl shall encode the da_addr_body */
	XDR da_addr_body;
	/* The position before any bytes are sent to the stream */
	size_t da_beginning = 0;
	/* The total length of the XDR-encoded da_addr_body */
	size_t da_length = 0;
	/* Address of the buffer that backs the stream */
	char *da_buffer = NULL;
	/* The space necessary to hold one response */
	count4 mincount = 0;
	/* The FSAL's requested size for the da_addr_body opaque */
	size_t da_addr_size = 0;
	/* Pointer to the fsal appropriate to this deviceid */
	struct fsal_module *fsal = NULL;

	resp->resop = NFS4_OP_GETDEVICEINFO;

	if (data->minorversion == 0)
		return res_GETDEVICEINFO4->gdir_status = NFS4ERR_INVAL;

	/* Overlay Ganesha's pnfs_deviceid on arg */
	deviceid = (struct pnfs_deviceid *) arg_GETDEVICEINFO4->gdia_device_id;

	if (deviceid->fsal_id >= FSAL_ID_COUNT) {
		LogInfo(COMPONENT_PNFS,
			"GETDEVICEINFO with invalid fsal id %0hhx",
			deviceid->fsal_id);
		return res_GETDEVICEINFO4->gdir_status = NFS4ERR_INVAL;
	}

	fsal = pnfs_fsal[deviceid->fsal_id];

	if (fsal == NULL) {
		LogInfo(COMPONENT_PNFS,
			"GETDEVICEINFO with inactive fsal id %0hhx",
			deviceid->fsal_id);
		return res_GETDEVICEINFO4->gdir_status = NFS4ERR_INVAL;
	}

	/* Check that we have space */

	mincount = sizeof(uint32_t) +	/* Count for the empty bitmap */
	    sizeof(layouttype4) +	/* Type in the device_addr4 */
	    sizeof(uint32_t);	/* Number of bytes in da_addr_body */

	da_addr_size = MIN(fsal->ops->fs_da_addr_size(fsal),
			   arg_GETDEVICEINFO4->gdia_maxcount - mincount);

	if (da_addr_size == 0) {
		LogCrit(COMPONENT_PNFS,
			"The FSAL must specify a non-zero da_addr size.");
		nfs_status = NFS4ERR_NOENT;
		goto out;
	}

	/* Set up the device_addr4 and get stream for FSAL to write into */

	res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.
	    da_layout_type = arg_GETDEVICEINFO4->gdia_layout_type;

	da_buffer = gsh_malloc(da_addr_size);

	if (da_buffer == NULL) {
		nfs_status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	xdrmem_create(&da_addr_body, da_buffer, da_addr_size, XDR_ENCODE);

	da_beginning = xdr_getpos(&da_addr_body);

	nfs_status = fsal->ops->getdeviceinfo(
			fsal,
			&da_addr_body,
			arg_GETDEVICEINFO4->gdia_layout_type,
			deviceid);

	da_length = xdr_getpos(&da_addr_body) - da_beginning;

	xdr_destroy(&da_addr_body);

	if (nfs_status != NFS4_OK)
		goto out;

	memset(&res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4.
	       gdir_notification, 0,
	       sizeof(res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4.
		      gdir_notification));

	res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.
	    da_addr_body.da_addr_body_len = da_length;
	res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.
	    da_addr_body.da_addr_body_val = da_buffer;

	nfs_status = NFS4_OK;

 out:

	if ((nfs_status != NFS4_OK) && da_buffer)
		gsh_free(da_buffer);

	res_GETDEVICEINFO4->gdir_status = nfs_status;

	return res_GETDEVICEINFO4->gdir_status;
}				/* nfs41_op_getdeviceinfo */

/**
 * @brief Free memory allocated for GETDEVICEINFO result
 *
 * This function frees memory allocated for the result of an
 * NFS4_OP_GETDEVICEINFO response.
 *
 * @param[in,out] resp  Results for nfs4_op
 *
 */
void nfs4_op_getdeviceinfo_Free(nfs_resop4 *res)
{
	GETDEVICEINFO4res *resp = &res->nfs_resop4_u.opgetdeviceinfo;

	if (resp->gdir_status == NFS4_OK) {
		if (resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.
		    da_addr_body.da_addr_body_val != NULL) {
			gsh_free(resp->GETDEVICEINFO4res_u.gdir_resok4.
				 gdir_device_addr.da_addr_body.
				 da_addr_body_val);
		}
	}
}				/* nfs41_op_getdeviceinfo_Free */
