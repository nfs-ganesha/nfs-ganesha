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
 * @file  nfs3_Setattr.c
 * @brief Everything you need for NFSv3 SETATTR
 */
#include "config.h"
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
 * @brief The NFS PROC2 and PROC3 SETATTR
 *
 * Implements the NFS PROC SETATTR function (for V2 and V3).
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Request context
 * @param[in]  worker  Data for worker thread
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int
nfs_Setattr(nfs_arg_t *arg,
            exportlist_t *export,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
        struct attrlist setattr;
        cache_entry_t *entry = NULL;
        pre_op_attr pre_attr = {
                .attributes_follow = false
        };
        cache_inode_status_t cache_status;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];
                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_setattr3.object,
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Setattr handle: %s",
                         str);
        }

	/* to avoid setting it on each error case */
	res->res_setattr3.SETATTR3res_u.resfail.obj_wcc.before
		.attributes_follow = FALSE;
	res->res_setattr3.SETATTR3res_u.resfail.obj_wcc.after
		.attributes_follow = FALSE;
	entry = nfs3_FhandleToCache(&arg->arg_setattr3.object,
				    req_ctx,
				    export,
				    &res->res_setattr3.status,
				    &rc);
        if(entry == NULL) {
                goto out;
        }

        nfs_SetPreOpAttr(entry,
                         req_ctx,
                         &pre_attr);

        if(nfs3_Is_Fh_Xattr(&arg->arg_setattr3.object)) 
         {
                /* do nothing */
                nfs_SetWccData(&pre_attr,
                               entry,
                               req_ctx,
                               &res->res_setattr3.SETATTR3res_u.resok.obj_wcc);
                res->res_setattr3.status = NFS3_OK;
                rc = NFS_REQ_OK;
                goto out;
         }


        if (arg->arg_setattr3.guard.check)
          {
                /* This pack of lines implements the "guard
                 * check" setattr.  This feature of nfsv3 is
                 * used to avoid several setattr to occur
                 * concurently on the same object, from
                 * different clients */
                LogFullDebug(COMPONENT_NFSPROTO,
                             "css=%d acs=%d csn=%d acn=%d",
                             arg->arg_setattr3.guard.sattrguard3_u.obj_ctime.tv_sec,
                             pre_attr.pre_op_attr_u.attributes.ctime.tv_sec,
                             arg->arg_setattr3.guard.sattrguard3_u.obj_ctime.tv_nsec,
                             pre_attr.pre_op_attr_u.attributes.ctime.tv_nsec);

                if ((arg->arg_setattr3.guard.sattrguard3_u.obj_ctime.tv_sec !=
                     pre_attr.pre_op_attr_u.attributes.ctime.tv_sec) ||
                    (arg->arg_setattr3.guard.sattrguard3_u.obj_ctime.tv_nsec !=
                     pre_attr.pre_op_attr_u.attributes.ctime.tv_nsec)) {
                        res->res_setattr3.status = NFS3ERR_NOT_SYNC;
                        rc = NFS_REQ_OK;
                        goto out;
                }
          }

        /* Conversion to FSAL attributes */
        if (!nfs3_Sattr_To_FSALattr( &setattr,
                                     &arg->arg_setattr3.new_attributes))
          {
             res->res_setattr3.status = NFS3ERR_INVAL;
             rc = NFS_REQ_OK;
             goto out;
          }

        if (setattr.mask != 0) {
            /* If owner or owner_group are set, and the credential was
             * squashed, then we must squash the set owner and owner_group.
             */
            squash_setattr(&worker->related_client, export, req_ctx->creds, &setattr);
            cache_status = cache_inode_setattr(entry,
                                               &setattr,
                                               req_ctx);

            if (cache_status != CACHE_INODE_SUCCESS) {
                    goto out_fail;
            }
        }


        /* Set the NFS return */
        /* Build Weak Cache Coherency data */
        res->res_setattr3.status = NFS3_OK;
        nfs_SetWccData(&pre_attr,
                       entry,
                       req_ctx,
                       &res->res_setattr3.SETATTR3res_u.resok.obj_wcc);

        rc = NFS_REQ_OK;
out:
        /* return references */
        if (entry) 
                cache_inode_put(entry);
        

        return rc;


out_fail:

        LogFullDebug(COMPONENT_NFSPROTO, "nfs_Setattr: failed");

        /* Set the NFS return */
                res->res_setattr3.status = nfs3_Errno(cache_status);
                nfs_SetWccData(&pre_attr,
                               entry,
                               req_ctx,
                               &res->res_setattr3.SETATTR3res_u.resfail
                               .obj_wcc);


        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
                goto out;
        }

        /* return references */
        if (entry) 
                cache_inode_put(entry);
        

        return rc;
}                               /* nfs_Setattr */

/**
 * @brief Free the result structure allocated for nfs_Setattr.
 *
 * This function frees the result structure allocated for nfs_Setattr.
 *
 * @param[in,out] res Result structure
 */

void nfs_Setattr_Free(nfs_res_t *res)
{
        return;
} /* nfs_Setattr_Free */
