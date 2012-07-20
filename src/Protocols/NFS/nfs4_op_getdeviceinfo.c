/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "fsal_pnfs.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"

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

int nfs4_op_getdeviceinfo(struct nfs_argop4 *op,
                          compound_data_t *data,
                          struct nfs_resop4 *resp)
{
        /* Convenience alias for arguments */
        GETDEVICEINFO4args *const arg_GETDEVICEINFO4
                = &op->nfs_argop4_u.opgetdeviceinfo;
        /* Convenience alias for response */
        GETDEVICEINFO4res *const res_GETDEVICEINFO4
                = &resp->nfs_resop4_u.opgetdeviceinfo;
        /* The separated deviceid passed to the FSAL */
        struct pnfs_deviceid deviceid = {0, 0};
        /* NFS4 return code */
        nfsstat4 nfs_status = 0;
        /* XDR stream into which the FSAl shall encode the da_addr_body */
        XDR da_addr_body;
        /* The position before any bytes are sent to the stream */
        size_t da_beginning = 0;
     /* The total length of the XDR-encoded da_addr_body */
        size_t da_length = 0;
        /* Address of the buffer that backs the stream */
        char* da_buffer = NULL;
        /* The space necessary to hold one response */
        count4 mincount = 0;
        /* The FSAL's requested size for the da_addr_body opaque */
        size_t da_addr_size = 0;
        /* Pointer to the export appropriate to this deviceid */
        exportlist_t *export = NULL;

        resp->resop = NFS4_OP_GETDEVICEINFO;

        if (data->minorversion == 0) {
                return (res_GETDEVICEINFO4->gdir_status = NFS4ERR_INVAL);
        }

        /* Disassemble and fix byte order of the deviceid halves.  Do
           memcpy then byte swap to avoid potential problems with
           unaligned/misaligned access. */

        memcpy(&deviceid.export_id, arg_GETDEVICEINFO4->gdia_device_id,
               sizeof(uint64_t));
        deviceid.export_id = nfs_ntohl64(deviceid.export_id);

        memcpy(&deviceid.devid,
               arg_GETDEVICEINFO4->gdia_device_id + sizeof(uint64_t),
               sizeof(uint64_t));
        deviceid.devid = nfs_ntohl64(deviceid.devid);

        /* Check that we have space */

        export = nfs_Get_export_by_id(data->pfullexportlist,
                                      deviceid.export_id);

        if (export == NULL) {
                nfs_status = NFS4ERR_NOENT;
                goto out;
        }

        if (!nfs4_pnfs_supported(export)) {
                nfs_status = NFS4ERR_NOENT;
                goto out;
        }

        da_addr_size
                = export->export_hdl->ops
                ->fs_da_addr_size(export->export_hdl);

        if (da_addr_size == 0) {
                LogCrit(COMPONENT_PNFS,
                        "The FSAL must specify a non-zero da_addr size.");
                nfs_status = NFS4ERR_SERVERFAULT;
                goto out;
        }

        mincount = sizeof(uint32_t) /* Count for the empty bitmap */ +
                sizeof(layouttype4) /* Type in the device_addr4 */ +
                sizeof(uint32_t) /* Number of bytes in da_addr_body */ +
                da_addr_size; /* The FSAL's requested size of the
                                 da_addr_body opaque */

        if (arg_GETDEVICEINFO4->gdia_maxcount < mincount) {
                nfs_status = NFS4ERR_TOOSMALL;
                res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_mincount
                        = mincount;
                goto out;
        }

        /* Set up the device_addr4 and get stream for FSAL to write into */

        res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4
                .gdir_device_addr.da_layout_type
                = arg_GETDEVICEINFO4->gdia_layout_type;

        if ((da_buffer = gsh_malloc(da_addr_size)) == NULL) {
                nfs_status = NFS4ERR_SERVERFAULT;
                goto out;
        }

        xdrmem_create(&da_addr_body,
                      da_buffer,
                      da_addr_size,
                      XDR_ENCODE);
        da_beginning = xdr_getpos(&da_addr_body);

        nfs_status = (export->export_hdl->ops
                      ->getdeviceinfo(export->export_hdl,
                                      &da_addr_body,
                                      arg_GETDEVICEINFO4->gdia_layout_type,
                                      &deviceid));

        da_length = xdr_getpos(&da_addr_body) - da_beginning;
        xdr_destroy(&da_addr_body);

        if (nfs_status != NFS4_OK) {
                goto out;
        }

        res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4
                .gdir_notification.bitmap4_len = 0;
        res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4
                .gdir_notification.bitmap4_val = NULL;

        res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4
                .gdir_device_addr.da_addr_body.da_addr_body_len
                = da_length;
        res_GETDEVICEINFO4->GETDEVICEINFO4res_u.gdir_resok4
                .gdir_device_addr.da_addr_body.da_addr_body_val
                = da_buffer;

        nfs_status = NFS4_OK;

out:

        if ((nfs_status != NFS4_OK) &&
            da_buffer) {
                gsh_free(da_buffer);
        }

        res_GETDEVICEINFO4->gdir_status = nfs_status;

        return res_GETDEVICEINFO4->gdir_status;
} /* nfs41_op_getdeviceinfo */

/**
 * @brief Free memory allocated for GETDEVICEINFO result
 *
 * This function frees memory allocated for the result of an
 * NFS4_OP_GETDEVICEINFO response.
 *
 * @param[in,out] resp  Results for nfs4_op
 *
 */
void nfs4_op_getdeviceinfo_Free(GETDEVICEINFO4res *resp)
{
        if (resp->gdir_status == NFS4_OK) {
                if (resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr
                    .da_addr_body.da_addr_body_val != NULL) {
                        gsh_free(resp->GETDEVICEINFO4res_u.gdir_resok4
                                 .gdir_device_addr.da_addr_body
                                 .da_addr_body_val);
                }
        }
        return;
} /* nfs41_op_getdeviceinfo_Free */
