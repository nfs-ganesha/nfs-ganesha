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
 * @file  nfs3_Readlink.c
 * @brief Everything you need for NFSv3 READLINK.
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
 * @brief The NFS PROC2 and PROC3 READLINK.
 *
 * This function implements the NFS PROC2-3 READLINK function.
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Requestcontext
 * @param[in]  worker  Client resource to be used
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @see cache_inode_readlink
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 */

int
nfs_Readlink(nfs_arg_t *arg,
             exportlist_t *export,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *worker,
             struct svc_req *req,
             nfs_res_t *res)
{
        cache_entry_t *entry = NULL;
        cache_inode_status_t cache_status;
        /**
         * @todo ACE: Bogus, fix in callbacification
         */
        char symlink_data[1024];
        char *ptr = NULL;
        struct gsh_buffdesc link_buffer = {.addr = symlink_data,
                                           .len  = 1024};
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];
                nfs_FhandleToStr(req->rq_vers,
                                 &(arg->arg_readlink3.symlink),
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Readlink handle: %s",
                         str);
        }

	/* to avoid setting it on each error case */
	res->res_readlink3.READLINK3res_u.resfail.symlink_attributes
		.attributes_follow = false;
	entry = nfs3_FhandleToCache(&arg->arg_readlink3.symlink,
				    req_ctx,
				    export,
				    &res->res_readlink3.status,
				    &rc);
        if(entry == NULL) {
                goto out;
        }

        /* Sanity Check: the entry must be a link */
        if (entry->type != SYMBOLIC_LINK) {
                res->res_readlink3.status = NFS3ERR_INVAL;
                
                rc = NFS_REQ_OK;
                goto out;
        }

        cache_status = cache_inode_readlink(entry,
					    &link_buffer,
					    req_ctx);
        if (cache_status != CACHE_INODE_SUCCESS)
         {
                res->res_readlink3.status =
                                nfs3_Errno(cache_status);
                nfs_SetPostOpAttr(entry,
                                  req_ctx,
                                  &res->res_readlink3.READLINK3res_u.resfail.symlink_attributes);

                if (nfs_RetryableError(cache_status)) {
                        rc = NFS_REQ_DROP;
                        goto out;
                }
                goto out;
        }

        if ((ptr = gsh_malloc(link_buffer.len+1)) == NULL) {
                res->res_readlink3.status = NFS3ERR_IO;
                
                rc = NFS_REQ_OK;
                goto out;
        }

        memcpy(ptr, symlink_data, link_buffer.len);
        ptr[link_buffer.len] = '\0';

        /* Reply to the client */
        res->res_readlink3.READLINK3res_u.resok.data = ptr;
        nfs_SetPostOpAttr(entry,
                          req_ctx,
                          &res->res_readlink3.READLINK3res_u.resok.
                          symlink_attributes);
        res->res_readlink3.status = NFS3_OK;

        rc = NFS_REQ_OK;

out:
        /* return references */
        if (entry) {
                cache_inode_put(entry);
        }

        return rc;
}                               /* nfs_Readlink */

/**
 * @brief Free the result structure allocated for nfs3_Readlink.
 *
 * This function frees the result structure allocated for
 * nfs3_Readlink.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs3_Readlink_Free(nfs_res_t *res)
{
        if(res->res_readlink3.status == NFS3_OK) {
                gsh_free(res->res_readlink3.READLINK3res_u.resok.data);
        }
} /* nfs3_Readlink_Free */
