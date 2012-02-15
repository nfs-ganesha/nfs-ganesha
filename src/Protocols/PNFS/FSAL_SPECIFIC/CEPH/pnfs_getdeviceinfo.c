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
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "pnfs.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#ifdef _USE_FSALMDS
#include "fsal_pnfs.h"
#endif /* _USE_FSALMDS */

#include "pnfs_internal.h"

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

/* This should be disposed of, and let the FSAL put the size it would
   like in its static info. */

nfsstat4 FSAL_pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs,
                             compound_data_t    * data,
                             GETDEVICEINFO4res  * pres )
{
     char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getdeviceinfo";
#ifdef _USE_FSALMDS
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
#endif /* _USE_FSALMDS */

#ifdef _USE_FSALMDS
     /* Disassemble and fix byte order of the deviceid halves */

     deviceid.export_id =
          nfs_ntohl64(*(uint64_t*)pargs->gdia_device_id);

     deviceid.devid =
          nfs_ntohl64(*(uint64_t*)(pargs->gdia_device_id
                                   + sizeof(uint64_t)));

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

     if (pargs->gdia_maxcount < mincount) {
          nfs_status = NFS4ERR_TOOSMALL;
          pres->GETDEVICEINFO4res_u.gdir_mincount
               = mincount;
          goto out;
     }

     /* Set up the device_addr4 and get stream for FSAL to write into */

     pres->GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_layout_type
          = pargs->gdia_layout_type;

     if ((da_buffer = Mem_Alloc(da_addr_size)) == NULL) {
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
                             pargs->gdia_layout_type,
                             &deviceid);

     da_length = xdr_getpos(&da_addr_body) - da_beginning;
     xdr_destroy(&da_addr_body);

     if (nfs_status != NFS4_OK) {
          goto out;
     }

     pres->GETDEVICEINFO4res_u.gdir_resok4
          .gdir_notification.bitmap4_len = 0;
     pres->GETDEVICEINFO4res_u.gdir_resok4
          .gdir_notification.bitmap4_val = NULL;

     pres->GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_addr_body.da_addr_body_len
          = da_length;
     pres->GETDEVICEINFO4res_u.gdir_resok4
          .gdir_device_addr.da_addr_body.da_addr_body_val
          = da_buffer;

     nfs_status = NFS4_OK;

out:

     if ((nfs_status != NFS4_OK) &&
         da_buffer) {
          Mem_Free(da_buffer);
     }

     pres->gdir_status = nfs_status;
#else
     pres->gdir_status = NFS4ERR_NOTSUPP;
#endif /* _USE_FSALMDS */

     return pres->gdir_status;
}
