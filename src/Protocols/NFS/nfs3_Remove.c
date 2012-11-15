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
 * @file  nfs3_Remove.c
 * @brief Everything you need for NFSv3 REMOVE
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
 * @brief The NFS PROC2 and PROC3 REMOVE
 *
 * Implements the NFS PROC REMOVE function (for V2 and V3).
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Request context
 * @param[in]  worker  Worker thread data
 * @param[in]  req     SVC request related to this call= nfs_FhandleToCache(req_ctx,
                                               req->rq_vers,
                                               &arg->arg_create2.where.dir,
                                               &arg->arg_create3.where.dir,
                                               NULL,
                                               &res->res_dirop2.status,
                                               &res->res_create3.status,
                                               NULL,
                                               export,
                                               &rc)) 
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int
nfs_Remove(nfs_arg_t *arg,
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

                name = arg->arg_remove3.object.name;

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_create3.where.dir,
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Remove handle: %s "
                         "name: %s", str, name);
        }

        /* Convert file handle into a pentry */
	/* to avoid setting it on each error case */
	res->res_remove3.REMOVE3res_u.resfail.dir_wcc.before
		.attributes_follow = FALSE;
	res->res_remove3.REMOVE3res_u.resfail.dir_wcc.after
		.attributes_follow = FALSE;
	parent_entry = nfs3_FhandleToCache(&arg->arg_remove3.object.dir,
					   req_ctx,
					   export,
					   &res->res_remove3.status,
					   &rc);
        if(parent_entry == NULL) {
                /* Stale NFS FH ? */
                goto out;
        }

        nfs_SetPreOpAttr(parent_entry,
                         req_ctx,
                         &pre_parent);

        if(nfs3_Is_Fh_Xattr(&arg->arg_remove3.object.dir)) 
          {
                rc = nfs3_Remove_Xattr(arg, export, req_ctx, req, res);
                goto out;
          }

        /*
         * Sanity checks: file name must be non-null; parent must be a
         * directory.
         */
        if (parent_entry->type != DIRECTORY) {
                res->res_remove3.status = NFS3ERR_NOTDIR;
                rc = NFS_REQ_OK;
                goto out;
        }

        name = arg->arg_remove3.object.name;

        if (name == NULL ||
            *name == '\0' ) {
                cache_status = CACHE_INODE_INVALID_ARGUMENT;
                goto out_fail;
        }

        /* Lookup the child entry to verify that it is not a directory */
        cache_status = cache_inode_lookup(parent_entry,
					  name,
					  req_ctx,
					  &child_entry);
        if (child_entry != NULL) {
                /* Sanity check: make sure we are not removing a
                   directory */
                if (child_entry->type == DIRECTORY) {
                        res->res_remove3.status = NFS3ERR_ISDIR;
                        rc = NFS_REQ_OK;
                        goto out;
                }
        }

        LogFullDebug(COMPONENT_NFSPROTO,
                     "==== NFS REMOVE ====> Trying to remove"
                     " file %s",
                     name);

        /* Remove the entry. */
        cache_status = cache_inode_remove(parent_entry,
					  name,
					  req_ctx);
        if (cache_status != CACHE_INODE_SUCCESS)
        {
                goto out_fail;
        }

        /* Build Weak Cache Coherency data */
        nfs_SetWccData(&pre_parent,
                       parent_entry,
                       req_ctx,
                       &res->res_remove3.REMOVE3res_u.resok.dir_wcc);

        res->res_remove3.status = NFS3_OK;
        rc = NFS_REQ_OK;

out:
        /* return references */
        if (child_entry) 
                cache_inode_put(child_entry);
        

        if (parent_entry) 
                cache_inode_put(parent_entry);
        

        return rc;

out_fail:
        res->res_remove3.status = nfs3_Errno(cache_status);
        nfs_SetWccData(&pre_parent,
                       parent_entry,
                       req_ctx,
                       &res->res_remove3.REMOVE3res_u.resfail.dir_wcc);


        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
                goto out;
        }

        /* return references */
        if (child_entry) 
                cache_inode_put(child_entry);
        

        if (parent_entry) 
                cache_inode_put(parent_entry);
        

        return rc;

} /* nfs_Remove */

/**
 * @brief Free the result structure allocated for nfs_Remove.
 *
 * This function frees the result structure allocated for nfs_Remove.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs_Remove_Free(nfs_res_t *res)
{
        return;
} /* nfs_Remove_Free */
