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
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "sal_functions.h"
#include "export_mgr.h"

#ifdef _HAVE_GSSAPI
/* flavor, oid len, qop, service */
#define GSS_RESP_SIZE (4 * BYTES_PER_XDR_UNIT)
#endif
/* nfsstat4, resok_len, 2 flavors
 * NOTE this requires space for up to 2 extra xdr units if the export doesn't
 * allow AUTH_NONE and/or AUTH_UNIX. The response size is overall so small
 * this op should never be the cause of overflow of maxrespsize...
 */
#define RESP_SIZE (4 * BYTES_PER_XDR_UNIT)

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
	secinfo4 *resok_val;
#ifdef _HAVE_GSSAPI
	sec_oid4 v5oid = { krb5oid.length, (char *)krb5oid.elements };
#endif /* _HAVE_GSSAPI */
	int num_entry = 0;
	uint32_t resp_size = RESP_SIZE;

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
#ifdef _HAVE_GSSAPI
	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_NONE)
		num_entry++;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_INTG)
		num_entry++;

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_PRIV)
		num_entry++;

	resp_size += (RNDUP(krb5oid.length) + GSS_RESP_SIZE) * num_entry;
#endif

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_NONE)
		num_entry++;

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_UNIX)
		num_entry++;

	/* Check for space in response. */
	res_SECINFO_NO_NAME4->status = check_resp_room(data, resp_size);

	if (res_SECINFO_NO_NAME4->status != NFS4_OK)
		goto out;

	data->op_resp_size = resp_size;

	resok_val = gsh_calloc(num_entry, sizeof(secinfo4));

	res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.SECINFO4resok_val
		= resok_val;

	/**
	 * @todo We give here the order in which the client should try
	 * different authentifications. Might want to give it in the
	 * order given in the config.
	 */
	int idx = 0;

#ifdef _HAVE_GSSAPI
	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_PRIV) {
		resok_val[idx].flavor = RPCSEC_GSS;
		resok_val[idx].secinfo4_u.flavor_info.service =
			RPCSEC_GSS_SVC_PRIVACY;
		resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_INTG) {
		resok_val[idx].flavor = RPCSEC_GSS;
		resok_val[idx].secinfo4_u.flavor_info.service =
			RPCSEC_GSS_SVC_INTEGRITY;
		resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_NONE) {
		resok_val[idx].flavor = RPCSEC_GSS;
		resok_val[idx].secinfo4_u.flavor_info.service =
			RPCSEC_GSS_SVC_NONE;
		resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
	}
#endif /* _HAVE_GSSAPI */

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_UNIX)
		resok_val[idx++].flavor = AUTH_UNIX;

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_NONE)
		resok_val[idx++].flavor = AUTH_NONE;

	res_SECINFO_NO_NAME4->SECINFO4res_u.resok4.SECINFO4resok_len = idx;

	/* Need to clear out CurrentFH */
	set_current_entry(data, NULL);

	data->currentFH.nfs_fh4_len = 0;

	/* Release CurrentFH reference to export. */
	if (op_ctx->ctx_export) {
		put_gsh_export(op_ctx->ctx_export);
		op_ctx->ctx_export = NULL;
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
}
