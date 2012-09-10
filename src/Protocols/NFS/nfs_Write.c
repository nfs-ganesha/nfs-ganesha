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
 * @file  nfs_Write.c
 * @brief Routines used for managing the Write requests.
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
#include <assert.h>
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
 * @brief The NFS PROC2 and PROC3 WRITE
 *
 * Implements the NFS PROC WRITE function (for V2 and V3).
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

int nfs_Write(nfs_arg_t *arg,
              exportlist_t *export,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *worker,
              struct svc_req *req,
              nfs_res_t *res)
{
        cache_entry_t *entry;
        pre_op_attr pre_attr = {
                .attributes_follow = false
        };
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        size_t size = 0;
        size_t written_size;
        uint64_t offset = 0;
        void *data = NULL;
        bool eof_met = false;
        cache_inode_stability_t stability = CACHE_INODE_SAFE_WRITE_TO_FS;
        int rc = NFS_REQ_OK;
#ifdef _USE_QUOTA
        fsal_status_t fsal_status ;
#endif

        if (isDebug(COMPONENT_NFSPROTO)) {
                char str[LEN_FH_STR], *stables = "";

                switch (req->rq_vers) {
                case NFS_V2:
                        offset = arg->arg_write2.offset;
                        size = arg->arg_write2.data.nfsdata2_len;
                        stables = "FILE_SYNC";
                        break;
                case NFS_V3:
                        offset = arg->arg_write3.offset;
                        size = arg->arg_write3.count;
                        switch (arg->arg_write3.stable) {
                        case UNSTABLE:  stables = "UNSTABLE"; break;
                        case DATA_SYNC: stables = "DATA_SYNC"; break;
                        case FILE_SYNC: stables = "FILE_SYNC"; break;
                        }
                }

                nfs_FhandleToStr(req->rq_vers,
                                 &arg->arg_write2.file,
                                 &arg->arg_write3.file,
                                 NULL,
                                 str);
                LogDebug(COMPONENT_NFSPROTO,
                         "REQUEST PROCESSING: Calling nfs_Write handle: %s "
                         "start: %"PRIx64" len: %"PRIx64" %s",
                         str, offset, size, stables);
        }

        if (req->rq_vers == NFS_V3) {
                /* to avoid setting it on each error case */
                res->res_write3.WRITE3res_u.resfail.file_wcc.before
                        .attributes_follow = false;
                res->res_write3.WRITE3res_u.resfail.file_wcc.after
                        .attributes_follow = false;
        }

        /* Convert file handle into a cache entry */
        if ((entry = nfs_FhandleToCache(req_ctx,
                                        req->rq_vers,
                                        &arg->arg_write2.file,
                                        &arg->arg_write3.file,
                                        NULL,
                                        &res->res_attr2.status,
                                        &res->res_write3.status,
                                        NULL,
                                        export,
                                        &rc)) == NULL) {
                /* Stale NFS FH ? */
                goto out;
        }

        nfs_SetPreOpAttr(entry,
                         req_ctx,
                         &pre_attr);

        if ((req->rq_vers == NFS_V3) &&
            (nfs3_Is_Fh_Xattr(&arg->arg_write3.file))) {
                rc = nfs3_Write_Xattr(arg, export, req_ctx, req, res);
                goto out;
        }

        if (cache_inode_access(entry,
                               FSAL_WRITE_ACCESS,
                               req_ctx,
                               &cache_status) != CACHE_INODE_SUCCESS) {
                /* NFSv3 exception : if user wants to write to a file
                 * that is readonly but belongs to him, then allow it
                 * to do it, push the permission check to the client
                 * side */
                cache_inode_lock_trust_attrs(entry,
                                             req_ctx);
                if ((cache_status == CACHE_INODE_FSAL_EACCESS) &&
                    (entry->obj_handle->attributes.owner
                     == req_ctx->creds->caller_uid)) {
                        LogDebug(COMPONENT_NFSPROTO,
                                 "Exception management: allowed user "
                                 "%"PRIu64" to write to read-only file "
                                 "belonging to him",
                                 entry->obj_handle->attributes.owner);
                } else {
                        pthread_rwlock_unlock(&entry->attr_lock);
                        switch (req->rq_vers) {
                        case NFS_V2:
                                res->res_attr2.status
                                        = nfs2_Errno(cache_status);
                                break;
                        case NFS_V3:
                                res->res_write3.status
                                        = nfs3_Errno(cache_status);
                                break;
                        }

                        rc = NFS_REQ_OK;
                        goto out;
                }
                pthread_rwlock_unlock(&entry->attr_lock);
        }

        /* Sanity check: write only a regular file */
        if (entry->type != REGULAR_FILE) {
                switch (req->rq_vers) {
                case NFS_V2:
                        /* In the RFC tell it not good but it does not
                           tell what to do ...  We use NFSERR_ISDIR
                           for lack of better */
                        res->res_attr2.status = NFSERR_ISDIR;
                        break;

                case NFS_V3:
                        if (entry->type == DIRECTORY) {
                                res->res_write3.status = NFS3ERR_ISDIR;
                        } else {
                                res->res_write3.status = NFS3ERR_INVAL;
                        }
                        break;
                }
                rc = NFS_REQ_OK;
                goto out;
        }

        /* For MDONLY export, reject write operation. This is done by
           replying EDQUOT (this error is known for not disturbing the
           client's requests cache */
        if (export->access_type != ACCESSTYPE_RW) {
                switch (req->rq_vers) {
                case NFS_V2:
                        switch (export->access_type) {
                        case ACCESSTYPE_MDONLY:
                        case ACCESSTYPE_MDONLY_RO:
                                res->res_attr2.status = NFSERR_DQUOT;
                                break;

                        case ACCESSTYPE_RO:
                                res->res_attr2.status = NFSERR_ROFS;
                                break;

                        default:
                                /* if we get here than the value is an
                                   invalid enum - corruption */
                                abort();
                                break;
                        }
                        break;

                case NFS_V3:
                        switch (export->access_type) {
                        case ACCESSTYPE_MDONLY:
                        case ACCESSTYPE_MDONLY_RO:
                                res->res_write3.status = NFS3ERR_DQUOT;
                                break;

                        case ACCESSTYPE_RO:
                                res->res_write3.status = NFS3ERR_ROFS;
                                break;

                        default:
                                /* if we get here than the value is an
                                   invalid enum - corruption */
                                abort();
                                break;
                        }
                        nfs_SetWccData(&pre_attr,
                                       entry,
                                       req_ctx,
                                       &res->res_write3.WRITE3res_u
                                       .resfail.file_wcc);
                        break;
                }

                rc = NFS_REQ_OK;
                goto out;
        }

#ifdef _USE_QUOTA
        /* if quota support is active, then we should check is the
           FSAL allows inode creation or not */
        fsal_status = export->export_hdl->ops->check_quota(export->export_hdl,
                                                           export->fullpath,
                                                           FSAL_QUOTA_BLOCKS,
                                                           req_ctx);
        if (FSAL_IS_ERROR(fsal_status)) {
                switch (req->rq_vers) {
                case NFS_V2:
                        res->res_attr2.status = NFSERR_DQUOT;
                        break;

                case NFS_V3:
                        res->res_write3.status = NFS3ERR_DQUOT;
                        break;
                }

                rc = NFS_REQ_OK ;
                goto out;
        }
#endif /* _USE_QUOTA */


        /* Extract the argument from the request */
        switch (req->rq_vers) {
        case NFS_V2:
                if (pre_attr.attributes_follow &&
                    pre_attr.pre_op_attr_u.attributes.size
                    > NFS2_MAX_FILESIZE) {
                        /* V2 clients don't understand filesizes >
                           2GB, so we don't allow them to alter them
                           in any way. BJP 6/26/2001 */
                        res->res_attr2.status = NFSERR_FBIG;
                        rc = NFS_REQ_OK;
                        goto out;
                }

                /* beginoffset is obsolete */
                offset = arg->arg_write2.offset;
                /* totalcount is obsolete  */
                size = arg->arg_write2.data.nfsdata2_len;
                data = arg->arg_write2.data.nfsdata2_val;
                if (export->use_commit) {
                        stability = CACHE_INODE_SAFE_WRITE_TO_FS;
                }
                break;

        case NFS_V3:
                offset = arg->arg_write3.offset;
                size = arg->arg_write3.count;
                if (size > arg->arg_write3.data.data_len) {
                        /* should never happen */
                        res->res_write3.status = NFS3ERR_INVAL;
                        rc = NFS_REQ_OK;
                        goto out;
                }

                if ((export->use_commit) &&
                    !export->use_ganesha_write_buffer &&
                    (arg->arg_write3.stable == UNSTABLE)) {
                        stability = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;
                } else if ((export->use_commit) &&
                           (export->use_ganesha_write_buffer) &&
                           (arg->arg_write3.stable == UNSTABLE)) {
                        stability = CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER;
                } else {
                        stability = CACHE_INODE_SAFE_WRITE_TO_FS;
                }
                data = arg->arg_write3.data.data_val;
                break;
        }

        /* Do not exceed maxium WRITE offset if set */
        if ((export->options & EXPORT_OPTION_MAXOFFSETWRITE)) {
                LogFullDebug(COMPONENT_NFSPROTO,
                             "-----> Write offset=%"PRIu64
                             " count=%"PRIu64
                             " MaxOffSet=%"PRIu64,
                             offset,
                             size,
                             export->MaxOffsetWrite);

                if((offset + size) > export->MaxOffsetWrite) {
                        LogEvent(COMPONENT_NFSPROTO,
                                 "NFS WRITE: A client tryed to violate max "
                                 "file size %"PRIu64" for exportid #%hu",
                                 export->MaxOffsetWrite,
                                 export->id);

                        switch (req->rq_vers) {
                        case NFS_V2:
                                res->res_attr2.status = NFSERR_DQUOT;
                                break;

                        case NFS_V3:
                                res->res_write3.status = NFS3ERR_INVAL;
                                break;
                        }

                        switch (req->rq_vers) {
                        case NFS_V2:
                                res->res_attr2.status
                                        = nfs2_Errno(cache_status);
                        case NFS_V3:
                                res->res_write3.status
                                        = nfs3_Errno(cache_status);
                                nfs_SetWccData(&pre_attr,
                                               entry,
                                               req_ctx,
                                               &res->res_write3.WRITE3res_u
                                               .resfail.file_wcc);
                        }

                        rc = NFS_REQ_OK;
                        goto out;
                }
        }

        /* We should take care not to exceed FSINFO wtmax field for
           the size */
        if ((export->options & EXPORT_OPTION_MAXWRITE) &&
            size > export->MaxWrite) {
                /* The client asked for too much data, we must
                   restrict him */
                size = export->MaxWrite;
        }

        if (size == 0) {
                cache_status = CACHE_INODE_SUCCESS;
                written_size = 0;
        } else {
                /* An actual write is to be made, prepare it */
                if ((cache_inode_rdwr(entry,
                                      CACHE_INODE_WRITE,
                                      offset,
                                      size,
                                      &written_size,
                                      data,
                                      &eof_met,
                                      req_ctx,
                                      stability,
                                      &cache_status)
                     == CACHE_INODE_SUCCESS)) {
                        switch (req->rq_vers) {
                        case NFS_V2:
                                cache_entry_to_nfs2_Fattr(
                                        entry,
                                        req_ctx,
                                        &res->res_attr2.ATTR2res_u
                                        .attributes);
                                res->res_attr2.status = NFS_OK;
                                break;

                        case NFS_V3:
                                /* Build Weak Cache Coherency data */
                                nfs_SetWccData(&pre_attr,
                                               entry,
                                               req_ctx,
                                               &res->res_write3.WRITE3res_u
                                               .resok.file_wcc);

                                /* Set the written size */
                                res->res_write3.WRITE3res_u.resok.count
                                        = written_size;

                                /* How do we commit data ? */
                                if(stability == CACHE_INODE_SAFE_WRITE_TO_FS) {
                                        res->res_write3.WRITE3res_u.resok
                                                .committed = FILE_SYNC;
                                } else {
                                        res->res_write3.WRITE3res_u.resok
                                                .committed = UNSTABLE;
                                }

                                /* Set the write verifier */
                                memcpy(res->res_write3.WRITE3res_u.resok.verf,
                                       NFS3_write_verifier,
                                       sizeof(writeverf3));

                                res->res_write3.status = NFS3_OK;
                                break;
                        }

                        rc = NFS_REQ_OK;
                        goto out;
                }
        }

        LogFullDebug(COMPONENT_NFSPROTO,
                     "---> failed write: cache_status=%d", cache_status);

        /* If we are here, there was an error */
        if (nfs_RetryableError(cache_status)) {
                rc = NFS_REQ_DROP;
                goto out;
        }

        switch (req->rq_vers) {
        case NFS_V2:
                res->res_attr2.status = nfs2_Errno(cache_status);
        case NFS_V3:
                res->res_write3.status = nfs3_Errno(cache_status);
                nfs_SetWccData(&pre_attr,
                               entry,
                               req_ctx,
                               &res->res_write3.WRITE3res_u.resfail.file_wcc);

        }

        rc = NFS_REQ_OK;

out:
        /* return references */
        if (entry) {
                cache_inode_put(entry);
        }

        return rc;

} /* nfs_Write.c */

/**
 * @brief Frees the result structure allocated for nfs_Write.
 *
 * Frees the result structure allocated for nfs_Write.
 *
 * @param[in,out] res Result structure
 *
 */
void
nfs_Write_Free(nfs_res_t *res)
{
        return;
}                               /* nfs_Write_Free */
