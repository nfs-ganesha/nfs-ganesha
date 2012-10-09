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
 * \file    nfs3_Commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.10 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Commit.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 * @brief Implements NFSPROC3_COMMIT
 *
 * Implements NFSPROC3_COMMIT.
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
nfs3_Commit(nfs_arg_t *arg,
            exportlist_t *export,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
        cache_inode_status_t cache_status;
        cache_entry_t *entry = NULL;
        pre_op_attr pre_attr;
        uint64_t typeofcommit;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];
                sprint_fhandle3(str, &(arg->arg_commit3.file));
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs3_Commit handle: %s",
                         str);
        }

        /* To avoid setting it on each error case */
        res->res_commit3.COMMIT3res_u.resfail.file_wcc.before
                .attributes_follow = FALSE;
        res->res_commit3.COMMIT3res_u.resfail.file_wcc.after
                .attributes_follow = FALSE;

	entry = nfs3_FhandleToCache(&arg->arg_commit3.file,
				    req_ctx,
				    export,
				    &res->res_commit3.status,
				    &rc);
        if (entry == NULL) {
                goto out;
        }

        nfs_SetPreOpAttr(entry,
                         req_ctx,
                         &pre_attr);

        if ((export->use_commit) &&
            (!export->use_ganesha_write_buffer)) {
                typeofcommit = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;
        } else if((export->use_commit) &&
                  (export->use_ganesha_write_buffer)) {
                typeofcommit = CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER;
        } else {
                /* We only do stable writes with this export so no
                   need to execute a commit */
                rc = NFS_REQ_OK;
                goto out;
        }

        if (cache_inode_commit(entry,
                               arg->arg_commit3.offset,
                               arg->arg_commit3.count,
                               typeofcommit,
                               req_ctx,
                               &cache_status) != CACHE_INODE_SUCCESS) {
                res->res_commit3.status = NFS3ERR_IO;;

                nfs_SetWccData(&pre_attr,
                               entry,
                               req_ctx,
                               &(res->res_commit3.COMMIT3res_u.resfail
                                 .file_wcc));

                rc = NFS_REQ_OK;
                goto out;
        }

        nfs_SetWccData(&pre_attr,
                       entry,
                       req_ctx,
                       &(res->res_commit3.COMMIT3res_u.resok
                         .file_wcc));

        /* Set the write verifier */
        memcpy(res->res_commit3.COMMIT3res_u.resok.verf, NFS3_write_verifier,
               sizeof(writeverf3));
        res->res_commit3.status = NFS3_OK;

out:

        if (entry) {
                cache_inode_put(entry);
        }

        return rc;
}                               /* nfs3_Commit */

/**
 * @brief Frees the result structure allocated for nfs3_Commit.
 *
 * This function frees the result structure allocated for nfs3_Commit.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs3_Commit_Free(nfs_res_t * pres)
{
        return;
} /* nfs3_Commit_Free */
