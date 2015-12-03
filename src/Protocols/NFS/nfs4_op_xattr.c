/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file nfs4_op_xattr.c
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
#include "nfs_convert.h"
#include "sal_functions.h"
#include "nfs_creds.h"

/**
 * @brief The NFS4_OP_GETXATTR operation.
 *
 * This functions handles the NFS4_OP_GETXATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */
int nfs4_op_getxattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	GETXATTR4args * const arg_GETXATTR4 = &op->nfs_argop4_u.opgetxattr;
	GETXATTR4res * const res_GETXATTR4 = &resp->nfs_resop4_u.opgetxattr;

	resp->resop = NFS4_OP_GETXATTR;
	res_GETXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "GetXattr len %d name: %s",
		 arg_GETXATTR4->ga_name.utf8string_len,
		 arg_GETXATTR4->ga_name.utf8string_val);

	/* Do basic checks on a filehandle */
	res_GETXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETXATTR4->status != NFS4_OK)
		return res_GETXATTR4->status;

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (nfs_in_grace()) {
		res_GETXATTR4->status = NFS4ERR_GRACE;
		return res_GETXATTR4->status;
	}
	res_GETXATTR4->status = NFS4ERR_NOTSUPP;
	return res_GETXATTR4->status;
}
/**
 * @brief Free memory allocated for GETXATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_GETXATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_getxattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

/**
 * @brief The NFS4_OP_SETXATTR operation.
 *
 * This functions handles the NFS4_OP_SETXATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */
int nfs4_op_setxattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	SETXATTR4args * const arg_SETXATTR4 = &op->nfs_argop4_u.opsetxattr;
	SETXATTR4res * const res_SETXATTR4 = &resp->nfs_resop4_u.opsetxattr;

	resp->resop = NFS4_OP_SETXATTR;
	res_SETXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "SetXattr type %d len %d name: %s",
		 arg_SETXATTR4->sa_type,
		 arg_SETXATTR4->sa_xattr.xa_name.utf8string_len,
		 arg_SETXATTR4->sa_xattr.xa_name.utf8string_val);

	/* Do basic checks on a filehandle */
	res_SETXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_SETXATTR4->status != NFS4_OK)
		return res_SETXATTR4->status;

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (nfs_in_grace()) {
		res_SETXATTR4->status = NFS4ERR_GRACE;
		return res_SETXATTR4->status;
	}
	res_SETXATTR4->status = NFS4ERR_NOTSUPP;
	return res_SETXATTR4->status;
}
/**
 * @brief Free memory allocated for SETXATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_SETXATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_setxattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

/**
 * @brief The NFS4_OP_LISTXATTR operation.
 *
 * This functions handles the NFS4_OP_LISTXATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */
int nfs4_op_listxattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	LISTXATTR4args * const arg_LISTXATTR4 = &op->nfs_argop4_u.oplistxattr;
	LISTXATTR4res * const res_LISTXATTR4 = &resp->nfs_resop4_u.oplistxattr;

	resp->resop = NFS4_OP_LISTXATTR;
	res_LISTXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "SetXattr max count %d cookie %" PRIu64,
		 arg_LISTXATTR4->la_maxcount, arg_LISTXATTR4->la_cookie);

	/* Do basic checks on a filehandle */
	res_LISTXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
						      false);

	if (res_LISTXATTR4->status != NFS4_OK)
		return res_LISTXATTR4->status;

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (nfs_in_grace()) {
		res_LISTXATTR4->status = NFS4ERR_GRACE;
		return res_LISTXATTR4->status;
	}
	res_LISTXATTR4->status = NFS4ERR_NOTSUPP;
	return res_LISTXATTR4->status;
}
/**
 * @brief Free memory allocated for LISTXATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_LISTXATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_listxattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

/**
 * @brief The NFS4_OP_REMOVEXATTR operation.
 *
 * This functions handles the NFS4_OP_REMOVEXATTR operation in NFSv4. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373-4
 */
int nfs4_op_removexattr(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	REMOVEXATTR4args * const arg_REMOVEXATTR4 =
					&op->nfs_argop4_u.opremovexattr;
	REMOVEXATTR4res * const res_REMOVEXATTR4 =
					&resp->nfs_resop4_u.opremovexattr;

	resp->resop = NFS4_OP_REMOVEXATTR;
	res_REMOVEXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "RemoveXattr len %d name: %s",
		 arg_REMOVEXATTR4->ra_name.utf8string_len,
		 arg_REMOVEXATTR4->ra_name.utf8string_val);

	/* Do basic checks on a filehandle */
	res_REMOVEXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
							false);

	if (res_REMOVEXATTR4->status != NFS4_OK)
		return res_REMOVEXATTR4->status;

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (nfs_in_grace()) {
		res_REMOVEXATTR4->status = NFS4ERR_GRACE;
		return res_REMOVEXATTR4->status;
	}
	res_REMOVEXATTR4->status = NFS4ERR_NOTSUPP;
	return res_REMOVEXATTR4->status;
}
/**
 * @brief Free memory allocated for REMOVEXATTR result
 *
 * This function fres any memory allocated for the result of the
 * NFS4_OP_REMOVEXATTR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_removexattr_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}

