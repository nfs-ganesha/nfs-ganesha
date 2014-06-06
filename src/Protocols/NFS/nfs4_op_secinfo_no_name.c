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
 * @file nfs4_op_secinfo_no_name.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "sal_functions.h"
#include "export_mgr.h"

/**
 * @brief NFSv4 SECINFO_NO_NAME operation
 *
 * This function impelments the NFSv4 SECINFO_NO_NAME operation.
 *
 * @param[in]     op   Operation reqest
 * @param[in,out] data Compound data
 * @param[out]    resp Response
 *
 * @return NFS status codes.
 */

int nfs4_op_secinfo_no_name(struct nfs_argop4 *op, compound_data_t *data,
			    struct nfs_resop4 *resp)
{
	SECINFO_NO_NAME4res * const res_SECINFO_NO_NAME4 =
	    &resp->nfs_resop4_u.opsecinfo_no_name;
	sec_oid4 v5oid = { krb5oid.length, (char *)krb5oid.elements };
	int num_entry = 0;

	res_SECINFO_NO_NAME4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_SECINFO_NO_NAME4->status =
	    nfs4_sanity_check_FH(data, NO_FILE_TYPE, false);

	if (res_SECINFO_NO_NAME4->status != NFS4_OK)
		goto out;

	if (op->nfs_argop4_u.opsecinfo_no_name == SECINFO_STYLE4_PARENT) {
		/* Use LOOKUPP to get the parent into CurrentFH. */
		res_SECINFO_NO_NAME4->status = nfs4_op_lookupp(op, data, resp);

		if (res_SECINFO_NO_NAME4->status != NFS4_OK)
			goto out;
	}

	/* Get the number of entries */
	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_NONE)
		num_entry++;

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_UNIX)
		num_entry++;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_NONE)
		num_entry++;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_INTG)
		num_entry++;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_PRIV)
		num_entry++;

	res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.SECINFO4resok_val =
	     gsh_calloc(num_entry, sizeof(secinfo4));

	if (res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.SECINFO4resok_val
	    == NULL) {
		res_SECINFO_NO_NAME4->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/**
	 * @todo We have the opportunity to associate a preferred
	 * security triple with a specific fs/export.  For now, list
	 * all implemented.
	 */
	int idx = 0;
	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_NONE)
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx++].flavor = AUTH_NONE;

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_UNIX)
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx++].flavor = AUTH_UNIX;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_NONE) {
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx].flavor = RPCSEC_GSS;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_NONE;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_INTG) {
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx].flavor = RPCSEC_GSS;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_INTEGRITY;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_PRIV) {
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .flavor = RPCSEC_GSS;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_PRIVACY;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.
		    SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.SECINFO4resok_len = idx;

	/* Need to clear out CurrentFH */
	set_current_entry(data, NULL, false);

	data->currentFH.nfs_fh4_len = 0;

	/* Release CurrentFH reference to export. */
	if (op_ctx->export) {
		put_gsh_export(op_ctx->export);
		op_ctx->export = NULL;
		op_ctx->fsal_export = NULL;
	}

	res_SECINFO_NO_NAME4->status = NFS4_OK;

 out:

	resp->resop = NFS4_OP_SECINFO_NO_NAME;

	return res_SECINFO_NO_NAME4->status;
}				/* nfs4_op_secinfo_no_name */

/**
 * @brief Free memory allocated for SECINFO_NO_NAME result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SECINFO_NO_NAME operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_secinfo_no_name_Free(nfs_resop4 *res)
{
	SECINFO_NO_NAME4res *resp = &res->nfs_resop4_u.opsecinfo_no_name;

	if ((resp->status == NFS4_OK)
	    && (resp->SECINFO4res_u.resok4.SECINFO4resok_val)) {
		gsh_free(resp->SECINFO4res_u.resok4.SECINFO4resok_val);
		resp->SECINFO4res_u.resok4.SECINFO4resok_val = NULL;
	}
	return;
}				/* nfs4_op_secinfo_no_name_Free */
