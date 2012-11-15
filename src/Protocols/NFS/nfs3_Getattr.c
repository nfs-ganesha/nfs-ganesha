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
 * @file  nfs3_Getattr.c
 * @brief Implements the NFSv3 GETATTR proc
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
#include <sys/file.h> /* for having FNDELAY */
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
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief Get attributes for a file
 *
 * Get attributes for a file. Implements NFS PROC2 GETATTR and NFS
 * PROC3 GETATTR.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Request context
 * @param[in]  worker  Data belonging to the worker thread
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int
nfs_Getattr(nfs_arg_t *arg,
            exportlist_t *export,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
        cache_entry_t *entry = NULL;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];
                nfs_FhandleToStr(req->rq_vers,
                                 &(arg->arg_getattr3.object),
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Getattr handle: %s",
                         str);
        }

	entry = nfs3_FhandleToCache(&arg->arg_getattr3.object,
				    req_ctx,
				    export,
				    &res->res_getattr3.status,
				    &rc);
        if(entry == NULL) {
                LogFullDebug(COMPONENT_NFSPROTO,
                             "nfs_Getattr returning %d", rc);
                goto out;
        }

        if(nfs3_Is_Fh_Xattr(&(arg->arg_getattr3.object))) 
        {
                rc = nfs3_Getattr_Xattr(arg, export, req_ctx, req, res);
                LogFullDebug(COMPONENT_NFSPROTO,
                             "nfs_Getattr returning %d from "
                             "nfs3_Getattr_Xattr", rc);
                goto out;
        }

        if (!(cache_entry_to_nfs3_Fattr(entry,
                                        req_ctx,
                                        &(res->res_getattr3.GETATTR3res_u.resok.obj_attributes))))
         {
            res->res_getattr3.status = nfs3_Errno(CACHE_INODE_INVALID_ARGUMENT);

            LogFullDebug(COMPONENT_NFSPROTO,
                         "nfs_Getattr set failed status v3");

            rc = NFS_REQ_OK;
            goto out;
         }
       res->res_getattr3.status = NFS3_OK;

       LogFullDebug(COMPONENT_NFSPROTO, "nfs_Getattr succeeded");
       rc = NFS_REQ_OK;

out:
        /* return references */
        if (entry) 
                cache_inode_put(entry);
        

        return rc;

}                               /* nfs_Getattr */

/**
 * nfs_Getattr_Free: Frees the result structure allocated for nfs_Getattr.
 * 
 * Frees the result structure allocated for nfs_Getattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Getattr_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Getattr_Free */
