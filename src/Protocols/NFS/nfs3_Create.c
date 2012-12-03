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
 * @file  nfs3_Create.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
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
 * @brief The NFS PROC2 and PROC3 CREATE
 *
 * Implements the NFS PROC CREATE function (for V2 and V3).
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
nfs_Create(nfs_arg_t *arg,
           exportlist_t *export,
           struct req_op_context *req_ctx,
           nfs_worker_data_t *worker,
           struct svc_req *req,
           nfs_res_t *res)
{
        char *file_name = NULL;
        uint32_t mode = 0;
        cache_entry_t *file_entry = NULL;
        cache_entry_t *parent_entry = NULL;
        pre_op_attr pre_parent = {
                .attributes_follow = false
        };
        struct attrlist sattr;
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        cache_inode_status_t cache_status_lookup;
        int rc = NFS_REQ_OK;
        fsal_status_t fsal_status;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];

		file_name = arg->arg_create3.where.name;
                nfs_FhandleToStr(req->rq_vers,
				 &(arg->arg_create3.where.dir),
				 NULL,
				 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Create handle: "
                         "%s name: %s", str, file_name);
        }

        if(nfs3_Is_Fh_Xattr(&(arg->arg_create3.where.dir))) {
                rc = nfs3_Create_Xattr(arg, export, req_ctx, req, res);
                goto out;
        }

        /* to avoid setting it on each error case */
        res->res_create3.CREATE3res_u.resfail.dir_wcc
                        .before.attributes_follow = FALSE;
        res->res_create3.CREATE3res_u.resfail.dir_wcc.after
                        .attributes_follow = FALSE;
	parent_entry = nfs3_FhandleToCache(&arg->arg_create3.where.dir,
					   req_ctx,
					   export,
					   &res->res_create3.status,
					   &rc);
        if(parent_entry == NULL) {
                /* Stale NFS FH ? */
                goto out;
        }

        /* get directory attributes before action (for V3 reply) */
        nfs_SetPreOpAttr(parent_entry,
                         req_ctx,
                         &pre_parent);

        /* Sanity checks: new file name must be non-null; parent must
           be a directory. */
        if (parent_entry->type != DIRECTORY)
          {
                res->res_create3.status = NFS3ERR_NOTDIR;
                rc = NFS_REQ_OK;
                goto out;
          }

        file_name = arg->arg_create3.where.name;
        if (arg->arg_create3.how.mode == EXCLUSIVE)
          {
                        /* Client has not provided mode
                           information. If the create works, the
                           client will issue a separate setattr
                           request to fix up the file's mode, so pick
                           arbitrary value for now. */
                        mode = 0;
          }
        else if(arg->arg_create3.how.createhow3_u.obj_attributes.mode.set_it)
         {
           mode = unix2fsal_mode(arg->arg_create3.how.createhow3_u.obj_attributes.mode.set_mode3_u.mode);
         } 
       else 
         mode = 0 ;

        /* if quota support is active, then we should check is the
           FSAL allows inode creation or not */
        fsal_status = export->export_hdl->ops
                ->check_quota(export->export_hdl,
                              export->fullpath,
                              FSAL_QUOTA_INODES,
                              req_ctx);
        if (FSAL_IS_ERROR(fsal_status)) {
		res->res_create3.status = NFS3ERR_DQUOT;
                rc = NFS_REQ_OK;
                goto out;
        }

        if (file_name == NULL ||
            *file_name == '\0' ) {
                res->res_create3.status = NFS3ERR_INVAL;
                
                goto out_fail;
        }
        /* Lookup file to see if it exists.  If so, use it.  Otherwise
           create a new one. */
        cache_status_lookup = cache_inode_lookup(parent_entry,
						 file_name,
						 req_ctx,
						 &file_entry);

        if ((cache_status_lookup != CACHE_INODE_SUCCESS) &&
            (cache_status_lookup != CACHE_INODE_NOT_FOUND)) {
                /* Server fault */
                cache_status = cache_status_lookup;
                goto out_fail;
        } else if ((cache_status_lookup == CACHE_INODE_SUCCESS) &&
                   (req->rq_vers == NFS_V3) &&
                   (arg->arg_create3.how.mode != UNCHECKED)) {
                /* Trying to create a file that already exists */
                cache_status = CACHE_INODE_ENTRY_EXISTS;
                goto out_fail;
        } else if (!file_entry) {
                cache_status = cache_inode_create(parent_entry,
						  file_name,
						  REGULAR_FILE,
						  mode,
						  NULL,
						  req_ctx,
						  &file_entry);

                if (!file_entry) 
			goto out_fail;

                /* Look at sattr to see if some attributes are to be
                   set at creation time */
                FSAL_CLEAR_MASK(sattr.mask);

                if (nfs3_Sattr_To_FSALattr(&sattr,
                                           &arg->arg_create3.how.createhow3_u.obj_attributes) == 0) 
                  {
                     res->res_create3.status = NFS3ERR_INVAL;
                     rc = NFS_REQ_OK;
                     goto out;
                  }

                /* Mode is managed above (in cache_inode_create),
                   there is no need to manage it */
                FSAL_UNSET_MASK(sattr.mask, ATTR_MODE);

                /* Some clients (like Solaris 10) try to set the size
                   of the file to 0 at creation time. The FSAL create
                   empty file, so we ignore this */
                FSAL_UNSET_MASK(sattr.mask, ATTR_SIZE);
                FSAL_UNSET_MASK(sattr.mask, ATTR_SPACEUSED);

                /* Are there attributes to be set (additional to the mode) ? */
                if (FSAL_TEST_MASK(sattr.mask, ~ATTR_MODE)) {
                        /* A call to cache_inode_setattr is required */
                        cache_status = cache_inode_setattr(file_entry,
							   &sattr,
							   req_ctx);
                        if (cache_status != CACHE_INODE_SUCCESS) {
                                goto out_fail;
                        }
                }
        }

        /* Build file handle */
        res->res_create3.status =
                        nfs3_AllocateFH(&res->res_create3.CREATE3res_u.resok.obj.post_op_fh3_u.handle);
        if (res->res_create3.status != NFS3_OK)
          {
             rc = NFS_REQ_OK;
             goto out;
                }

        /* Set Post Op Fh3 structure */
	if (!nfs3_FSALToFhandle(&(res->res_create3.CREATE3res_u.resok.obj
				  .post_op_fh3_u.handle),
				file_entry->obj_handle))
           {
               gsh_free(res->res_create3.CREATE3res_u.resok.obj.
                        post_op_fh3_u.handle.data.data_val);

               res->res_create3.status = NFS3ERR_BADHANDLE;
               rc = NFS_REQ_OK;
               goto out;
           }

        /* Set Post Op Fh3 structure */
        res->res_create3.CREATE3res_u.resok.obj.handle_follows = TRUE ;

        /* Build entry attributes */
        nfs_SetPostOpAttr( file_entry,
                           req_ctx,
                           &res->res_create3.CREATE3res_u.resok.
                           obj_attributes);

        nfs_SetWccData( &pre_parent,
                        parent_entry,
                        req_ctx,
                        &res->res_create3.CREATE3res_u.resok.dir_wcc);

        res->res_create3.status = NFS3_OK;

        rc = NFS_REQ_OK;

out:
        /* return references */
        if (file_entry) {
                cache_inode_put(file_entry);
        }

        if (parent_entry) {
                cache_inode_put(parent_entry);
        }

        return rc;

out_fail:
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
        } else {
		res->res_create3.status = nfs3_Errno(cache_status);

		nfs_SetWccData(&pre_parent,
				parent_entry,
				req_ctx,
				&res->res_create3.CREATE3res_u.resfail.dir_wcc);
	}
	goto out;
} /* nfs_Create */

/**
 * @brief Free the result structure allocated for nfs_Create.
 *
 * Thsi function frees the result structure allocated for nfs_Create.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs_Create_Free(nfs_res_t *res)
{
        if ((res->res_create3.status == NFS3_OK) &&
            (res->res_create3.CREATE3res_u.resok.obj.handle_follows)) {
                gsh_free(res->res_create3.CREATE3res_u.resok.obj
                         .post_op_fh3_u.handle.data.data_val);
        }
}
