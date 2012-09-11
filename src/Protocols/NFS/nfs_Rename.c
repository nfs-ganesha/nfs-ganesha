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
 * @file  nfs_Rename.c
 * @brief Routines for NFSv2/v3 rename
 *
 * This file contains everything you need to rename files under NFSv2
 * and NFSv3.
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
 * @brief The NFS PROC2 and PROC3 RENAME
 *
 * Implements the NFS PROC RENAME function (for V2 and V3).
 *
 * @param[in]  arg     NFS argument union
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
nfs_Rename(nfs_arg_t *arg,
           exportlist_t *export,
           struct req_op_context *req_ctx,
           nfs_worker_data_t *worker,
           struct svc_req *req,
           nfs_res_t *res)
{
        char *entry_name = NULL;
        char *new_entry_name = NULL;
        cache_entry_t *parent_entry = NULL;
        cache_entry_t *new_parent_entry = NULL;
        cache_inode_status_t cache_status;
        pre_op_attr pre_parent = {
                .attributes_follow = false
        };
        pre_op_attr pre_new_parent = {
                .attributes_follow = false
        };
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

                switch (req->rq_vers) {
                case NFS_V2:
                        entry_name = arg->arg_rename2.from.name;
                        new_entry_name = arg->arg_rename2.to.name;
                        break;

                case NFS_V3:
                        entry_name = arg->arg_rename3.from.name;
                        new_entry_name = arg->arg_rename3.to.name;
                        break;
                }

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_rename2.from.dir,
                                 &arg->arg_rename3.from.dir,
                                 NULL,
                                 strfrom);

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_rename2.to.dir,
                                 &arg->arg_rename3.to.dir,
                                 NULL,
                                 strto);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Rename from "
                         "handle: %s name %s to handle: %s name: %s",
                         strfrom, entry_name, strto, new_entry_name);
        }

        if (req->rq_vers == NFS_V3) {
                /* to avoid setting it on each error case */
                res->res_rename3.RENAME3res_u.resfail.fromdir_wcc.before
                        .attributes_follow = FALSE;
                res->res_rename3.RENAME3res_u.resfail.fromdir_wcc.after
                        .attributes_follow = FALSE;
                res->res_rename3.RENAME3res_u.resfail.todir_wcc.before
                        .attributes_follow = FALSE;
                res->res_rename3.RENAME3res_u.resfail.todir_wcc.after
                        .attributes_follow = FALSE;
        }

        /* Convert fromdir file handle into a cache_entry */
        if ((parent_entry = nfs_FhandleToCache(req_ctx,
                                               req->rq_vers,
                                               &arg->arg_rename2.from.dir,
                                               &arg->arg_rename3.from.dir,
                                               NULL,
                                               &res->res_dirop2.status,
                                               &res->res_create3.status,
                                               NULL,
                                               export,
                                               &rc)) == NULL) {
                goto out;
        }

        nfs_SetPreOpAttr(parent_entry,
                         req_ctx,
                         &pre_parent);

        /* Convert todir file handle into a cache_entry */
        if ((new_parent_entry = nfs_FhandleToCache(req_ctx,
                                                   req->rq_vers,
                                                   &arg->arg_rename2.to.dir,
                                                   &arg->arg_rename3.to.dir,
                                                   NULL,
                                                   &res->res_dirop2.status,
                                                   &res->res_create3.status,
                                                   NULL,
                                                   export,
                                                   &rc)) == NULL) {
                goto out;
        }

        nfs_SetPreOpAttr(new_parent_entry,
                         req_ctx,
                         &pre_new_parent);

        /* Sanity checks: both parents must be directories */
        if ((parent_entry->type != DIRECTORY) ||
            (new_parent_entry->type != DIRECTORY)) {
                switch (req->rq_vers) {
                case NFS_V2:
                        res->res_stat2 = NFSERR_NOTDIR;
                        break;

                case NFS_V3:
                        res->res_rename3.status = NFS3ERR_NOTDIR;
                        break;
                }

                rc = NFS_REQ_OK;
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                entry_name = arg->arg_rename2.from.name;
                new_entry_name = arg->arg_rename2.to.name;
                break;

        case NFS_V3:
                entry_name = arg->arg_rename3.from.name;
                new_entry_name = arg->arg_rename3.to.name;
                break;
        }

        if(entry_name == NULL ||
           *entry_name == '\0' ||
           new_entry_name == NULL ||
           *new_entry_name == '\0') {
                cache_status = CACHE_INODE_INVALID_ARGUMENT;
                goto out_fail;
        }

        /**
         * @note ACE: Removed several checks which were also done in
         * cache_inode_rename.  There is no need to replicate them.
         */

        if (cache_inode_rename(parent_entry,
                               entry_name,
                               new_parent_entry,
                               new_entry_name,
                               req_ctx,
                               &cache_status)
            != CACHE_INODE_SUCCESS) {
                goto out_fail;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_stat2 = NFS_OK;
                break;

        case NFS_V3:
                res->res_rename3.status = NFS3_OK;
                nfs_SetWccData(&pre_parent,
                               parent_entry,
                               req_ctx,
                               &res->res_rename3.RENAME3res_u.resok
                               .fromdir_wcc);
                nfs_SetWccData(&pre_new_parent,
                               new_parent_entry,
                               req_ctx,
                               &res->res_rename3.RENAME3res_u.resok.todir_wcc);
                break;
        }

        rc = NFS_REQ_OK;



out:

        if (parent_entry) {
                cache_inode_put(parent_entry);
        }

        if (new_parent_entry) {
                cache_inode_put(new_parent_entry);
        }

        return rc;

out_fail:

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_stat2 = nfs2_Errno(cache_status);

        case NFS_V3:
                res->res_rename3.status = nfs3_Errno(cache_status);
                nfs_SetWccData(&pre_parent,
                               parent_entry,
                               req_ctx,
                               &res->res_rename3.RENAME3res_u.resfail
                               .fromdir_wcc);

                nfs_SetWccData(&pre_new_parent,
                               new_parent_entry,
                               req_ctx,
                               &res->res_rename3.RENAME3res_u.resfail
                               .todir_wcc);
        }

        /* If we are here, there was an error */
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
        }

        return rc;
} /* nfs_Rename */

/**
 * @brief Free the result structure allocated for nfs_Rename.
 *
 * This function frees the result structure allocated for nfs_Rename.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs_Rename_Free(nfs_res_t *res)
{
        return;
} /* nfs_Rename_Free */
