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
 * @file    nfs4_op_open.c
 * @brief   NFS4_OP_OPEN
 *
 * Function implementing the NFS4_OP_OPEN operation and support code.
 *
 * @todo ACE: Add support for EXCLUSIVE4 and EXCLUSIVE4_1
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "cache_inode_lru.h"

static const char *open_tag = "OPEN";

/**
 * @brief Copy an OPEN result
 *
 * This function copies an open result to the supplied destination.
 *
 * @param[out] res_dst Buffer to which to copy the result
 * @param[in]  res_src The result to copy
 */
void nfs4_op_open_CopyRes(OPEN4res     * res_dst,
                          OPEN4res     * res_src)
{
        if (res_src->OPEN4res_u.resok4.attrset.bitmap4_val != NULL) {
                res_dst->OPEN4res_u.resok4.attrset.bitmap4_val =
                        gsh_calloc(res_dst->OPEN4res_u.resok4.attrset
                                   .bitmap4_len, sizeof(uint32_t));
                if (res_dst->OPEN4res_u.resok4.attrset.bitmap4_val == NULL) {
                        res_dst->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
                }
        }
}

/**
 * @brief Perform the open operation
 *
 * This function performs the actual open operation in cache_inode and
 * the State Abstraction layer.
 *
 * @param[in]     op        Arguments to the OPEN operation
 * @param[in,out] data      Compound's data
 * @param[in]     owner     The open owner
 * @param[out]    state     The created or found open state
 * @param[out]    new_state True if the state was newly created
 * @param[in]     openflags Open flags for the FSAL
 *
 * @retval NFS4_OK on success.
 * @retval Valid errors for NFS4_OP_OPEN.
 */

static nfsstat4
open4_do_open(struct nfs_argop4  * op,
              compound_data_t    * data,
              state_owner_t      * owner,
              state_t           ** state,
              bool_t             * new_state,
              fsal_openflags_t     openflags)
{
        /* The arguments to the open operation */
        OPEN4args            * args = &op->nfs_argop4_u.opopen;
        /* The state to be added */
        state_data_t           candidate_data;
        /* The type of state to add */
        state_type_t           candidate_type = STATE_TYPE_SHARE;
        /* Return value of state operations */
        state_status_t         state_status = STATE_SUCCESS;
        /* Return value of Cache inode operations */
        cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
        /* Iterator for state list */
        struct glist_head    * glist = NULL;
        /* Current state being investigated */
        state_t              * state_iterate = NULL;
        /* The open state for the file */
        state_t              * file_state = NULL;

        *state = NULL;
        *new_state = TRUE;

        if ((args->share_deny & OPEN4_SHARE_DENY_WRITE) ||
            (args->share_access & OPEN4_SHARE_ACCESS_WRITE)) {
                if (cache_inode_access(data->current_entry, FSAL_WRITE_ACCESS,
                                       data->req_ctx, &cache_status)
                    != CACHE_INODE_SUCCESS) {
                        return NFS4ERR_ACCESS;
                }
        }

        if (args->share_access & OPEN4_SHARE_ACCESS_READ) {
                if (cache_inode_access(data->current_entry,
                                       FSAL_READ_ACCESS,
                                       data->req_ctx,
                                       &cache_status) != CACHE_INODE_SUCCESS) {
                        return NFS4ERR_ACCESS;
                }
        }

        candidate_data.share.share_access
                = (args->share_access &
                   ~OPEN4_SHARE_ACCESS_WANT_DELEG_MASK);
        candidate_data.share.share_deny        = args->share_deny;
        candidate_data.share.share_access_prev = 0;
        candidate_data.share.share_deny_prev   = 0;

        /* Quick exit if there is any share conflict */
        if (state_share_check_conflict(data->current_entry,
                                       candidate_data.share.share_access,
                                       candidate_data.share.share_deny,
                                       &state_status) != STATE_SUCCESS) {
                return nfs4_Errno_state(state_status);
        }

        /* Try to find if the same open_owner already has acquired a
           stateid for this file */
        glist_for_each(glist, &data->current_entry->state_list) {
                state_iterate = glist_entry(glist, state_t, state_list);

                /**
                 * @todo This will need to be updated when we get
                 * delegations.
                 */
                if (state_iterate->state_type != STATE_TYPE_SHARE)
                        continue;

                /* Check if open_owner is the same.  Since owners are
                   created/looked up we should be able to just
                   compare pointers.  */
                if (state_iterate->state_powner == owner) {
                        /* We'll be re-using the found state */
                        file_state = state_iterate;
                        *new_state = FALSE;
                        /* If we are re-using stateid, then release
                           extra reference to open owner */
                        break;
                }
        }

        if (*new_state) {
                if (state_add_impl(data->current_entry, candidate_type,
                                   &candidate_data, owner,
                                   &file_state,
                                   &state_status) != STATE_SUCCESS) {
                        return nfs4_Errno_state(state_status);
                }

                init_glist(&(file_state->state_data.share.share_lockstates));

                /* Attach this open to an export */
                file_state->state_pexport = data->pexport;
                pthread_mutex_lock(&data->pexport->exp_state_mutex);
                glist_add_tail(&data->pexport->exp_state_list,
                               &file_state->state_export_list);
                pthread_mutex_unlock(&data->pexport->exp_state_mutex);
        } else {
                /* Check if open from another export */
                if (file_state->state_pexport != data->pexport) {
                        LogEvent(COMPONENT_STATE,
                                 "Lock Owner Export Conflict, Lock held for "
                                 "export %d (%s), request for export %d (%s)",
                                 file_state->state_pexport->id,
                                 file_state->state_pexport->fullpath,
                                 data->pexport->id,
                                 data->pexport->fullpath);
                        return STATE_INVALID_ARGUMENT;
                }
        }

        if (cache_inode_open(data->current_entry, openflags,
                             data->req_ctx,
                             0, &cache_status)
            != CACHE_INODE_SUCCESS) {
                return nfs4_Errno(cache_status);
        }

        /* Push share state to SAL (and FSAL) and update the union of
           file share state. */

        if (*new_state) {
                if (state_share_add(data->current_entry,
                                    owner, file_state, &state_status)
                    != STATE_SUCCESS) {
                        if (cache_inode_close(data->current_entry,
                                              0, &cache_status)
                            != CACHE_INODE_SUCCESS) {
                                /* Log bad close and continue. */
                                LogEvent(COMPONENT_STATE,
                                         "Failed to close cache inode: "
                                         "status=%d", cache_status);
                        }
                        return nfs4_Errno_state(state_status);
                }
        } else {
                /* If we find the previous share state, update share state. */
                if ((candidate_type == STATE_TYPE_SHARE) &&
                    (file_state->state_type == STATE_TYPE_SHARE)) {
                        LogFullDebug(COMPONENT_STATE,
                                     "Update existing share state");
                        if (state_share_upgrade(data->current_entry,
                                                &candidate_data,
                                                owner, file_state,
                                                &state_status)
                            != STATE_SUCCESS) {
                                if (cache_inode_close(data->current_entry,
                                                      0, &cache_status)
                                    != CACHE_INODE_SUCCESS) {
                                        /* Log bad close and continue. */
                                        LogEvent(COMPONENT_STATE,
                                                 "Failed to close cache "
                                                 "inode: status=%d",
                                                 cache_status);
                                }
                                LogEvent(COMPONENT_STATE,
                                         "Failed to update existing "
                                         "share state");
                                return nfs4_Errno_state(state_status);
                        }
                }
        }

        *state = file_state;
        return NFS4_OK;
}

/**
 * @brief Create an NFSv4 filehandle
 *
 * This function creates an NFSv4 filehandle from the supplied cache
 * entry and sets it to be the current filehandle.
 *
 * @param[in,out] data   Compound's data
 * @param[in]     entry  Cache entry corresponding to the file
 *
 * @retval NFS4_OK on success.
 * @retval Valid errors for NFS4_OP_OPEN.
 */

static nfsstat4
open4_create_fh(compound_data_t *data, cache_entry_t *entry)
{
        nfs_fh4                     newfh4;
        struct alloc_file_handle_v4 new_handle;

        newfh4.nfs_fh4_val = (caddr_t) &new_handle;
        newfh4.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);

        /* Building a new fh */
        if (!nfs4_FSALToFhandle(&newfh4, entry->obj_handle, data)) {
                return NFS4ERR_SERVERFAULT;
        }

        /* This new fh replaces the current FH */
        data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
        memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
               newfh4.nfs_fh4_len);

        data->current_entry = entry;
        data->current_filetype = REGULAR_FILE;

        return NFS4_OK;
}

/**
 * @brief Validate claim type
 *
 * Check that the claim type specified is allowed and return the
 * appropriate error code if not.
 *
 * @param[in]  data         Compound's data
 * @param[in]  claim        Claim type
 *
 * @retval NFS4_OK claim is valid.
 * @retval NFS4ERR_GRACE new open not allowed in grace period.
 * @retval NFS4ERR_NO_GRACE reclaim not allowed after grace period or
 *         reclaim complete.
 * @retval NFS4ERR_NOTSUPP claim type not supported by minor version.
 * @retval NFS4ERR_INVAL unknown claim type.
 */

static nfsstat4
open4_validate_claim(compound_data_t   * data,
                     open_claim_type4    claim,
                     nfs_client_id_t   * clientid)
{
        /* Return code */
        nfsstat4 status = NFS4_OK;

        /* Pick off erroneous claims so we don't have to deal with
           them later. */

        switch (claim) {
        case CLAIM_NULL:
                if (nfs_in_grace()) {
                        status = NFS4ERR_GRACE;
                }
                break;

        case CLAIM_FH:
                if (data->minorversion == 0) {
                        status = NFS4ERR_NOTSUPP;
                }
                if (nfs_in_grace()) {
                        status = NFS4ERR_GRACE;
                }
                break;

        case CLAIM_PREVIOUS:
                if ((clientid->cid_allow_reclaim != 1) ||
                    !nfs_in_grace()) {
                        status = NFS4ERR_NO_GRACE;
                }
                break;

        case CLAIM_DELEGATE_CUR:
        case CLAIM_DELEGATE_PREV:
        case CLAIM_DELEG_CUR_FH:
        case CLAIM_DELEG_PREV_FH:
                status = NFS4ERR_NOTSUPP;
                break;

        default:
                status = NFS4ERR_INVAL;
        }

        return status;
}

/**
 * @brief Validate and create an open owner
 *
 * This function finds or creates an owner to be associated with the
 * requested open state.
 *
 * @param[in]     arg      Arguments to OPEN4 operation
 * @param[in,out] data     Compound's data
 * @param[out]    res      Response to OPEN4 operation
 * @param[in]     clientid Clientid record for this request
 * @param[out]    owner    The found/created owner owner
 *
 * @return NFS4_OK on success, errors otherwise.
 */

static nfsstat4
open4_open_owner(struct nfs_argop4 * op,
                 compound_data_t   * data,
                 struct nfs_resop4 * res,
                 nfs_client_id_t   * clientid,
                 state_owner_t    ** owner)
{
        /* Shortcut to open args */
        OPEN4args       * const arg_OPEN4 = &(op->nfs_argop4_u.opopen);
        /* Shortcut to open args */
        OPEN4res        * const res_OPEN4 = &(res->nfs_resop4_u.opopen);
        /* The parsed-out name of the open owner */
        state_nfs4_owner_name_t owner_name;

        /* Is this open_owner known? If so, get it so we can use
           replay cache */
        convert_nfs4_open_owner(&arg_OPEN4->owner,
                                &owner_name,
                                (data->minorversion == 1 ?
                                 data->psession->clientid :
                                 0LL));

        if (!nfs4_owner_Get_Pointer(&owner_name, owner)) {
                LogFullDebug(COMPONENT_STATE, "OPEN new owner");
        } else {
                if (isFullDebug(COMPONENT_STATE)) {
                        char str[HASHTABLE_DISPLAY_STRLEN];

                        display_nfs4_owner(*owner, str);
                        LogFullDebug(COMPONENT_STATE,
                                     "A previously known open_owner is used: "
                                     "%p %s arg_OPEN4.seqid=%u",
                                     owner, str, arg_OPEN4->seqid);
                }

                /* Seqid checking is only proper for NFSv4.0 */
                if (data->minorversion == 0) {
                        if (arg_OPEN4->seqid == 0) {
                                LogDebug(COMPONENT_STATE,
                                         "Previously known open_owner is "
                                         "used with seqid=0, ask the client "
                                         "to confirm it again");
                                (*owner)->so_owner.so_nfs4_owner.so_confirmed
                                        = FALSE;
                        } else {
                                /* Check for replay */
                                if (!Check_nfs4_seqid(*owner,
                                                      arg_OPEN4->seqid,
                                                      op, data, res,
                                                      open_tag)) {
                                        /* Response is setup for us
                                           and LogDebug told what was
                                           wrong */
                                        return res_OPEN4->status;
                                }
                        }
                }
        }

        /* Is this open_owner known ? */
        if (*owner == NULL) {
                /* This open owner is not known yet, allocated and set
                   up a new one */
                *owner = create_nfs4_owner(&owner_name,
                                          clientid,
                                          STATE_OPEN_OWNER_NFSV4,
                                          NULL,
                                          (data->minorversion == 0 ?
                                           0 : 1));

                if (owner == NULL) {
                        res_OPEN4->status
                                = (data->minorversion == 0 ?
                                   NFS4ERR_RESOURCE :
                                   NFS4ERR_SERVERFAULT);
                        LogDebug(COMPONENT_STATE,
                                 "NFS4 OPEN returning error for "
                                 "CLAIM_NULL (could not create NFS4 Owner");
                        dec_client_id_ref(clientid);
                        return res_OPEN4->status;
                }
        }

        return res_OPEN4->status;
}

/**
 * @brief Create a named file
 *
 * This function implements the OPEN4_CREATE alternative of
 * CLAIM_NULL.
 *
 * @param[in]      arg      OPEN4 arguments
 * @param[in,out]  data     Comopund's data
 * @param[out]     res      OPEN4 response
 * @param[in]      parent   Directory in which to create the file
 * @param[out]     entry    Entry to be opened
 * @param[in]      filename filename
 */

static nfsstat4
open4_create(OPEN4args           * arg,
             compound_data_t     * data,
             OPEN4res            * res,
             cache_entry_t       * parent,
             cache_entry_t      ** entry,
             const char          * filename)
{
        /* Newly created file */
        cache_entry_t      * entry_newfile = NULL;
#ifdef _USE_QUOTA
        /* Return code from calls made directly to the FSAL. */
        fsal_status_t        fsal_status = {0, 0};
#endif /* _USE_QUOTA */
        /* Convertedattributes to set */
        struct attrlist      sattr;
        /* Whether the client supplied any attributes */
        bool_t               sattr_provided = FALSE;
        /* Return from Cache Inode calls */
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

        *entry = NULL;

#ifdef _USE_QUOTA
        /* if quota support is active, then we should check is
           the FSAL allows inode creation or not */
	fsal_status
		= data->pexport->export_hdl->ops->check_quota(data->pexport->export_hdl,
							      data->pexport->fullpath,
							      FSAL_QUOTA_INODES,
							      data->req_ctx);
        if (FSAL_IS_ERROR(fsal_status)) {
                return NFS4ERR_DQUOT;
        }
#endif /* _USE_QUOTA */

        /* Check if asked attributes are correct */
        if (arg->openhow.openflag4_u.how.mode == GUARDED4 ||
            arg->openhow.openflag4_u.how.mode == UNCHECKED4) {
                if (!nfs4_Fattr_Supported(
                            &arg->openhow.openflag4_u
                            .how.createhow4_u.createattrs)) {
                        return NFS4ERR_ATTRNOTSUPP;
                }
                if (!nfs4_Fattr_Check_Access(&arg->openhow.openflag4_u.how
                                             .createhow4_u.createattrs,
                                             FATTR4_ATTR_WRITE)) {
                        return NFS4ERR_INVAL;
                }
                if (arg->openhow.openflag4_u.how.createhow4_u
                    .createattrs.attrmask.bitmap4_len != 0) {
                        /* Convert fattr4 so nfs4_sattr */
                        res->status
                                = nfs4_Fattr_To_FSAL_attr(&sattr,
                                                          &(arg->openhow
                                                            .openflag4_u.how
                                                            .createhow4_u
                                                            .createattrs));
                        if (res->status != NFS4_OK) {
                                return res->status;
                        }
                        sattr_provided = TRUE;
                }
        }


        entry_newfile = cache_inode_create(parent,
                                           filename,
                                           REGULAR_FILE,
                                           /* Any mode supplied by
                                              the client will be set
                                              by setattr after the
                                              create step. */
                                           0600,
                                           NULL,
                                           NULL,
                                           data->req_ctx,
                                           &cache_status);

        /* Complete failure */
        if ((cache_status != CACHE_INODE_SUCCESS) &&
            (cache_status != CACHE_INODE_ENTRY_EXISTS)) {
                return nfs4_Errno(cache_status);
        }

        if ((cache_status == CACHE_INODE_ENTRY_EXISTS) &&
            (arg->openhow.openflag4_u.how.mode == GUARDED4)) {
                cache_inode_put(entry_newfile);
                entry_newfile = NULL;
                return nfs4_Errno(cache_status);
        }

        /* If the object exists already size is the only attribute we
           set. */
        if (cache_status == CACHE_INODE_ENTRY_EXISTS) {
                if (sattr_provided &&
                    (FSAL_TEST_MASK(sattr.mask,
                                    ATTR_SIZE)) &&
                    (sattr.filesize == 0)) {
                       FSAL_CLEAR_MASK(sattr.mask);
                        FSAL_SET_MASK(sattr.mask,
                                      ATTR_SIZE);
                } else {
                        sattr_provided = FALSE;
                }
        }

        if (sattr_provided) {
                cache_inode_setattr(entry_newfile,
                                    &sattr,
                                    data->req_ctx,
                                    &cache_status);
                if (cache_status != CACHE_INODE_SUCCESS) {
                        return nfs4_Errno(cache_status);
                }
        }

        *entry = entry_newfile;
        return nfs4_Errno(cache_status);
}


/**
 * @brief Open or create a named file
 *
 * This function implements the CLAIM_NULL type, which is used to
 * create a new or open a preëxisting file.
 *
 * entry has +1 refcount
 *
 * @param[in]     arg   OPEN4 arguments
 * @param[in,out] data  Comopund's data
 * @param[out]    res   OPEN4 rsponse
 * @param[out]    entry Entry to open
 */

static nfsstat4
open4_claim_null(OPEN4args        * arg,
                 compound_data_t  * data,
                 OPEN4res         * res,
                 cache_entry_t   ** entry)
{
        /* Parent directory in which to open the file. */
        cache_entry_t       * parent = NULL;
        /* Status for cache_inode calls */
        cache_inode_status_t  cache_status = CACHE_INODE_SUCCESS;
        /* NFS Status from function calls */
        nfsstat4              nfs_status = NFS4_OK;
        /* The filename to create */
        char                * filename = NULL;

        parent = data->current_entry;

        /**
         * Validate and convert the utf8 filename
         */
	nfs_status = nfs4_utf8string2dynamic(&arg->claim.open_claim4_u.file,
					     UTF8_SCAN_ALL,
					     &filename);
        if (nfs_status != NFS4_OK) {
                goto out;
        }

        /* Check parent */
        parent = data->current_entry;

        /* Parent must be a directory */
        if ((parent->type != DIRECTORY)) {
                if (parent->type == SYMBOLIC_LINK) {
                        nfs_status = NFS4ERR_SYMLINK;
                        goto out;
                } else {
                        nfs_status = NFS4ERR_NOTDIR;
                        goto out;
                }
        }

        switch (arg->openhow.opentype) {
        case OPEN4_CREATE:
                nfs_status = open4_create(arg,
                                          data,
                                          res,
                                          parent,
                                          entry,
                                          filename);
                break;

        case OPEN4_NOCREATE:
                *entry = cache_inode_lookup(parent,
                                            filename,
                                            NULL,
                                            data->req_ctx,
                                            &cache_status);

                if (cache_status != CACHE_INODE_SUCCESS) {
                        nfs_status = nfs4_Errno(cache_status);
                }
                break;

        default:
                nfs_status = NFS4ERR_INVAL;
        }

out:
        if (filename) {
                gsh_free(filename);
                filename = NULL;
        }
        return nfs_status;
}

/**
 * @brief NFS4_OP_OPEN
 *
 * This function impelments the NFS4_OP_OPEN operation, which
 * potentially creates and opens a regular file.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 369-70
 */

int nfs4_op_open(struct nfs_argop4 *op,
                 compound_data_t *data,
                 struct nfs_resop4 *resp)
{
        /* Shorter alias for OPEN4 arguments */
        OPEN4args         * const arg_OPEN4 = &(op->nfs_argop4_u.opopen);
        /* Shorter alias for OPEN4 response */
        OPEN4res          * const res_OPEN4 = &(resp->nfs_resop4_u.opopen);
        /* The cache entry from which the change_info4 is to be
           generated.  Every mention of change_info4 in RFC5661
           speaks of the parent directory of the file being opened.
           However, with CLAIM_FH, CLAIM_DELEG_CUR_FH, and
           CLAIM_DELEG_PREV_FH, there is no way to derive the parent
           directory from the file handle.  It is Unclear what the
           correct behavior is.  In our implementation, we take the
           change_info4 of whatever filehandle is current when the
           OPEN operation is invoked. */
        cache_entry_t           * entry_change = NULL;
        /* Open flags to be passed to the FSAL */
        fsal_openflags_t          openflags = 0;
        /* Return code from state oeprations */
        state_status_t            state_status = STATE_SUCCESS;
        /* The found client record */
        nfs_client_id_t         * clientid;
        /* The found or created state owner for this open */
        state_owner_t           * owner = NULL;
        /* The supplied calim type */
        open_claim_type4          claim = arg_OPEN4->claim.claim;
        /* The open state for the file */
        state_t           * file_state = NULL;
        /* True if the state was newly created */
        bool_t              new_state = FALSE;

        LogDebug(COMPONENT_STATE,
                 "Entering NFS v4 OPEN handler -----------------------------");

        /* What kind of open is it ? */
        LogFullDebug(COMPONENT_STATE,
                     "OPEN: Claim type = %d, Open Type = %d, Share Deny = %d, "
                     "Share Access = %d ",
                     arg_OPEN4->claim.claim,
                     arg_OPEN4->openhow.opentype,
                     arg_OPEN4->share_deny,
                     arg_OPEN4->share_access);

        resp->resop = NFS4_OP_OPEN;
        res_OPEN4->status = NFS4_OK;
        res_OPEN4->OPEN4res_u.resok4.rflags = 0 ;

        /* Do basic checks on a filehandle */
        res_OPEN4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE);
        if (res_OPEN4->status != NFS4_OK) {
                return res_OPEN4->status;
        }

        /* This can't be done on the pseudofs */
        if (nfs4_Is_Fh_Pseudo(&(data->currentFH))) {
                /* Since the PseudoFS contains nothing but
                   directories. */
                res_OPEN4->status = NFS4ERR_ISDIR;
                LogDebug(COMPONENT_STATE,
                         "NFS4 OPEN returning NFS4ERR_ISDIR");
                return res_OPEN4->status;
        }

        /*
         * If Filehandle points to a xattr object, manage it via the
         * xattrs specific functions
         */
        if (nfs4_Is_Fh_Xattr(&(data->currentFH))) {
                return nfs4_op_open_xattr(op, data, resp);
        }

        if (data->current_entry == NULL) {
                /* This should be impossible, as PUTFH fills in the
                   current entry and previous checks weed out handles
                   in the PseudoFS and DS handles. */
                res_OPEN4->status = NFS4ERR_SERVERFAULT;
                LogCrit(COMPONENT_NFS_V4,
                        "Impossible condition in compound data at %s:%u.",
                        __FILE__, __LINE__);
                goto out3;
        }

        /* It this a known client id? */
        LogDebug(COMPONENT_STATE,
                 "OPEN Client id = %"PRIx64,
                 arg_OPEN4->owner.clientid);

        if (nfs_client_id_get_confirmed((data->minorversion == 0 ?
                                         arg_OPEN4->owner.clientid :
                                         data->psession->clientid),
                                        &clientid) != CLIENT_ID_SUCCESS) {
                res_OPEN4->status = NFS4ERR_STALE_CLIENTID;
                goto out3;
        }

        /* Check if lease is expired and reserve it */
        pthread_mutex_lock(&clientid->cid_mutex);

        if (!reserve_lease(clientid)) {
                V(clientid->cid_mutex);
                dec_client_id_ref(clientid);
                res_OPEN4->status = NFS4ERR_EXPIRED;
                goto out3;
        }

        V(clientid->cid_mutex);

        /* Get the open owner */

        if ((res_OPEN4->status = open4_open_owner(op,
                                                  data,
                                                  resp,
                                                  clientid,
                                                  &owner))
            != NFS4_OK) {
                goto out3;
        }

        /* Do the claim check here, so we can save the result in the
           owner for NFSv4.0. */

        if ((res_OPEN4->status
             = open4_validate_claim(data, claim, clientid)) != NFS4_OK) {
                goto out;
        }

        /* After this point we know we have only CLAIM_NULL,
           CLAIM_FH, or CLAIM_PREVIOUS, and that our grace period and
           minor version are appropriate for the claim specified. */

        if ((arg_OPEN4->openhow.opentype == OPEN4_CREATE) &&
            (claim != CLAIM_NULL)) {
                res_OPEN4->status = NFS4ERR_INVAL;
                goto out2;
        }

        /* So we still have a reference even after we repalce the
           current FH. */

        entry_change = data->current_entry;
        if (cache_inode_lru_ref(entry_change, 0) !=
            CACHE_INODE_SUCCESS) {
                res_OPEN4->status = NFS4ERR_SERVERFAULT;
                goto out2;
        }

        res_OPEN4->OPEN4res_u.resok4.cinfo.before
                = cache_inode_get_changeid4(entry_change);

        /*
         * Check if share_access does not have any access set, or has
         * invalid bits that are set.  check that share_deny doesn't
         * have any invalid bits set.
         */
        if (!(arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_BOTH) ||
            (arg_OPEN4->share_access & (~OPEN4_SHARE_ACCESS_WANT_DELEG_MASK &
                                        ~OPEN4_SHARE_ACCESS_BOTH)) ||
            (arg_OPEN4->share_deny & ~OPEN4_SHARE_DENY_BOTH)) {
                res_OPEN4->status = NFS4ERR_INVAL;
                goto out;
        }

        /* Set openflags. */
        if (arg_OPEN4->share_access == OPEN4_SHARE_ACCESS_BOTH) {
                openflags = FSAL_O_RDWR;
        } else if(arg_OPEN4->share_access == OPEN4_SHARE_ACCESS_READ) {
                openflags = FSAL_O_READ;
        } else if(arg_OPEN4->share_access == OPEN4_SHARE_ACCESS_WRITE) {
                openflags = FSAL_O_WRITE;
        }

        /* Set the current entry to the file to be opened */
        switch (claim) {
        case CLAIM_NULL:
                {
                cache_entry_t *entry = NULL;
                res_OPEN4->status
                        = open4_claim_null(arg_OPEN4,
                                           data,
                                           res_OPEN4,
                                           &entry);
                if (res_OPEN4->status == NFS4_OK) {
                        /* Decrement the current entry here, because
                           nfs4_create_fh replaces the current fh. */
                        cache_inode_put(data->current_entry);
                        data->current_entry = NULL;
                        res_OPEN4->status = open4_create_fh(data, entry);
                }
                }
                break;

                /* Both of these just use the current filehandle. */
        case CLAIM_PREVIOUS:
                owner->so_owner.so_nfs4_owner.so_confirmed = TRUE;
        case CLAIM_FH:
                break;


        default:
                LogFatal(COMPONENT_STATE,
                         "Programming error.  Invalid claim after check.");
                break;
        }

        if (res_OPEN4->status != NFS4_OK) {
                goto out;
        }

        /* OPEN4 is to be done on a file */
        if (data->current_entry->type != REGULAR_FILE) {
                if (data->current_entry->type == DIRECTORY) {
                        res_OPEN4->status = NFS4ERR_ISDIR;
                } else if (data->current_entry->type == SYMBOLIC_LINK){
                        res_OPEN4->status = NFS4ERR_SYMLINK;
                } else {
                        res_OPEN4->status = NFS4ERR_INVAL;
                }
		goto out;
        }

        /* Set the openflags variable */
        if (arg_OPEN4->share_deny & OPEN4_SHARE_DENY_WRITE) {
                openflags |= FSAL_O_READ;
        }
        if (arg_OPEN4->share_deny & OPEN4_SHARE_DENY_READ) {
                openflags |= FSAL_O_WRITE;
        }
        if (arg_OPEN4->share_access & OPEN4_SHARE_ACCESS_WRITE) {
                openflags = FSAL_O_RDWR;
        }
        if (arg_OPEN4->share_access != 0) {
                /**
                 * @todo: FSF - I don't think we can just ignore this
                 * : Something better later
                 */
                openflags = FSAL_O_RDWR;
        }

        pthread_rwlock_wrlock(&data->current_entry->state_lock);
        res_OPEN4->status = open4_do_open(op, data, owner, &file_state,
                                          &new_state, openflags);
        pthread_rwlock_unlock(&data->current_entry->state_lock);
        if (res_OPEN4->status != NFS4_OK) {
                goto out;
        }


        res_OPEN4->OPEN4res_u.resok4.attrset.bitmap4_len = 3;
        if ((res_OPEN4->OPEN4res_u.resok4.attrset.bitmap4_val =
             gsh_calloc(res_OPEN4->OPEN4res_u.resok4.attrset.bitmap4_len,
                        sizeof(uint32_t))) == NULL) {
                res_OPEN4->status = NFS4ERR_SERVERFAULT;
                res_OPEN4->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
                LogCrit(COMPONENT_STATE,
                        "Allocation of attr failed.");
                goto out;
        }

        /* If server use OPEN_CONFIRM4, set the correct flag */
        if (owner->so_owner.so_nfs4_owner.so_confirmed == FALSE) {
                res_OPEN4->OPEN4res_u.resok4.rflags |=
                        OPEN4_RESULT_CONFIRM;
        }

        res_OPEN4->OPEN4res_u.resok4.rflags
                |= OPEN4_RESULT_LOCKTYPE_POSIX;

        LogFullDebug(COMPONENT_STATE,
                     "NFS4 OPEN returning NFS4_OK");

        /* regular exit */
        res_OPEN4->status = NFS4_OK;

        /* Update change_info4 */
        res_OPEN4->OPEN4res_u.resok4.cinfo.after
                = cache_inode_get_changeid4(entry_change);
        cache_inode_put(entry_change);
        entry_change = NULL;
        res_OPEN4->OPEN4res_u.resok4.cinfo.atomic = FALSE;

        /* We do not support delegations */
        res_OPEN4->OPEN4res_u.resok4.delegation.delegation_type
                = OPEN_DELEGATE_NONE;

        /* Handle stateid/seqid for success */
        update_stateid(file_state,
                       &res_OPEN4->OPEN4res_u.resok4.stateid,
                       data,
                       open_tag);

 out:

        /* Save the response in the lock or open owner */
        if (data->minorversion == 0) {
                Copy_nfs4_state_req(owner, arg_OPEN4->seqid, op, data,
                                    resp, open_tag);
        }

out2:

        /* Update the lease before exit */
        if (data->minorversion == 0) {
                pthread_mutex_lock(&clientid->cid_mutex);
                update_lease(clientid);
                pthread_mutex_unlock(&clientid->cid_mutex);
        }

        dec_client_id_ref(clientid);

out3:

        /* Clean up if we have an error exit */
        if ((file_state != NULL) &&
            new_state &&
            (res_OPEN4->status != NFS4_OK)) {
                /* Need to destroy open owner and state */
                if (state_del(file_state,
                              &state_status) != STATE_SUCCESS)
                        LogDebug(COMPONENT_NFS_V4_LOCK,
                                 "state_del failed with status %s",
                                 state_err_str(state_status));
        }

        if (entry_change) {
                cache_inode_put(entry_change);
        }

        if(owner != NULL) {
                /* Need to release the open owner for this call */
                dec_state_owner_ref(owner);
        }

        return res_OPEN4->status;
} /* nfs4_op_open */

/**
 * @brief Free memory allocated for OPEN result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_OPEN function.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_open_Free(OPEN4res *resp)
{
        if(resp->OPEN4res_u.resok4.attrset.bitmap4_val != NULL) {
                gsh_free(resp->OPEN4res_u.resok4.attrset.bitmap4_val);
        }
        resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
} /* nfs4_op_open_Free */

