/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs41_op_getdeviceinfo.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_GETDEVICEINFO operation.
 *
 * nfs41_op_getdeviceinfo.c :  Routines used for managing the GETDEVICEINFO operation.
 *
 *
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
#ifdef _PNFS_MDS
#include "fsal_pnfs.h"
#endif /* _PNFS_MDS */
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 *
 * \brief The NFS4_OP_GETDEVICEINFO operation.
 *
 * Gets the list of pNFS devices
 *
 * \param op    [IN]    pointer to nfs4_op arguments
 * \param data  [INOUT] Pointer to the compound request's data
 * \param resp  [IN]    Pointer to nfs4_op results
 *
 * \return NFS4_OK if successfull, other values show an error.
 *
 * \see nfs4_Compound
 *
 */

#define arg_GETDEVICEINFO4  op->nfs_argop4_u.opgetdeviceinfo
#define res_GETDEVICEINFO4  resp->nfs_resop4_u.opgetdeviceinfo

int nfs41_op_getdeviceinfo(struct nfs_argop4 *op,
                           compound_data_t *data,
                           struct nfs_resop4 *resp)
{
     char __attribute__ ((__unused__)) funcname[] = "nfs41_op_getdeviceinfo";
#ifdef _PNFS_MDS
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
#endif /* _PNFS_MDS */

     resp->resop = NFS4_OP_GETDEVICEINFO;

#ifdef _PNFS_MDS

     /* Disassemble and fix byte order of the deviceid halves.  Do
        memcpy then byte swap to avoid potential problems with
        unaligned/misaligned access. */

     memcpy(&deviceid.export_id, arg_GETDEVICEINFO4.gdia_device_id,
            sizeof(uint64_t));
     deviceid.export_id = nfs_ntohl64(deviceid.export_id);

     memcpy(&deviceid.devid,
            arg_GETDEVICEINFO4.gdia_device_id + sizeof(uint64_t),
            sizeof(uint64_t));
     deviceid.devid = nfs_ntohl64(deviceid.export_id);

     /* Check that we have space */

     export = nfs_Get_export_by_id(data->pexportlist,
                                   deviceid.export_id);

     if (export == NULL) {
          nfs_status = NFS4ERR_NOENT;
          goto out;
     }

     if (!nfs4_pnfs_supported(export)) {
          nfs_status = NFS4ERR_NOENT;
          goto out;
     }

     da_addr_size = (export->FS_export_context.fe_static_fs_info
                     ->dsaddr_buffer_size);

     if (da_addr_size == 0) {
          LogCrit(COMPONENT_PNFS,
                  "The FSAL must specify a non-zero dsaddr_buffer_size "
                  "in its fsal_staticfsinfo_t");
          nfs_status = NFS4ERR_SERVERFAULT;
          goto out;
     }

     mincount = sizeof(uint32_t) /* Count for the empty bitmap */ +
          sizeof(layouttype4) /* Type in the device_addr4 */ +
          sizeof(uint32_t) /* Number of bytes in da_addr_body */ +
          da_addr_size; /* The FSAL's requested size of the
                           da_addr_body opaque */

     if (arg_GETDEVICEINFO4.gdia_maxcount < mincount) {
          nfs_status = NFS4ERR_TOOSMALL;
          res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_mincount
               = mincount;
          goto out;
     }

     /* Set up the device_addr4 and get stream for FSAL to write into */

     res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_layout_type
          = arg_GETDEVICEINFO4.gdia_layout_type;

     if ((da_buffer = gsh_malloc(da_addr_size)) == NULL) {
          nfs_status = NFS4ERR_SERVERFAULT;
          goto out;
     }

     xdrmem_create(&da_addr_body,
                   da_buffer,
                   da_addr_size,
                   XDR_ENCODE);
     da_beginning = xdr_getpos(&da_addr_body);

     /*
      * XXX This assumes a single FSAL and must be changed after the
      * XXX Lieb Rearchitecture.  The MDS function structure must be
      * XXX looked up, using the export_id stored in the high quad of
      * XXX the deviceid.
      */

     nfs_status
          = (fsal_mdsfunctions
             .getdeviceinfo)(data->pcontext,
                             &da_addr_body,
                             arg_GETDEVICEINFO4.gdia_layout_type,
                             &deviceid);

     da_length = xdr_getpos(&da_addr_body) - da_beginning;
     xdr_destroy(&da_addr_body);

     if (nfs_status != NFS4_OK) {
          goto out;
     }

     res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4
          .gdir_notification.bitmap4_len = 0;
     res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4
          .gdir_notification.bitmap4_val = NULL;

     res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_addr_body.da_addr_body_len
          = da_length;
     res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_addr_body.da_addr_body_val
          = da_buffer;

     nfs_status = NFS4_OK;

out:

     if ((nfs_status != NFS4_OK) &&
         da_buffer) {
          gsh_free(da_buffer);
     }

     res_GETDEVICEINFO4.gdir_status = nfs_status;
#else /* !_PNFS_MDS */
     res_GETDEVICEINFO4.gdir_status = NFS4ERR_NOTSUPP;
#endif /* !_PNFS_MDS */
     return res_GETDEVICEINFO4.gdir_status;
} /* nfs41_op_getdeviceinfo */

/**
 * \brief Frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * Frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs41_op_getdeviceinfo_Free(GETDEVICEINFO4res * resp)
{
#ifdef _PNFS_MDS
     if (resp->gdir_status == NFS4_OK) {
          if (resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr
              .da_addr_body.da_addr_body_val != NULL) {
               gsh_free(resp->GETDEVICEINFO4res_u.gdir_resok4
                        .gdir_device_addr.da_addr_body.da_addr_body_val);
          }
     }
#endif /* _PNFS_MDS */
     return;
} /* nfs41_op_getdeviceinfo_Free */
