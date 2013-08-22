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
 * @file nfs4_op_setattr.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#include "config.h"
#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "sal_functions.h"

/**
 * @brief The NFS4_OP_SETATTR operation.
 *
 * This functions handles the NFS4_OP_SETATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */

int
nfs4_op_setattr(struct nfs_argop4 *op,
                compound_data_t *data,
                struct nfs_resop4 *resp)
{
	SETATTR4args *const arg_SETATTR4 = &op->nfs_argop4_u.opsetattr;
	SETATTR4res *const res_SETATTR4 = &resp->nfs_resop4_u.opsetattr;
        struct attrlist        sattr;
        cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
        const char           * tag = "SETATTR";
        state_t              * state_found = NULL;
        state_t              * state_open  = NULL;
        cache_entry_t        * entry       = NULL;

        memset(&sattr, 0, sizeof(sattr));
        resp->resop = NFS4_OP_SETATTR;
        res_SETATTR4->status = NFS4_OK;

        /* Do basic checks on a filehandle */
        res_SETATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);
        if (res_SETATTR4->status != NFS4_OK) {
                return res_SETATTR4->status;
        }

        /* Get only attributes that are allowed to be read */
        if (!nfs4_Fattr_Check_Access(&arg_SETATTR4->obj_attributes,
                                     FATTR4_ATTR_WRITE)) {
                res_SETATTR4->status = NFS4ERR_INVAL;
                return res_SETATTR4->status;
        }

        /* Ask only for supported attributes */
        if (!nfs4_Fattr_Supported(&arg_SETATTR4->obj_attributes)) {
                res_SETATTR4->status = NFS4ERR_ATTRNOTSUPP;
                return res_SETATTR4->status;
        }

        /* Convert the fattr4 in the request to a fsal sattr structure */
	res_SETATTR4->status
		= nfs4_Fattr_To_FSAL_attr(&sattr,
					  &(arg_SETATTR4->obj_attributes),
					  data);
        if (res_SETATTR4->status != NFS4_OK) {
                return res_SETATTR4->status;
        }

        /* Trunc may change Xtime so we have to start with trunc and
         * finish by the mtime and atime */
        if (FSAL_TEST_MASK(sattr.mask, ATTR_SIZE)) {
                /* Setting the size of a directory is prohibited */
                if (data->current_filetype == DIRECTORY) {
                        res_SETATTR4->status = NFS4ERR_ISDIR;
                        return res_SETATTR4->status;
                }
                /* Object should be a file */
                if (data->current_entry->type != REGULAR_FILE) {
                        res_SETATTR4->status = NFS4ERR_INVAL;
                        return res_SETATTR4->status;
                }

                entry = data->current_entry;

                /* Check stateid correctness and get pointer to state */
                res_SETATTR4->status
                        = nfs4_Check_Stateid(&arg_SETATTR4->stateid,
                                             data->current_entry,
                                             &state_found,
                                             data,
                                             STATEID_SPECIAL_ANY,
                                             0,
                                             FALSE,
                                             tag);
                if (res_SETATTR4->status != NFS4_OK) {
                        return res_SETATTR4->status;
                }

                /* NB: After this point, if state_found == NULL, then
                   the stateid is all-0 or all-1 */
                if (state_found != NULL) {
                        switch (state_found->state_type) {
                        case STATE_TYPE_SHARE:
                                state_open = state_found;
                                break;

                        case STATE_TYPE_LOCK:
                                state_open = state_found->state_data
					.lock.openstate;
                                break;

                        case STATE_TYPE_DELEG:
                                state_open = NULL;
                                break;

                        default:
                                res_SETATTR4->status = NFS4ERR_BAD_STATEID;
                                return res_SETATTR4->status;
                        }

                        /* This is a size operation, this means that
                           the file MUST have been opened for
                           writing */
                        if (state_open != NULL &&
                            (state_open->state_data.share.share_access &
                             OPEN4_SHARE_ACCESS_WRITE) == 0) {
                                /* Bad open mode, return NFS4ERR_OPENMODE */
                                res_SETATTR4->status = NFS4ERR_OPENMODE;
                                return res_SETATTR4->status;
                        }
                } else {
                        /* Special stateid, no open state, check to
                           see if any share conflicts */
                        state_open = NULL;

                        /* Special stateid, no open state, check to
                           see if any share conflicts The stateid is
                           all-0 or all-1 */
                        res_SETATTR4->status
                                = nfs4_check_special_stateid(
                                        entry,
                                        "SETATTR(size)",
                                        FATTR4_ATTR_WRITE);
                        if (res_SETATTR4->status != NFS4_OK) {
                                return res_SETATTR4->status;
                        }
                }
        }
        /* Now, we set the mode */
        if (FSAL_TEST_MASK(sattr.mask, ATTR_MODE) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_OWNER) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_GROUP) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_SIZE) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_MTIME_SERVER) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_MTIME_SERVER) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_MTIME) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_ACL) ||
            FSAL_TEST_MASK(sattr.mask, ATTR_ATIME)) {
                /* Check for root access when using chmod */
                if(FSAL_TEST_MASK(sattr.mask, ATTR_MODE)) {
                        if ((sattr.mode & S_ISUID) &&
                            ((data->pexport->export_perms.options & EXPORT_OPTION_NOSUID) ||
                             ((sattr.mode & S_ISGID) &&
                              ((data->pexport->export_perms.options &
                                EXPORT_OPTION_NOSGID))))) {
                                LogInfo(COMPONENT_NFS_V4,
                                        "Setattr denied because setuid "
                                        "or setgid bit is disabled in "
                                        "configuration file. setuid=%d, "
                                        "setgid=%d",
                                        sattr.mode & S_ISUID ? 1 : 0,
                                        sattr.mode & S_ISGID ? 1 : 0);
                                res_SETATTR4->status = NFS4ERR_PERM;
                                return res_SETATTR4->status;
                        }
                }
        }

	const time_t S_NSECS=1000000000UL;
        /* Set the atime and mtime (ctime is not setable) */

        /* A carry into seconds considered invalid */
        if (sattr.atime.tv_nsec >= S_NSECS) {
                res_SETATTR4->status = NFS4ERR_INVAL;
                return res_SETATTR4->status;
        }
        if (sattr.mtime.tv_nsec >= S_NSECS) {
                res_SETATTR4->status = NFS4ERR_INVAL;
                return res_SETATTR4->status;
        }
        /* If owner or owner_group are set, and the credential was
         * squashed, then we must squash the set owner and owner_group.
         */
        squash_setattr(&data->export_perms, data->req_ctx->creds, &sattr);

        cache_status = cache_inode_setattr(data->current_entry,
					   &sattr,
					   data->req_ctx);
	if (cache_status != CACHE_INODE_SUCCESS) {
                res_SETATTR4->status = nfs4_Errno(cache_status);
                return res_SETATTR4->status;
        }

        /* Set the replyed structure */
        res_SETATTR4->attrsset = arg_SETATTR4->obj_attributes.attrmask;

        /* Exit with no error */
        res_SETATTR4->status = NFS4_OK;

        return res_SETATTR4->status;
} /* nfs4_op_setattr */

/**
 * @brief Free memory allocated for SETATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_SETATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_setattr_Free(nfs_resop4 *resp)
{
        return;
} /* nfs4_op_setattr_Free */
