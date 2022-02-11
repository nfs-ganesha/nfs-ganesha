// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM Corporation, 2015
 *  Contributor: Marc Eshel <eshel@us.ibm.com>
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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "sal_functions.h"
#include "nfs_creds.h"

#define XATTR_VALUE_SIZE 1024

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
 */
enum nfs_req_result nfs4_op_getxattr(struct nfs_argop4 *op,
				     compound_data_t *data,
				     struct nfs_resop4 *resp)
{
	GETXATTR4args * const arg_GETXATTR4 = &op->nfs_argop4_u.opgetxattr;
	GETXATTR4res * const res_GETXATTR4 = &resp->nfs_resop4_u.opgetxattr;
	xattrvalue4 gxr_value;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;
	uint32_t resp_size;

	resp->resop = NFS4_OP_GETXATTR;
	res_GETXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "GetXattr name: %.*s",
		 arg_GETXATTR4->gxa_name.utf8string_len,
		 arg_GETXATTR4->gxa_name.utf8string_val);

	res_GETXATTR4->GETXATTR4res_u.resok4.gxr_value.utf8string_len = 0;
	res_GETXATTR4->GETXATTR4res_u.resok4.gxr_value.utf8string_val = NULL;

	gxr_value.utf8string_len = XATTR_VALUE_SIZE;
	gxr_value.utf8string_val = gsh_malloc(gxr_value.utf8string_len + 1);

	/* Do basic checks on a filehandle */
	res_GETXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETXATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	if (!(op_ctx->fsal_export->exp_ops.fs_supported_attrs
		(op_ctx->fsal_export) & ATTR4_XATTR)) {

		res_GETXATTR4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->gxa_name,
						    &gxr_value);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_XATTR2BIG) {
			LogDebug(COMPONENT_NFS_V4,
				 "FSAL buffer len %d too small",
				  XATTR_VALUE_SIZE);
			/* Get size of xattr value  */
			gsh_free(gxr_value.utf8string_val);
			gxr_value.utf8string_len = 0;
			gxr_value.utf8string_val = NULL;
			fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->gxa_name,
						    &gxr_value);

			if (FSAL_IS_ERROR(fsal_status)) {
				res_GETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
				return NFS_REQ_ERROR;
			}

			LogDebug(COMPONENT_NFS_V4,
				 "FSAL buffer new len %d",
				  gxr_value.utf8string_len);
			/* Try again with a bigger buffer */
			gxr_value.utf8string_val = gsh_malloc(
						gxr_value.utf8string_len + 1);
			fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->gxa_name,
						    &gxr_value);

			if (FSAL_IS_ERROR(fsal_status)) {
				res_GETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
				return NFS_REQ_ERROR;
			}
		} else {
			res_GETXATTR4->status = nfs4_Errno_status(fsal_status);
			return NFS_REQ_ERROR;
		}
	}


	resp_size = sizeof(nfsstat4) + sizeof(uint32_t) +
			RNDUP(gxr_value.utf8string_len);
	res_GETXATTR4->status = check_resp_room(data, resp_size);
	if (res_GETXATTR4->status != NFS4_OK) {
		gsh_free(gxr_value.utf8string_val);
		return NFS_REQ_ERROR;
	}

	res_GETXATTR4->GETXATTR4res_u.resok4.gxr_value = gxr_value;
	return NFS_REQ_OK;
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
	GETXATTR4res * const res_GETXATTR4 = &resp->nfs_resop4_u.opgetxattr;
	GETXATTR4resok *res = &res_GETXATTR4->GETXATTR4res_u.resok4;

	if (res_GETXATTR4->status == NFS4_OK) {
		gsh_free(res->gxr_value.utf8string_val);
	}
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
 */
enum nfs_req_result nfs4_op_setxattr(struct nfs_argop4 *op,
				     compound_data_t *data,
				     struct nfs_resop4 *resp)
{
	SETXATTR4args * const arg_SETXATTR4 = &op->nfs_argop4_u.opsetxattr;
	SETXATTR4res * const res_SETXATTR4 = &resp->nfs_resop4_u.opsetxattr;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;

	resp->resop = NFS4_OP_SETXATTR;

	LogDebug(COMPONENT_NFS_V4,
		 "SetXattr option=%d key=%.*s",
		 arg_SETXATTR4->sxa_option,
		 arg_SETXATTR4->sxa_key.utf8string_len,
		 arg_SETXATTR4->sxa_key.utf8string_val);

	/* Do basic checks on a filehandle */
	res_SETXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);
	if (res_SETXATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	if (!(op_ctx->fsal_export->exp_ops.fs_supported_attrs
		(op_ctx->fsal_export) & ATTR4_XATTR)) {

		res_SETXATTR4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (!nfs_get_grace_status(false)) {
		res_SETXATTR4->status = NFS4ERR_GRACE;
		return NFS_REQ_ERROR;
	}

	res_SETXATTR4->SETXATTR4res_u.resok4.sxr_info.atomic = false;
	res_SETXATTR4->SETXATTR4res_u.resok4.sxr_info.before =
				fsal_get_changeid4(data->current_obj);
	fsal_status = obj_handle->obj_ops->setxattrs(obj_handle,
					arg_SETXATTR4->sxa_option,
					&arg_SETXATTR4->sxa_key,
					&arg_SETXATTR4->sxa_value);
	if (FSAL_IS_ERROR(fsal_status))
		res_SETXATTR4->status = nfs4_Errno_status(fsal_status);
	else
		res_SETXATTR4->SETXATTR4res_u.resok4.sxr_info.after =
				fsal_get_changeid4(data->current_obj);
	nfs_put_grace_status();
	return nfsstat4_to_nfs_req_result(res_SETXATTR4->status);
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
 */
enum nfs_req_result nfs4_op_listxattr(struct nfs_argop4 *op,
				      compound_data_t *data,
				      struct nfs_resop4 *resp)
{
	LISTXATTR4args * const arg_LISTXATTR4 = &op->nfs_argop4_u.oplistxattr;
	LISTXATTR4res * const res_LISTXATTR4 = &resp->nfs_resop4_u.oplistxattr;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;
	xattrlist4 list = { 0 };
	nfs_cookie4 lxa_cookie = arg_LISTXATTR4->lxa_cookie;
	bool_t lxr_eof;
	component4 *entry;
	uint32_t maxcount, overhead, resp_size;
	int i;

	resp->resop = NFS4_OP_LISTXATTR;
	res_LISTXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "SetXattr max count %d cookie %" PRIu64,
		 arg_LISTXATTR4->lxa_maxcount, lxa_cookie);

	/* Do basic checks on a filehandle */
	res_LISTXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
						      false);
	if (res_LISTXATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	/* Do basic checks on a filehandle */
	res_LISTXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
									false);
	if (res_LISTXATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	if (!(op_ctx->fsal_export->exp_ops.fs_supported_attrs
		(op_ctx->fsal_export) & ATTR4_XATTR)) {

		res_LISTXATTR4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	/*
	 * Send the FSAL a maxcount for the lxr_names field. Each name
	 * takes 4 bytes (for the length field) + length of the actual
	 * data (sans NULL terminators). The names returned should have
	 * the qualifying prefix stripped off (that is, no "user." prefix
	 * on the names).
	 */
	overhead = sizeof(nfs_cookie4) + RNDUP(sizeof(bool));

	/* Is this maxcount too small for even the tiniest xattr name? */
	if (arg_LISTXATTR4->lxa_maxcount <
			(overhead + sizeof(uint32_t) + RNDUP(1))) {
		res_LISTXATTR4->status = NFS4ERR_TOOSMALL;
		return NFS_REQ_ERROR;
	}

	maxcount = arg_LISTXATTR4->lxa_maxcount - overhead;
	fsal_status = obj_handle->obj_ops->listxattrs(obj_handle,
						maxcount,
						&lxa_cookie,
						&lxr_eof,
						&list);
	if (FSAL_IS_ERROR(fsal_status)) {
		res_LISTXATTR4->status = nfs4_Errno_status(fsal_status);
		res_LISTXATTR4->LISTXATTR4res_u.resok4.lxr_names.xl4_entries
									= NULL;
		return NFS_REQ_ERROR;
	}

	entry = list.xl4_entries;
	resp_size = sizeof(nfsstat4) + sizeof(nfs_cookie4) +
				list.xl4_count * sizeof(uint32_t) +
				RNDUP(sizeof(bool));

	for (i = 0; i < list.xl4_count; i++) {
		LogDebug(COMPONENT_FSAL, "entry %d len %d name %.*s",
			i, entry->utf8string_len,
			entry->utf8string_len, entry->utf8string_val);
		resp_size += RNDUP(entry->utf8string_len);
		entry += 1;
	}

	res_LISTXATTR4->status = check_resp_room(data, resp_size);
	if (res_LISTXATTR4->status != NFS4_OK) {
		for (i = 0; i < list.xl4_count; i++)
			gsh_free(list.xl4_entries[i].utf8string_val);
		gsh_free(list.xl4_entries);
		return NFS_REQ_ERROR;
	}

	res_LISTXATTR4->LISTXATTR4res_u.resok4.lxr_cookie = lxa_cookie;
	res_LISTXATTR4->LISTXATTR4res_u.resok4.lxr_eof = lxr_eof;
	res_LISTXATTR4->LISTXATTR4res_u.resok4.lxr_names = list;

	return NFS_REQ_OK;
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
	LISTXATTR4res * const res_LISTXATTR4 = &resp->nfs_resop4_u.oplistxattr;
	xattrlist4 *names = &res_LISTXATTR4->LISTXATTR4res_u.resok4.lxr_names;
	int i;

	if (res_LISTXATTR4->status == NFS4_OK) {
		for (i = 0; i < names->xl4_count; i++)
			gsh_free(names->xl4_entries[i].utf8string_val);
		gsh_free(names->xl4_entries);
	}
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
enum nfs_req_result nfs4_op_removexattr(struct nfs_argop4 *op,
					compound_data_t *data,
					struct nfs_resop4 *resp)
{
	REMOVEXATTR4args * const arg_REMOVEXATTR4 =
					&op->nfs_argop4_u.opremovexattr;
	REMOVEXATTR4res * const res_REMOVEXATTR4 =
					&resp->nfs_resop4_u.opremovexattr;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;

	resp->resop = NFS4_OP_REMOVEXATTR;
	res_REMOVEXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "RemoveXattr len %d name: %s",
		 arg_REMOVEXATTR4->rxa_name.utf8string_len,
		 arg_REMOVEXATTR4->rxa_name.utf8string_val);

	/* Do basic checks on a filehandle */
	res_REMOVEXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
							false);
	if (res_REMOVEXATTR4->status != NFS4_OK)
		return NFS_REQ_ERROR;

	if (!(op_ctx->fsal_export->exp_ops.fs_supported_attrs
		(op_ctx->fsal_export) & ATTR4_XATTR)) {

		res_REMOVEXATTR4->status = NFS4ERR_NOTSUPP;
		return NFS_REQ_ERROR;
	}

	/* Don't allow attribute change while we are in grace period.
	 * Required for delegation reclaims and may be needed for other
	 * reclaimable states as well.
	 */
	if (!nfs_get_grace_status(false)) {
		res_REMOVEXATTR4->status = NFS4ERR_GRACE;
		return NFS_REQ_ERROR;
	}

	res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rxr_info.atomic = false;
	res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rxr_info.before =
				fsal_get_changeid4(data->current_obj);
	fsal_status = obj_handle->obj_ops->removexattrs(obj_handle,
					&arg_REMOVEXATTR4->rxa_name);
	if (FSAL_IS_ERROR(fsal_status))
		res_REMOVEXATTR4->status = nfs4_Errno_status(fsal_status);
	else
		res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rxr_info.after =
				fsal_get_changeid4(data->current_obj);
	nfs_put_grace_status();
	return nfsstat4_to_nfs_req_result(res_REMOVEXATTR4->status);
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
