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
 * @file  nfs_lookup.c
 * @brief everything that is needed to handle NFS PROC2-3 LINK.
 *
 * everything that is needed to handle NFS PROC2-3 LOOKUP NFS V2-3
 * generic file browsing function
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
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 LOOKUP
 *
 * Implements the NFS PROC LOOKUP function (for V2 and V3).
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Request context
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int
nfs_Lookup(nfs_arg_t *arg,
           exportlist_t *export,
           struct req_op_context *req_ctx,
           nfs_worker_data_t *worker,
           struct svc_req *req,
           nfs_res_t *res)
{
        cache_entry_t *entry_dir = NULL;
        cache_entry_t *entry_file = NULL;
        cache_inode_status_t cache_status;
        char *name = NULL;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];

                switch (req->rq_vers) {
                case NFS_V2:
                        name = arg->arg_lookup2.name;
                        break;

                case NFS_V3:
                        name = arg->arg_lookup3.what.name;
                        break;
                }

                nfs_FhandleToStr(req->rq_vers,
                                 &(arg->arg_lookup2.dir),
                                 &(arg->arg_lookup3.what.dir),
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Lookup handle: %s "
                         " name: %s", str, name);
        }

        if (req->rq_vers == NFS_V3) {
                /* to avoid setting it on each error case */
                res->res_lookup3.LOOKUP3res_u.resfail.dir_attributes
                        .attributes_follow = FALSE;
        }

        if((entry_dir = nfs_FhandleToCache(req_ctx, req->rq_vers,
                                           &(arg->arg_lookup2.dir),
                                           &(arg->arg_lookup3.what.dir),
                                           NULL,
                                           &(res->res_dirop2.status),
                                           &(res->res_lookup3.status),
                                           NULL,
                                           export, &rc)) == NULL) {
                /* Stale NFS FH? */
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                name = arg->arg_lookup2.name;
                break;

        case NFS_V3:
                name = arg->arg_lookup3.what.name;
                break;
        }

        if ((req->rq_vers == NFS_V3) &&
            (nfs3_Is_Fh_Xattr(&(arg->arg_lookup3.what.dir)))) {
                rc = nfs3_Lookup_Xattr(arg, export, req_ctx, req, res);
                goto out;
        }

        if ((entry_file
             = cache_inode_lookup(entry_dir,
                                  name,
                                  req_ctx,
                                  &cache_status)) &&
            (cache_status == CACHE_INODE_SUCCESS)) {
                switch (req->rq_vers) {
                case NFS_V2:
                        /* Build file handle */
                        if (nfs2_FSALToFhandle(
                                    &(res->res_dirop2.DIROP2res_u
                                      .diropok.file),
                                    entry_file->obj_handle,
                                    export) &&
                            cache_entry_to_nfs2_Fattr(
                                    entry_file,
                                    req_ctx,
                                    &(res->res_dirop2.DIROP2res_u
                                      .diropok.attributes))) {
                                res->res_dirop2.status = NFS_OK;
                        }
                        break;

                case NFS_V3:
                        /* Build FH */
                        res->res_lookup3.LOOKUP3res_u.resok.object.data
                                .data_val = gsh_malloc(
                                        sizeof(struct alloc_file_handle_v3));
                        if (res->res_lookup3.LOOKUP3res_u.resok.object
                            .data.data_val == NULL) {
                                res->res_lookup3.status = NFS3ERR_INVAL;
                        } else {
                                if (nfs3_FSALToFhandle(
                                            &(res->res_lookup3.LOOKUP3res_u
                                              .resok.object),
                                            entry_file->obj_handle,
                                            export)) {
                                        /* Build entry attributes */
                                        nfs_SetPostOpAttr(entry_file,
                                                          req_ctx,
                                                          &(res->res_lookup3
                                                            .LOOKUP3res_u.resok
                                                            .obj_attributes));

                                        /* Build directory attributes */
                                        nfs_SetPostOpAttr(entry_dir,
                                                          req_ctx,
                                                          &(res->res_lookup3
                                                            .LOOKUP3res_u.resok
                                                            .dir_attributes));
                                        res->res_lookup3.status = NFS3_OK;
                                }
                        }
                        break;
                }
        } else {
                /* If we are here, there was an error */
                if (nfs_RetryableError(cache_status)) {
                        rc = NFS_REQ_DROP;
                        goto out;
                }

                switch (req->rq_vers) {
                case NFS_V2:
                        res->res_dirop2.status = nfs2_Errno(cache_status);
                case NFS_V3:
                        res->res_lookup3.status
                                = nfs3_Errno(cache_status);
                        nfs_SetPostOpAttr(entry_dir,
                                          req_ctx,
                                          &res->res_lookup3.LOOKUP3res_u
                                          .resfail.dir_attributes);
                }

        }

        rc = NFS_REQ_OK;

out:
        /* return references */
        if (entry_dir) {
                cache_inode_put(entry_dir);
        }

        if (entry_file) {
                cache_inode_put(entry_file);
        }

        return (rc);
}                               /* nfs_Lookup */

/**
 * @brief Free the result structure allocated for nfs_Lookup.
 *
 * This function frees the result structure allocated for nfs_Lookup.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs3_Lookup_Free(nfs_res_t *res)
{
        if (res->res_lookup3.status == NFS3_OK) {
                gsh_free(res->res_lookup3.LOOKUP3res_u.resok.object.data
                         .data_val);
        }
} /* nfs3_Lookup_Free */

/**
 * @brief Free the result structure allocated for nfs_Lookup.
 *
 * This function frees the result structure allocated for nfs_Lookup.
 *
 * @param[in,out] res Result structure
 */
void nfs2_Lookup_Free(nfs_res_t *res)
{
        return;
} /* nfs2_Lookup_Free */
