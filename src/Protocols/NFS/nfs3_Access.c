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
 * @file  nfs3_Access.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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
 * Implements NFSPROC3_ACCESS.
 *
 * This function implements NFSPROC3_ACCESS.
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
nfs3_Access(nfs_arg_t *arg,
            exportlist_t *export,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *worker,
            struct svc_req *req,
            nfs_res_t *res)
{
        uint32_t access_mode;
        cache_inode_status_t cache_status;
        object_file_type_t filetype;
        cache_entry_t *entry = NULL;
        int rc = NFS_REQ_OK;

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR];
                sprint_fhandle3(str, &(arg->arg_access3.object));
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs3_Access handle: %s",
                         str);
        }

        /* Is this a xattr FH ? */
        if (nfs3_Is_Fh_Xattr(&(arg->arg_access3.object))) {
                rc = nfs3_Access_Xattr(arg, export, req_ctx, req, res);
                goto out;
        }

        /* to avoid setting it on each error case */
        res->res_access3.ACCESS3res_u.resfail.obj_attributes.attributes_follow
                = FALSE;

        /* Convert file handle into a vnode */
        entry = nfs_FhandleToCache(req_ctx,
                                   req->rq_vers, NULL,
                                   &(arg->arg_access3.object),
                                   NULL, NULL, &(res->res_access3.status),
                                   NULL, export, &rc);
        if (entry == NULL) {
                goto out;
        }

        /* Get file type */
        filetype = entry->type;

        access_mode = 0;

        if (arg->arg_access3.access & ACCESS3_READ) {
                access_mode |= nfs_get_access_mask(ACCESS3_READ,
                                                   filetype);
        }

        if (arg->arg_access3.access & ACCESS3_MODIFY) {
                access_mode |= nfs_get_access_mask(ACCESS3_MODIFY,
                                                   filetype);
        }

        if (arg->arg_access3.access & ACCESS3_EXTEND) {
                access_mode |= nfs_get_access_mask(ACCESS3_EXTEND,
                                                   filetype);
        }

        if (filetype == REGULAR_FILE) {
                if (arg->arg_access3.access & ACCESS3_EXECUTE) {
                        access_mode |=
                                nfs_get_access_mask(ACCESS3_EXECUTE,
                                                    filetype);
                }
        } else if (arg->arg_access3.access & ACCESS3_LOOKUP) {
                access_mode |= nfs_get_access_mask(ACCESS3_LOOKUP,
                                                   filetype);
        }

        if (filetype == DIRECTORY) {
                if (arg->arg_access3.access & ACCESS3_DELETE) {
                        access_mode |=
                                nfs_get_access_mask(ACCESS3_DELETE,
                                                    filetype);
                }
        }

        nfs3_access_debug("requested access",
                          arg->arg_access3.access);

        /* Perform the 'access' call */
        if (cache_inode_access(entry,
                               access_mode,
                               req_ctx, &cache_status)
            == CACHE_INODE_SUCCESS) {
                nfs3_access_debug("granted access", arg->arg_access3.access);

                /* In Unix, delete permission only applies to
                   directories */

                if (filetype == DIRECTORY) {
                        res->res_access3.ACCESS3res_u.resok.access
                                = arg->arg_access3.access;
                } else {
                        res->res_access3.ACCESS3res_u.resok.access =
                                (arg->arg_access3.access &
                        ~ACCESS3_DELETE);
                }

                /* Build Post Op Attributes */
                nfs_SetPostOpAttr(entry,
                                  req_ctx,
                                  &(res->res_access3.ACCESS3res_u
                                    .resok.obj_attributes));

                res->res_access3.status = NFS3_OK;
                rc = NFS_REQ_OK;
                goto out;
        }

        if (cache_status == CACHE_INODE_FSAL_EACCESS) {
                /* We have to determine which access bits are good one
                   by one */
                res->res_access3.ACCESS3res_u.resok.access = 0;

                access_mode = nfs_get_access_mask(ACCESS3_READ,
                                                  filetype);
                if (cache_inode_access(entry,
                                       access_mode,
                                       req_ctx, &cache_status)
                    == CACHE_INODE_SUCCESS) {
                        res->res_access3.ACCESS3res_u.resok.access
                                |= ACCESS3_READ;
                }

                access_mode = nfs_get_access_mask(ACCESS3_MODIFY,
                                                  filetype);
                if (cache_inode_access(entry,
                                       access_mode,
                                       req_ctx, &cache_status)
                    == CACHE_INODE_SUCCESS) {
                        res->res_access3.ACCESS3res_u.resok.access
                                |= ACCESS3_MODIFY;
                }

                access_mode = nfs_get_access_mask(ACCESS3_EXTEND,
                                                  filetype);
                if (cache_inode_access(entry,
                                       access_mode,
                                       req_ctx, &cache_status)
                    == CACHE_INODE_SUCCESS) {
                        res->res_access3.ACCESS3res_u.resok.access
                                |= ACCESS3_EXTEND;
                }

                if (filetype == REGULAR_FILE) {
                        access_mode =
                                nfs_get_access_mask(ACCESS3_EXECUTE,
                                                    filetype);
                        if (cache_inode_access(entry,
                                               access_mode,
                                               req_ctx, &cache_status)
                            == CACHE_INODE_SUCCESS) {
                                res->res_access3.ACCESS3res_u.resok.access
                                        |= ACCESS3_EXECUTE;
                        }
                } else {
                        access_mode =
                                nfs_get_access_mask(ACCESS3_LOOKUP,
                                                    filetype);
                        if (cache_inode_access(entry,
                                               access_mode,
                                               req_ctx, &cache_status)
                            == CACHE_INODE_SUCCESS) {
                                res->res_access3.ACCESS3res_u.resok.access
                                        |= ACCESS3_LOOKUP;
                        }
                }

                if (filetype == DIRECTORY) {
                        access_mode =
                                nfs_get_access_mask(ACCESS3_DELETE,
                                                    filetype);
                        if (cache_inode_access(entry,
                                               access_mode,
                                               req_ctx, &cache_status)
                            == CACHE_INODE_SUCCESS) {
                                res->res_access3.ACCESS3res_u.resok.access
                                        |= ACCESS3_DELETE;
                        }
                }

                nfs3_access_debug("reduced access",
                                  res->res_access3.ACCESS3res_u.resok.access);

                res->res_access3.status = NFS3_OK;
                rc = NFS_REQ_OK;
                goto out;
        }

        /* If we are here, there was an error */
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
                goto out;
        }

        res->res_access3.status = nfs3_Errno(cache_status);
        nfs_SetPostOpAttr(entry,
                          req_ctx,
                          &(res->res_access3.ACCESS3res_u.resfail
                            .obj_attributes));
out:

        if (entry) {
                cache_inode_put(entry);
        }

        return rc;
} /* nfs3_Access */

/**
 * @brief Free the result structure allocated for nfs3_Access.
 *
 * this function frees the result structure allocated for nfs3_Access.
 *
 * @param[in,out] res Result structure.
 *
 */
void nfs3_Access_Free(nfs_res_t *res)
{
        /* Nothing to do */
        return;
} /* nfs3_Access_Free */
