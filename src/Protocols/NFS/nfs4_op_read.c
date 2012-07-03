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
 * @file    nfs4_op_read.c
 * @brief   NFSv4 read operation
 *
 * This file implements NFS4_OP_READ within an NFSv4 compound call.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include <stdlib.h>
#include <unistd.h>
#include "fsal_pnfs.h"
#include "server_stats.h"

static int op_dsread(struct nfs_argop4 *op,
                     compound_data_t * data,
                     struct nfs_resop4 *resp);

/**
 * @brief The NFS4_OP_READ operation
 *
 * This functions handles the NFS4_OP_READ operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    The nfs4_op arguments
 * @param[in,out] data  The compound request's data
 * @param[out]    resp  The nfs4_op results
 *
 * @return Errors as specified by RFC3550 RFC5661 p. 371.
 */

#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

int
nfs4_op_read(struct nfs_argop4 *op,
             compound_data_t *data,
             struct nfs_resop4 *resp)
{
        size_t                 size = 0, check_size = 0;
        size_t                 read_size = 0;
        uint64_t               offset = 0;
        bool                   eof_met = false;
        void                 * bufferdata = NULL;
        cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
        state_t              * state_found = NULL;
        state_t              * state_open = NULL;
        uint64_t               file_size = 0;
        cache_entry_t        * entry = NULL;
        cache_inode_stability_t stability = CACHE_INODE_SAFE_WRITE_TO_FS;
        /* This flag is set to true in the case of an anonymous read
           so that we know to release the state lock afterward.  The
           state lock does not need to be held during a non-anonymous
           read, since the open state itself prevents a conflict. */
        bool                   anonymous = false;

        /* Say we are managing NFS4_OP_READ */
        resp->resop = NFS4_OP_READ;
        res_READ4.status = NFS4_OK;

        /* Do basic checks on a filehandle Only files can be read */

        res_READ4.status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
        if (res_READ4.status != NFS4_OK) {
                return res_READ4.status;
        }

        /* If Filehandle points to a xattr object, manage it via the
           xattrs specific functions */
        if (nfs4_Is_Fh_Xattr(&(data->currentFH))) {
                return nfs4_op_read_xattr(op, data, resp);
        }

        if (nfs4_Is_Fh_DSHandle(&data->currentFH)) {
                return op_dsread(op, data, resp);
        }

        /* Manage access type MDONLY */
        if ((data->pexport->access_type == ACCESSTYPE_MDONLY) ||
            (data->pexport->access_type == ACCESSTYPE_MDONLY_RO)) {
                res_READ4.status = NFS4ERR_DQUOT;
                return res_READ4.status;
        }

        entry = data->current_entry;
        /* Check stateid correctness and get pointer to state (also
         checks for special stateids) */

        res_READ4.status = nfs4_Check_Stateid(&arg_READ4.stateid,
                                              entry,
                                              &state_found,
                                              data,
                                              STATEID_SPECIAL_ANY,
                                              0,
                                              FALSE,
                                              "READ");
        if (res_READ4.status != NFS4_OK) {
                return res_READ4.status;
        }

        /* NB: After this point, if state_found == NULL, then the
           stateid is all-0 or all-1 */

        if (state_found != NULL) {
                switch (state_found->state_type) {
                case STATE_TYPE_SHARE:
                        state_open = state_found;
                        /**
                         * @todo FSF: need to check against existing locks
                         */
                        break;

                case STATE_TYPE_LOCK:
                        state_open = state_found->state_data.lock.openstate;
                        /**
                         * @todo FSF: should check that write is in
                         * range of an byte range lock...
                         */
                        break;

                case STATE_TYPE_DELEG:
                        state_open = NULL;
                        /**
                         * @todo FSF: should check that this is a read
                         * delegation?
                         */
                        break;

                default:
                        res_READ4.status = NFS4ERR_BAD_STATEID;
                        LogDebug(COMPONENT_NFS_V4_LOCK,
                                 "READ with invalid statid of type %d",
                                 state_found->state_type);
                        return res_READ4.status;
                }

                /* This is a read operation, this means that the file
                   MUST have been opened for reading */
                if (state_open != NULL &&
                    (state_open->state_data.share.share_access &
                     OPEN4_SHARE_ACCESS_READ) == 0) {
                        /* Even if file is open for write, the client
                           may do accidently read operation (caching).
                           Because of this, READ is allowed if not
                           explicitely denied.  See page 72 in RFC3530
                           for more details */

                        if (state_open->state_data.share.share_deny &
                            OPEN4_SHARE_DENY_READ) {
                                /* Bad open mode, return NFS4ERR_OPENMODE */
                                res_READ4.status = NFS4ERR_OPENMODE;
                                LogDebug(COMPONENT_NFS_V4_LOCK,
                                         "READ state %p doesn't have "
                                         "OPEN4_SHARE_ACCESS_READ",
                                         state_found);
                                return res_READ4.status;
                        }
                }

                /**
                 * @todo : this piece of code looks a bit suspicious
                 *  (see Rong's mail)
                 *
                 * @todo: ACE: This works for now.  How do we want to
                 * handle owner confirmation across NFSv4.0/NFSv4.1?
                 * Do we want to mark every NFSv4.1 owner
                 * pre-confirmed, or make the check conditional on
                 * minorversion like we do here?
                 */
                switch (state_found->state_type) {
                case STATE_TYPE_SHARE:
                        if ((data->minorversion == 0) &&
                            (!(state_found->state_owner->so_owner
                               .so_nfs4_owner.so_confirmed))) {
                                res_READ4.status = NFS4ERR_BAD_STATEID;
                                return res_READ4.status;
                        }
                        break;

                case STATE_TYPE_LOCK:
                        /* Nothing to do */
                        break;

                default:
                        /* Sanity check: all other types are illegal.
                         * we should not got that place (similar check
                         * above), anyway it costs nothing to add this
                         * test */
                        res_READ4.status = NFS4ERR_BAD_STATEID;
                        return res_READ4.status;
                        break;
                }
        } else {
                /* Special stateid, no open state, check to see if any
                   share conflicts */
                state_open = NULL;
                PTHREAD_RWLOCK_rdlock(&entry->state_lock);
                anonymous = true;

      /* Special stateid, no open state, check to see if any share
         conflicts The stateid is all-0 or all-1 */
                res_READ4.status = nfs4_check_special_stateid(entry,"READ",
                                                    FATTR4_ATTR_READ);
                if(res_READ4.status != NFS4_OK) {
                        PTHREAD_RWLOCK_unlock(&entry->state_lock);
                        return res_READ4.status;
                }
        }

        if (state_open == NULL) {
		cache_status = cache_inode_access(entry,
						  FSAL_READ_ACCESS,
						  data->req_ctx);
                if (cache_status != CACHE_INODE_SUCCESS) {
                        res_READ4.status = nfs4_Errno(cache_status);
                        goto done;
                }
        }
        /* Get the size and offset of the read operation */
        offset = arg_READ4.offset;
        size = arg_READ4.count;

        if (((data->pexport->options & EXPORT_OPTION_MAXOFFSETREAD) ==
             EXPORT_OPTION_MAXOFFSETREAD) &&
            ((offset + size) > data->pexport->MaxOffsetRead)) {
                res_READ4.status = NFS4ERR_DQUOT;
                goto done;
        }

        /* Do not read more than FATTR4_MAXREAD.  We should check
           against the value we returned in getattr. This was not the
           case before the following check_size code was added. */
        if (((data->pexport->options & EXPORT_OPTION_MAXREAD)
             == EXPORT_OPTION_MAXREAD)) {
                check_size = data->pexport->MaxRead;
        } else {
                check_size = entry->obj_handle->export->ops
                        ->fs_maxread(entry->obj_handle->export);
        }
        if (size > check_size) {
                /* the client asked for too much data, this should normally
                   not happen because client will get FATTR4_MAXREAD value
                   at mount time */

                LogFullDebug(COMPONENT_NFS_V4,
                             "NFS4_OP_READ: read requested size = %zu "
                             " read allowed size = %"PRIu32,
                             size, data->pexport->MaxRead);
                size = data->pexport->MaxRead;
        }

        /* If size == 0, no I/O is to be made and everything is
           alright */
        if (size == 0) {
                /* A size = 0 can not lead to EOF */
                res_READ4.READ4res_u.resok4.eof = false;
                res_READ4.READ4res_u.resok4.data.data_len = 0;
                res_READ4.READ4res_u.resok4.data.data_val = NULL;
                res_READ4.status = NFS4_OK;
                goto done;
        }

        /* Some work is to be done */
        if ((bufferdata = gsh_malloc_aligned(4096, size)) == NULL) {
                LogEvent(COMPONENT_NFS_V4,
                        "FAILED to allocate bufferdata");
                res_READ4.status = NFS4ERR_SERVERFAULT;
                goto done;
        }

        if (!anonymous &&
            data->minorversion == 0) {
                data->req_ctx->clientid
                        = &state_found->state_owner->so_owner
                        .so_nfs4_owner.so_clientid;
        }

        cache_status = cache_inode_rdwr(entry,
					CACHE_INODE_READ,
					offset,
					size,
					&read_size,
					bufferdata,
					&eof_met,
					data->req_ctx,
					&stability);
	if (cache_status != CACHE_INODE_SUCCESS) {
                res_READ4.status = nfs4_Errno(cache_status);
                goto done;
        }

        if (cache_inode_size(entry,
                             data->req_ctx,
                             &file_size) != CACHE_INODE_SUCCESS) {
                res_READ4.status = nfs4_Errno(cache_status);
                goto done;
        }

        if (!anonymous &&
            data->minorversion == 0) {
                data->req_ctx->clientid = NULL;
        }

        res_READ4.READ4res_u.resok4.data.data_len = read_size;
        res_READ4.READ4res_u.resok4.data.data_val = bufferdata;

        LogFullDebug(COMPONENT_NFS_V4,
                     "NFS4_OP_READ: offset = %"PRIu64" read length = %zu eof=%u",
                     offset, read_size, eof_met);

        /* Is EOF met or not ? */
        res_READ4.READ4res_u.resok4.eof
                = (eof_met ||
                   ((offset + read_size) >= file_size));

        /* Say it is ok */
        res_READ4.status = NFS4_OK;

done:
        if (anonymous) {
                PTHREAD_RWLOCK_unlock(&entry->state_lock);
        }
#ifdef USE_DBUS_STATS
	server_stats_io_done(data->req_ctx,
			     data->pexport->id,
			     size,
			     read_size,
			     (res_READ4.status == NFS4_OK) ? true : false,
			     false);
#endif

        return res_READ4.status;
}                               /* nfs4_op_read */

/**
 * @brief Free data allocated for READ result.
 *
 * This function frees any data allocated for the result of the
 * NFS4_OP_READ operation.
 *
 * @param[in,out] resp  Results fo nfs4_op
 *
 */
void
nfs4_op_read_Free(READ4res *resp)
{
        if (resp->status == NFS4_OK) {
                if (resp->READ4res_u.resok4.data.data_len != 0) {
                        gsh_free(resp->READ4res_u.resok4.data.data_val);
                }
        }
        return;
} /* nfs4_op_read_Free */

/**
 * @brief Read on a pNFS pNFS data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a data-server read.
 *
 * @param[in]     op   Arguments for nfs41_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs41_op
 *
 * @return per RFC5661, p. 371
 *
 */

static int op_dsread(struct nfs_argop4 *op,
                     compound_data_t *data,
                     struct nfs_resop4 *resp)
{
        /* NFSv4 return code */
        nfsstat4 nfs_status = 0;
        /* Buffer into which data is to be read */
        void *buffer = NULL;
        /* End of file flag */
        bool eof = false;

        /* Don't bother calling the FSAL if the read length is 0. */

        if (arg_READ4.count == 0) {
                res_READ4.READ4res_u.resok4.eof = FALSE;
                res_READ4.READ4res_u.resok4.data.data_len = 0;
                res_READ4.READ4res_u.resok4.data.data_val = NULL;
                res_READ4.status = NFS4_OK;
                return res_READ4.status;
        }

        /* Construct the FSAL file handle */

        buffer = gsh_malloc_aligned(4096, arg_READ4.count);
        if (buffer == NULL) {
                LogEvent(COMPONENT_NFS_V4,
                        "FAILED to allocate read buffer");
                res_READ4.status = NFS4ERR_SERVERFAULT;
                return res_READ4.status;
        }

        res_READ4.READ4res_u.resok4.data.data_val = buffer;

        if ((nfs_status
             = data->current_ds->ops->read(data->current_ds,
                                           data->req_ctx,
                                           &arg_READ4.stateid,
                                           arg_READ4.offset,
                                           arg_READ4.count,
                                           res_READ4.READ4res_u
                                           .resok4.data.data_val,
                                           &res_READ4.READ4res_u
                                           .resok4.data.data_len,
                                           &eof)) != NFS4_OK) {
                gsh_free(buffer);
                buffer = NULL;
        }

        if (eof) {
                res_READ4.READ4res_u.resok4.eof = TRUE;
        } else {
                res_READ4.READ4res_u.resok4.eof = FALSE;
        }

        res_READ4.status = nfs_status;

        return res_READ4.status;
}
