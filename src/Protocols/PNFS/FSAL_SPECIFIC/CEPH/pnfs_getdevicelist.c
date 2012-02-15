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
 * \file    pnfs_getdevicelist.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_GETDEVICELIST operation.
 *
 * pnfs_getdevicelist.c :  Routines used for managing the GETDEVICELIST operation.
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
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"
#ifdef _USE_FSALMDS
#include "fsal_pnfs.h"
#endif /* _USE_FSALMDS */

#include "pnfs_internal.h"

/**
 *
 * \brief The NFS4_OP_GETDEVICELIST operation.
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

nfsstat4 FSAL_pnfs_getdevicelist( GETDEVICELIST4args * pargs,
                                  compound_data_t * data,
                                  GETDEVICELIST4res  * pres )
{
     char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getdevicelist";
#ifdef _USE_FSALMDS
     /* NFS4 return code */
     nfsstat4 nfs_status = 0;
     /* Return from cache_inode functions */
     cache_inode_status_t cache_status = 0;
     /* Input paramaters of FSAL function */
     struct fsal_getdevicelist_arg arg;
     /* Input/output and output parameters of FSAL function */
     struct fsal_getdevicelist_res res;
     /* FSAL file handle */
     fsal_handle_t *handle = NULL;
     /* Iterator for populating deviceid list */
     size_t i = 0;
#endif /* _USE_FSALMDS */

#ifdef _USE_FSALMDS
     if ((nfs_status = nfs4_sanity_check_FH(data, 0))
         != NFS4_OK) {
          goto out;
     }

     /* Filesystems that don't support pNFS have no deviceids. */

     if (!nfs4_pnfs_supported(data->pexport)) {
          nfs_status = NFS4_OK;
          pres->GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookie
               = 0;
          pres->GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_val = NULL;
          pres->GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_len = 0;
          pres->GETDEVICELIST4res_u.gdlr_resok4.gdlr_eof
               = 1;
          goto out;
     }

     handle = cache_inode_get_fsal_handle(data->current_entry,
                                          &cache_status);

     if (cache_status != CACHE_INODE_SUCCESS) {
          nfs_status = nfs4_Errno(cache_status);
          goto out;
     }

     memset(&arg, 0, sizeof(struct fsal_getdevicelist_arg));
     memset(&res, 0, sizeof(struct fsal_getdevicelist_res));

     arg.export_id = data->pexport->id;
     arg.type = pargs->gdla_layout_type;

     res.cookie = pargs->gdla_cookie;
     memcpy(&res.cookieverf, pargs->gdla_cookieverf,
            NFS4_VERIFIER_SIZE);
     res.count = pargs->gdla_maxdevices;
     res.devids = (uint64_t*) Mem_Alloc(res.count * sizeof(uint64_t));

     if (res.devids == NULL) {
          nfs_status = NFS4ERR_SERVERFAULT;
          goto out;
     }

     /*
      * XXX This assumes a single FSAL and must be changed after the
      * XXX Lieb Rearchitecture.  The MDS function structure
      * XXX associated with the current filehandle should be used.
      */

     if ((nfs_status
          = fsal_mdsfunctions.getdevicelist(handle,
                                            data->pcontext,
                                            &arg,
                                            &res))
         != NFS4_OK) {
          goto out;
     }

     pres->GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookie
          = res.cookie;
     memcpy(pres->GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookieverf,
            res.cookieverf, NFS4_VERIFIER_SIZE);

     if ((pres->GETDEVICELIST4res_u.gdlr_resok4
          .gdlr_deviceid_list.gdlr_deviceid_list_val
          = (void*) Mem_Alloc(res.count * sizeof(deviceid4))) == NULL) {
          nfs_status = NFS4ERR_SERVERFAULT;
          goto out;
     }

     for (i = 0; i < res.count; i++) {
          *(uint64_t*)
               pres->GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_val[i]
               = nfs_htonl64(data->pexport->id);
          *(uint64_t*)
               (pres->GETDEVICELIST4res_u.gdlr_resok4
                .gdlr_deviceid_list.gdlr_deviceid_list_val[i] +
                sizeof(uint64_t))
               = nfs_htonl64(res.devids[i]);
     }

     pres->GETDEVICELIST4res_u.gdlr_resok4
          .gdlr_deviceid_list.gdlr_deviceid_list_len = res.count;
     pres->GETDEVICELIST4res_u.gdlr_resok4.gdlr_eof
          = res.eof;

     nfs_status = NFS4_OK;

out:

     Mem_Free(res.devids);

     pres->gdlr_status = nfs_status;
#else
     pres->gdlr_status = NFS4ERR_NOTSUPP;
#endif                          /* _USE_PNFS */
     return pres->gdlr_status;
}                               /* pnfs_exchange_id */

