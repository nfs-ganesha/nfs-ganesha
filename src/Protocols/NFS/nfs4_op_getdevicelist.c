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
 * @file    nfs4_op_getdevicelist.c
 * @brief   Routines used for managing the NFS4_OP_GETDEVICELIST operation.
 *
 * Routines used for managing the GETDEVICELIST operation.
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
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
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

#define arg_GETDEVICELIST4  op->nfs_argop4_u.opgetdevicelist
#define res_GETDEVICELIST4  resp->nfs_resop4_u.opgetdevicelist

int nfs4_op_getdevicelist(struct nfs_argop4 *op,
                          compound_data_t *data,
                          struct nfs_resop4 *resp)
{
#ifdef _PNFS_MDS
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
#endif /* _PNFS_MDS */

     resp->resop = NFS4_OP_GETDEVICELIST;

     if (data->minorversion == 0) {
          return (res_GETDEVICELIST4.gdlr_status = NFS4ERR_INVAL);
     }


#ifdef _PNFS_MDS
     if ((nfs_status = nfs4_sanity_check_FH(data, NO_FILE_TYPE))
         != NFS4_OK) {
          goto out;
     }

     /* Filesystems that don't support pNFS have no deviceids. */

     if (!nfs4_pnfs_supported(data->pexport)) {
          nfs_status = NFS4_OK;
          res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookie
               = 0;
          res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_val = NULL;
          res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_len = 0;
          res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4.gdlr_eof
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
     arg.type = arg_GETDEVICELIST4.gdla_layout_type;

     res.cookie = arg_GETDEVICELIST4.gdla_cookie;
     memcpy(&res.cookieverf, arg_GETDEVICELIST4.gdla_cookieverf,
            NFS4_VERIFIER_SIZE);
     res.count = arg_GETDEVICELIST4.gdla_maxdevices;
     res.devids = gsh_calloc(res.count, sizeof(uint64_t));

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

     res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookie
          = res.cookie;
     memcpy(res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4.gdlr_cookieverf,
            res.cookieverf, NFS4_VERIFIER_SIZE);

     if ((res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
          .gdlr_deviceid_list.gdlr_deviceid_list_val
          = gsh_malloc(res.count * sizeof(deviceid4))) == NULL) {
          nfs_status = NFS4ERR_SERVERFAULT;
          goto out;
     }

     for (i = 0; i < res.count; i++) {
          *(uint64_t*)
               res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
               .gdlr_deviceid_list.gdlr_deviceid_list_val[i]
               = nfs_htonl64(data->pexport->id);
          *(uint64_t*)
               (res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
                .gdlr_deviceid_list.gdlr_deviceid_list_val[i] +
                sizeof(uint64_t))
               = nfs_htonl64(res.devids[i]);
     }

     res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4
          .gdlr_deviceid_list.gdlr_deviceid_list_len = res.count;
     res_GETDEVICELIST4.GETDEVICELIST4res_u.gdlr_resok4.gdlr_eof
          = res.eof;

     nfs_status = NFS4_OK;

out:

     gsh_free(res.devids);

     res_GETDEVICELIST4.gdlr_status = nfs_status;
#else /* !_PNFS_MDS */
     res_GETDEVICELIST4.gdlr_status = NFS4ERR_NOTSUPP;
#endif /* !_PNFS_MDS */
     return res_GETDEVICELIST4.gdlr_status;
} /* nfs41_op_getdevicelist */

/**
 * @brief Free memory allocated for GETDEVICELIST result
 *
 * This function frees the memory allocates for the result of the
 * NFS4_OP_GETDEVICELIST operation.
 *
 * @param[in.out] resp nfs4_op results
 *
 */
void nfs4_op_getdevicelist_Free(GETDEVICELIST4res * resp)
{
#ifdef _PNFS_MDS
     if (resp->gdlr_status == NFS4_OK) {
          gsh_free(resp->GETDEVICELIST4res_u.gdlr_resok4
                   .gdlr_deviceid_list.gdlr_deviceid_list_val);
     }
#endif /* _PNFS_MDS */
     return;
} /* nfs41_op_getdevicelist_Free */
