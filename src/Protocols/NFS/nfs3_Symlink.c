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
 * @file  nfs3_Symlink.c
 * @brief Everything you need for NFSv3 SYMLINK
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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 SYMLINK
 *
 * Implements the NFS PROC SYMLINK function (for V2 and V3).
 *
 * @param[in]  arg     NFS argument union
 * @param[in]  export  NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
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
nfs_Symlink(nfs_arg_t *arg,
            exportlist_t *export,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
        char *symlink_name = NULL;
        char *target_path = NULL;
        cache_inode_create_arg_t create_arg;
        uint32_t mode = 0777;
        cache_entry_t *symlink_entry = NULL;
        cache_entry_t *parent_entry;
        pre_op_attr pre_parent;
        cache_inode_status_t cache_status;
        int rc = NFS_REQ_OK;
        fsal_status_t fsal_status;

        memset(&create_arg, 0, sizeof(create_arg));

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];

                symlink_name = arg->arg_symlink3.where.name;
                target_path = arg->arg_symlink3.symlink.symlink_data;

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_symlink3.where.dir,
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Symlink handle: "
                         "%s name: %s target: %s", str, symlink_name,
                         target_path);
        }

	/* to avoid setting it on each error case */
	res->res_symlink3.SYMLINK3res_u.resfail.dir_wcc.before
		.attributes_follow = false;
	res->res_symlink3.SYMLINK3res_u.resfail.dir_wcc.after
		.attributes_follow = false;
	parent_entry = nfs3_FhandleToCache(&arg->arg_symlink3.where.dir,
					   req_ctx,
					   export,
					   &res->res_symlink3.status,
					   &rc);
        if(parent_entry == NULL) {
                goto out;;
        }

        nfs_SetPreOpAttr(parent_entry,
                         req_ctx,
                         &pre_parent);

        /* Sanity checks: new directory name must be non-null; parent
           must be a directory. */
        if (parent_entry->type != DIRECTORY)
          {
                res->res_symlink3.status = NFS3ERR_NOTDIR;

                rc = NFS_REQ_OK;
                goto out;
          }

        /* if quota support is active, then we should check is the
           FSAL allows inode creation or not */
        fsal_status = export->export_hdl->ops->check_quota(export->export_hdl,
                                                           export->fullpath,
                                                           FSAL_QUOTA_INODES,
                                                           req_ctx);
        if (FSAL_IS_ERROR(fsal_status)) {
                res->res_symlink3.status = NFS3ERR_DQUOT;

                rc = NFS_REQ_OK;
                goto out;
        }
        symlink_name = arg->arg_symlink3.where.name;
        target_path = arg->arg_symlink3.symlink.symlink_data;

        create_arg.link_content = target_path;

        if (symlink_name == NULL ||
            *symlink_name == '\0'||
            target_path == NULL  ||
            *target_path == '\0') {
                cache_status = CACHE_INODE_INVALID_ARGUMENT;
                goto out_fail;

        }

        /* Make the symlink */
	cache_status = cache_inode_create(parent_entry,
					  symlink_name,
					  SYMBOLIC_LINK,
					  mode,
					  &create_arg,
					  req_ctx,
					  &symlink_entry);
	if (symlink_entry == NULL) {
                goto out_fail;
        }

        struct attrlist sattr;
        /* Some clients (like the Spec NFS benchmark) set
           attributes with the NFSPROC3_SYMLINK request */
        if (!nfs3_Sattr_To_FSALattr(&sattr,
                                    &arg->arg_symlink3.symlink.symlink_attributes)) 
         {
                res->res_create3.status = NFS3ERR_INVAL;
                rc = NFS_REQ_OK;
                goto out;
         }

        FSAL_UNSET_MASK(sattr.mask,
                        (ATTR_MODE | ATTR_SIZE |
                         ATTR_SPACEUSED));

        /* Are there any attributes left to set? */
        if (sattr.mask) {
                /* A call to cache_inode_setattr is required */
                cache_status = cache_inode_setattr(symlink_entry,
						   &sattr,
						   req_ctx);
		if (cache_status != CACHE_INODE_SUCCESS) {
			goto out_fail;
		}
	}

        if ((res->res_symlink3.status =
             (nfs3_AllocateFH(&res->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle))) != NFS3_OK) {
                res->res_symlink3.status = NFS3ERR_IO;
                rc = NFS_REQ_OK;
                goto out;
        }

        if (!nfs3_FSALToFhandle(&res->res_symlink3.SYMLINK3res_u
				.resok.obj.post_op_fh3_u.handle,
				symlink_entry->obj_handle))
             {
                gsh_free(res->res_symlink3.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle.data.data_val);
                res->res_symlink3.status = NFS3ERR_BADHANDLE;
                rc = NFS_REQ_OK;
                goto out;
             }

        res->res_symlink3.SYMLINK3res_u.resok.obj.handle_follows = TRUE;

        /* Build entry attributes */
        nfs_SetPostOpAttr(symlink_entry,
                          req_ctx,
                          &res->res_symlink3.SYMLINK3res_u.resok.obj_attributes);

        /* Build Weak Cache Coherency data */
        nfs_SetWccData(&pre_parent,
                       parent_entry,
                       req_ctx,
                       &res->res_symlink3.SYMLINK3res_u.resok.dir_wcc);

        res->res_symlink3.status = NFS3_OK;
        rc = NFS_REQ_OK;


out:
        /* return references */
        if (parent_entry) 
                cache_inode_put(parent_entry);
        

        if (symlink_entry) 
                cache_inode_put(symlink_entry);
        

        return rc;

out_fail:

        res->res_symlink3.status = nfs3_Errno(cache_status);
        nfs_SetWccData(&pre_parent,
                       parent_entry,
                       req_ctx,
                       &res->res_symlink3.SYMLINK3res_u.resfail.dir_wcc);


        if (nfs_RetryableError(cache_status)) 
                rc = NFS_REQ_DROP;
        

        /* return references */
        if (parent_entry) 
                cache_inode_put(parent_entry);
        

        if (symlink_entry) 
                cache_inode_put(symlink_entry);
        

        return rc;
}                               /* nfs_Symlink */

/**
 * @brief Free the result structure allocated for nfs_Symlink.
 *
 * This function frees the result structure allocated for nfs_Symlink.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs_Symlink_Free(nfs_res_t *res)
{
        if ((res->res_symlink3.status == NFS3_OK) &&
            (res->res_symlink3.SYMLINK3res_u.resok.obj
             .handle_follows)) {
                gsh_free(res->res_symlink3.SYMLINK3res_u.resok.obj
                         .post_op_fh3_u.handle.data.data_val);
        }
} /* nfs_Symlink_Free */
