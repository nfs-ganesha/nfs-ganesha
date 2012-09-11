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
 * @file    nfs_Rmdir.c
 * @brief   Remove directories in NFSv2/3
 *
 * This file contains everything you need to remove a directory in
 * NFSv2 and NFSv3.
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
 * @brief The NFS PROC2 and PROC3 RMDIR
 *
 * Implements the NFS PROC RMDIR function (for V2 and V3).
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Request context
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int
nfs_Rmdir(nfs_arg_t *arg,
          exportlist_t *export,
          struct req_op_context *req_ctx,
          nfs_worker_data_t *worker,
          struct svc_req *req,
          nfs_res_t *res)
{
        cache_entry_t *parent_entry = NULL;
        cache_entry_t *child_entry = NULL;
        pre_op_attr pre_parent = {
                .attributes_follow = false
        };
        cache_inode_status_t cache_status;
        char *name = NULL;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];

                switch (req->rq_vers) {
                case NFS_V2:
                        name = arg->arg_rmdir2.name;
                        break;
                case NFS_V3:
                        name = arg->arg_rmdir3.object.name;
                        break;
                }

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_rmdir2.dir,
                                 &arg->arg_rmdir3.object.dir,
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Rmdir handle: %s "
                         "name: %s", str, name);
        }

        if (req->rq_vers == NFS_V3) {
                /* to avoid setting it on each error case */
                res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.before
                        .attributes_follow = FALSE;
                res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.after
                        .attributes_follow = FALSE;
        }

        /* Convert file handle into a pentry */
        if ((parent_entry = nfs_FhandleToCache(req_ctx,
                                               req->rq_vers,
                                               &arg->arg_rmdir2.dir,
                                               &arg->arg_rmdir3.object.dir,
                                               NULL,
                                               &res->res_stat2,
                                               &res->res_rmdir3.status,
                                               NULL,
                                               export,
                                               &rc)) == NULL) {
                goto out;
        }

        nfs_SetPreOpAttr(parent_entry,
                         req_ctx,
                         &pre_parent);

        /* Sanity checks: directory name must be non-null; parent
           must be a directory. */
        if (parent_entry->type != DIRECTORY) {
                switch (req->rq_vers) {
                case NFS_V2:
                        res->res_stat2 = NFSERR_NOTDIR;
                        break;
                case NFS_V3:
                        res->res_rmdir3.status = NFS3ERR_NOTDIR;
                        break;
                }

                rc = NFS_REQ_OK;
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                name = arg->arg_rmdir2.name;
                break;

        case NFS_V3:
                name = arg->arg_rmdir3.object.name;
                break;
        }

        if ((name == NULL) ||
            (*name == '\0' )) {
                cache_status = CACHE_INODE_INVALID_ARGUMENT;
                goto out_fail;
        }
        /* Lookup to the entry to be removed to check that it is a
           directory */
        if ((child_entry = cache_inode_lookup(parent_entry,
                                              name,
                                              req_ctx,
                                              &cache_status)) != NULL) {
                /* Sanity check: make sure we are about to remove a
                   directory */
                if (child_entry->type != DIRECTORY) {
                        switch (req->rq_vers) {
                        case NFS_V2:
                                res->res_stat2 = NFSERR_NOTDIR;
                                break;

                        case NFS_V3:
                                res->res_rmdir3.status = NFS3ERR_NOTDIR;
                                break;
                        }
                        rc = NFS_REQ_OK;
                        goto out;
                }
        }

        if (cache_inode_remove(parent_entry,
                               name,
                               req_ctx,
                               &cache_status)
            != CACHE_INODE_SUCCESS) {
        }

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_stat2 = NFS_OK;
                break;

        case NFS_V3:
                nfs_SetWccData(&pre_parent,
                               parent_entry,
                               req_ctx,
                               &res->res_rmdir3.RMDIR3res_u.resok.dir_wcc);
                res->res_rmdir3.status = NFS3_OK;
                break;
        }

        rc = NFS_REQ_OK;


out:
        /* return references */
        if (child_entry) {
                cache_inode_put(child_entry);
        }

        if (parent_entry) {
                cache_inode_put(parent_entry);
        }

        return rc;

out_fail:

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_stat2 = nfs2_Errno(cache_status);
                break;

        case NFS_V3:
                res->res_rmdir3.status = nfs3_Errno(cache_status);
                nfs_SetWccData(&pre_parent,
                               parent_entry,
                               req_ctx,
                               &res->res_rmdir3.RMDIR3res_u.resfail.dir_wcc);

                break;
        }

        /* If we are here, there was an error */
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
        }

        /* return references */
        if (child_entry) {
                cache_inode_put(child_entry);
        }

        if (parent_entry) {
                cache_inode_put(parent_entry);
        }

        return rc;
} /* nfs_Rmdir */

/**
 * @brief Free the result structure allocated for nfs_Rmdir
 *
 * This function frees the result structure allocated for nfs_Rmdir.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs_Rmdir_Free(nfs_res_t *res)
{
        return;
} /* nfs_Rmdir_Free */
