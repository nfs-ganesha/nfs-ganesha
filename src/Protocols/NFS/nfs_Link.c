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
 * @file  nfs_Link.c
 * @brief everything that is needed to handle NFS PROC2-3 LINK.
 *
 * Everything that is needed to handle NFS PROC2-3 LINK * LINK
 * Performs hardlink through NFS.
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
#include "nfs_file_handle.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 LINK
 *
 * The NFS PROC2 and PROC3 LINK.
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
nfs_Link(nfs_arg_t *arg,
         exportlist_t *export,
         struct req_op_context *req_ctx,
         nfs_worker_data_t *worker,
         struct svc_req *req,
         nfs_res_t *res)
{
        char *link_name = NULL;
        cache_entry_t *target_entry = NULL;
        cache_entry_t *parent_entry;
        pre_op_attr pre_parent = {
                .attributes_follow = false
        };
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        short to_exportid = 0;
        short from_exportid = 0;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char strto[LEN_FH_STR], strfrom[LEN_FH_STR];

                switch (req->rq_vers) {
                case NFS_V2:
                        link_name = arg->arg_link2.to.name;
                        break;

                case NFS_V3:
                        link_name = arg->arg_link3.link.name;
                        break;
                }

                nfs_FhandleToStr(req->rq_vers,
                                 &(arg->arg_link2.from),
                                 &(arg->arg_link3.file),
                                 NULL,
                                 strfrom);

                nfs_FhandleToStr(req->rq_vers,
                                 &(arg->arg_link2.to.dir),
                                 &(arg->arg_link3.link.dir),
                                 NULL,
                                 strto);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Link handle: %s to "
                         "handle: %s name: %s",
                         strfrom, strto, link_name);
        }

        if (req->rq_vers == NFS_V3) {
                /* to avoid setting it on each error case */
                res->res_link3.LINK3res_u.resfail.file_attributes
                        .attributes_follow = FALSE;
                res->res_link3.LINK3res_u.resfail.linkdir_wcc.before
                        .attributes_follow = FALSE;
                res->res_link3.LINK3res_u.resfail.linkdir_wcc.after
                        .attributes_follow = FALSE;
		parent_entry = nfs3_FhandleToCache(&arg->arg_link3.link.dir,
						   req_ctx,
						   export,
						   &res->res_link3.status,
						   &rc);
		if(parent_entry != NULL)
			nfs_SetPreOpAttr(parent_entry,
					 req_ctx,
					 &pre_parent);
		else
			goto out;
		target_entry = nfs3_FhandleToCache(&arg->arg_link3.file,
						   req_ctx,
						   export,
						   &res->res_link3.status,
						   &rc);
		if(target_entry == NULL)
			goto out;
        } else {
		/* Get entry for parent directory */
		parent_entry = nfs2_FhandleToCache(&arg->arg_link2.to.dir,
						   req_ctx,
						   export,
						   &res->res_stat2,
						   &rc);
		if(parent_entry == NULL)
			nfs_SetPreOpAttr(parent_entry,
					 req_ctx,
					 &pre_parent);
		else
			goto out;

		target_entry = nfs2_FhandleToCache(&arg->arg_link2.from,
						   req_ctx,
						   export,
						   &res->res_stat2,
						   &rc);
		if(target_entry == NULL)
			goto out;
        }

        /* Sanity checks: */
        if (parent_entry->type != DIRECTORY) {
                switch (req->rq_vers) {
                case NFS_V2:
                        res->res_stat2 = NFSERR_NOTDIR;
                        break;
                case NFS_V3:
                        res->res_link3.status = NFS3ERR_NOTDIR;
                        break;
                }
                rc = NFS_REQ_OK;
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                link_name = arg->arg_link2.to.name;
                to_exportid = nfs2_FhandleToExportId(&(arg->arg_link2.to.dir));
                from_exportid = nfs2_FhandleToExportId(&(arg->arg_link2.from));
                break;

        case NFS_V3:
                link_name = arg->arg_link3.link.name;
                to_exportid = nfs3_FhandleToExportId(&(arg->arg_link3
                                                       .link.dir));
                from_exportid = nfs3_FhandleToExportId(&(arg->arg_link3.file));
                break;
        }

        if (link_name == NULL || *link_name == '\0' ) {
                if (req->rq_vers == NFS_V2) {
                        res->res_stat2 = NFSERR_IO;
                }
                if (req->rq_vers == NFS_V3) {
                        res->res_link3.status = NFS3ERR_INVAL;
                }
        } else {
                /* Both objects have to be in the same filesystem */

                if (to_exportid != from_exportid) {
                        if (req->rq_vers == NFS_V2) {
                                res->res_stat2 = NFSERR_PERM;
                        } else if (req->rq_vers == NFS_V3)
                                res->res_link3.status = NFS3ERR_XDEV;
                } else {
                        if (cache_inode_link(target_entry,
                                             parent_entry,
                                             link_name,
                                             req_ctx,
                                             &cache_status)
                            == CACHE_INODE_SUCCESS) {
                                switch (req->rq_vers) {
                                case NFS_V2:
                                        res->res_stat2 = NFS_OK;
                                        break;

                                case NFS_V3:
                                        nfs_SetPostOpAttr(target_entry,
                                                          req_ctx,
                                                          &(res->res_link3
                                                            .LINK3res_u.resok.
                                                            file_attributes));

                                        nfs_SetWccData(&pre_parent,
                                                       parent_entry,
                                                       req_ctx,
                                                       &(res->res_link3
                                                         .LINK3res_u.resok
                                                         .linkdir_wcc));
                                        res->res_link3.status = NFS3_OK;
                                        break;
                                }
                                rc = NFS_REQ_OK;
                                goto out;
                        }
                }                   /* else */
        }

        /* If we are here, there was an error */
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_stat2 = nfs2_Errno(cache_status);
        case NFS_V3:
                res->res_link3.status = nfs3_Errno(cache_status);
                nfs_SetPostOpAttr(target_entry,
                                  req_ctx,
                                  &(res->res_link3.LINK3res_u.resfail
                                    .file_attributes));


        nfs_SetWccData(&pre_parent,
                       parent_entry,
                       req_ctx,
                       &res->res_link3.LINK3res_u.resfail.linkdir_wcc);

        }

        rc = NFS_REQ_OK;

out:
        /* return references */
        if (target_entry) {
                cache_inode_put(target_entry);
        }

        if (parent_entry) {
                cache_inode_put(parent_entry);
        }

        return rc;

}                               /* nfs_Link */

/**
 * @brief Free the result structure allocated for nfs_Link
 *
 * This function frees the result structure allocated for nfs_Link.
 *
 * @param[in,out] res Result structure
 *
 */
void nfs_Link_Free(nfs_res_t *rep)
{
        return;
} /* nfs_Link_Free */
