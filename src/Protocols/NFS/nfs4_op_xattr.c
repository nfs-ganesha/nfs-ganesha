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
int nfs4_op_getxattr(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp)
{
	GETXATTR4args * const arg_GETXATTR4 = &op->nfs_argop4_u.opgetxattr;
	GETXATTR4res * const res_GETXATTR4 = &resp->nfs_resop4_u.opgetxattr;
	xattrvalue4 gr_value;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;

	resp->resop = NFS4_OP_GETXATTR;
	res_GETXATTR4->status = NFS4_OK;

	LogDebug(COMPONENT_NFS_V4,
		 "GetXattr len %d name: %s",
		 arg_GETXATTR4->ga_name.utf8string_len,
		 arg_GETXATTR4->ga_name.utf8string_val);

	res_GETXATTR4->GETXATTR4res_u.resok4.gr_value.utf8string_len = 0;
	res_GETXATTR4->GETXATTR4res_u.resok4.gr_value.utf8string_val = NULL;

	gr_value.utf8string_len = XATTR_VALUE_SIZE;
	gr_value.utf8string_val = gsh_malloc(gr_value.utf8string_len);

	/* Do basic checks on a filehandle */
	res_GETXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_GETXATTR4->status != NFS4_OK)
		return res_GETXATTR4->status;

	fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->ga_name,
						    &gr_value);
	if (FSAL_IS_ERROR(fsal_status)) {
		if (fsal_status.major == ERR_FSAL_TOOSMALL) {
			LogDebug(COMPONENT_NFS_V4,
				 "FSAL buffer len %d too small",
				  XATTR_VALUE_SIZE);
			/* Get size of xattr value  */
			gsh_free(gr_value.utf8string_val);
			gr_value.utf8string_len = 0;
			gr_value.utf8string_val = NULL;
			fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->ga_name,
						    &gr_value);
			if (FSAL_IS_ERROR(fsal_status))
				return res_GETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
			LogDebug(COMPONENT_NFS_V4,
				 "FSAL buffer new len %d",
				  gr_value.utf8string_len);
			/* Try again with a bigger buffer */
			gr_value.utf8string_val = gsh_malloc(
						      gr_value.utf8string_len);
			fsal_status = obj_handle->obj_ops->getxattrs(obj_handle,
						    &arg_GETXATTR4->ga_name,
						    &gr_value);
			if (FSAL_IS_ERROR(fsal_status))
				return res_GETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
		} else
			return res_GETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
	}
	res_GETXATTR4->status = NFS4_OK;
	res_GETXATTR4->GETXATTR4res_u.resok4.gr_value.utf8string_len =
							gr_value.utf8string_len;
	res_GETXATTR4->GETXATTR4res_u.resok4.gr_value.utf8string_val =
							gr_value.utf8string_val;
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
	GETXATTR4res * const res_GETXATTR4 = &resp->nfs_resop4_u.opgetxattr;
	GETXATTR4resok *res = &res_GETXATTR4->GETXATTR4res_u.resok4;

	if (res_GETXATTR4->status == NFS4_OK) {
		gsh_free(res->gr_value.utf8string_val);
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
int nfs4_op_setxattr(struct nfs_argop4 *op, compound_data_t *data,
		     struct nfs_resop4 *resp)
{
	SETXATTR4args * const arg_SETXATTR4 = &op->nfs_argop4_u.opsetxattr;
	SETXATTR4res * const res_SETXATTR4 = &resp->nfs_resop4_u.opsetxattr;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;

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
	res_SETXATTR4->SETXATTR4res_u.resok4.sr_info.atomic = false;
	res_SETXATTR4->SETXATTR4res_u.resok4.sr_info.before =
				fsal_get_changeid4(data->current_obj);
	fsal_status = obj_handle->obj_ops->setxattrs(obj_handle,
					arg_SETXATTR4->sa_type,
					&arg_SETXATTR4->sa_xattr.xa_name,
					&arg_SETXATTR4->sa_xattr.xa_value);
	if (FSAL_IS_ERROR(fsal_status))
		return res_SETXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
	res_SETXATTR4->SETXATTR4res_u.resok4.sr_info.after =
				fsal_get_changeid4(data->current_obj);
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
 */
int nfs4_op_listxattr(struct nfs_argop4 *op, compound_data_t *data,
		      struct nfs_resop4 *resp)
{
	LISTXATTR4args * const arg_LISTXATTR4 = &op->nfs_argop4_u.oplistxattr;
	LISTXATTR4res * const res_LISTXATTR4 = &resp->nfs_resop4_u.oplistxattr;
	fsal_status_t fsal_status;
	struct fsal_obj_handle *obj_handle = data->current_obj;
	xattrlist4 list;
	nfs_cookie4 la_cookie;
	verifier4 la_cookieverf;
	bool_t lr_eof;
	component4 *entry;
	int i;
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);

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

	/* Do basic checks on a filehandle */
	res_LISTXATTR4->status = nfs4_sanity_check_FH(data, NO_FILE_TYPE,
									false);
	if (res_LISTXATTR4->status != NFS4_OK)
		return res_LISTXATTR4->status;

	/* Double buf size, one half for compound and on half for names. */
	list.entries = (component4 *)gsh_malloc(2*arg_LISTXATTR4->la_maxcount);
	la_cookie = arg_LISTXATTR4->la_cookie;
	memset(la_cookieverf, 0, NFS4_VERIFIER_SIZE);

	if (la_cookie == 0 && use_cookie_verifier) {
		if (memcmp(la_cookieverf, arg_LISTXATTR4->la_cookieverf,
			   NFS4_VERIFIER_SIZE) != 0) {
			res_LISTXATTR4->status = NFS4ERR_BAD_COOKIE;
			LogFullDebug(COMPONENT_NFS_V4,
				     "Bad cookie");
			return res_LISTXATTR4->status;
		}
	}
	fsal_status = obj_handle->obj_ops->listxattrs(obj_handle,
						arg_LISTXATTR4->la_maxcount,
						&la_cookie,
						&la_cookieverf,
						&lr_eof,
						&list);
	if (FSAL_IS_ERROR(fsal_status)) {
		res_LISTXATTR4->status =
			nfs4_Errno_state(state_error_convert(fsal_status));
		gsh_free(list.entries);
		res_LISTXATTR4->LISTXATTR4res_u.resok4.lr_names.entries = NULL;
		return res_LISTXATTR4->status;
	}
	res_LISTXATTR4->LISTXATTR4res_u.resok4.lr_cookie = la_cookie;
	res_LISTXATTR4->LISTXATTR4res_u.resok4.lr_eof = lr_eof;
	memcpy(res_LISTXATTR4->LISTXATTR4res_u.resok4.lr_cookieverf,
		la_cookieverf, NFS4_VERIFIER_SIZE);
	res_LISTXATTR4->LISTXATTR4res_u.resok4.lr_names = list;
	entry = list.entries;
	for (i = 0; i < list.entryCount; i++) {
		LogFullDebug(COMPONENT_FSAL,
			"entry %d at %p len %d at %p name %s",
			i, entry, entry->utf8string_len,
			entry->utf8string_val, entry->utf8string_val);
		entry += 1;
	}
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
	LISTXATTR4res *res_LISTXATTR4 = &resp->nfs_resop4_u.oplistxattr;
	LISTXATTR4resok *res = &res_LISTXATTR4->LISTXATTR4res_u.resok4;

	if (res_LISTXATTR4->status == NFS4_OK) {
		gsh_free(res->lr_names.entries);
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
int nfs4_op_removexattr(struct nfs_argop4 *op, compound_data_t *data,
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
	res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rr_info.atomic = false;
	res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rr_info.before =
				fsal_get_changeid4(data->current_obj);
	fsal_status = obj_handle->obj_ops->removexattrs(obj_handle,
					&arg_REMOVEXATTR4->ra_name);
	if (FSAL_IS_ERROR(fsal_status))
		return res_REMOVEXATTR4->status = nfs4_Errno_state(
					state_error_convert(fsal_status));
	res_REMOVEXATTR4->REMOVEXATTR4res_u.resok4.rr_info.after =
				fsal_get_changeid4(data->current_obj);
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

