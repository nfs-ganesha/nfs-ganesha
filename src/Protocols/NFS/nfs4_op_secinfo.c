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
 * @file nfs4_op_secinfo.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#include "config.h"
#include "fsal.h"
#include "nfs_core.h"
/*#include "cache_inode.h"*/
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "sal_functions.h"
#include "export_mgr.h"

/**
 * @brief NFSv4 SECINFO operation
 *
 * This function impelments the NFSv4 SECINFO operation.
 *
 * @param[in]     op   Operation reqest
 * @param[in,out] data Compound data
 * @param[out]    resp Response
 *
 * @return NFS status codes.
 */

int nfs4_op_secinfo(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	SECINFO4args * const arg_SECINFO4 = &op->nfs_argop4_u.opsecinfo;
	SECINFO4res * const res_SECINFO4 = &resp->nfs_resop4_u.opsecinfo;
	char *secinfo_fh_name = NULL;
	fsal_status_t fsal_status = {0, 0};
	struct fsal_obj_handle *obj_src = NULL;
	sec_oid4 v5oid = { krb5oid.length, (char *)krb5oid.elements };
	int num_entry = 0;
	struct export_perms save_export_perms = { 0, };
	struct gsh_export *saved_gsh_export = NULL;

	resp->resop = NFS4_OP_SECINFO;
	res_SECINFO4->status = NFS4_OK;

	/* Read name from uft8 strings, if one is empty then returns
	 * NFS4ERR_INVAL
	 */
	res_SECINFO4->status = nfs4_utf8string2dynamic(&arg_SECINFO4->name,
						       UTF8_SCAN_ALL,
						       &secinfo_fh_name);

	if (res_SECINFO4->status != NFS4_OK)
		goto out;

	/* Do basic checks on a filehandle SecInfo is done only on a directory
	 */
	res_SECINFO4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_SECINFO4->status != NFS4_OK)
		goto out;


	fsal_status = fsal_lookup(data->current_obj, secinfo_fh_name, &obj_src);

	if (obj_src == NULL) {
		res_SECINFO4->status = nfs4_Errno_status(fsal_status);
		goto out;
	}

#if 0
	if (obj_src->type == DIRECTORY &&
	    obj_src->object.dir.junction_export != NULL) {
		/* XXX dang handle junction */
		/* Handle junction */
	}
#endif

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

	res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val =
	     gsh_calloc(num_entry, sizeof(secinfo4));

	/**
	 * @todo We have the opportunity to associate a preferred
	 * security triple with a specific fs/export.  For now, list
	 * all implemented.
	 */
	int idx = 0;

	/* List the security flavors in the order we prefer */
	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_PRIV) {
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .flavor = RPCSEC_GSS;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_PRIVACY;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_INTG) {
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx].
		    flavor = RPCSEC_GSS;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_INTEGRITY;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options &
	    EXPORT_OPTION_RPCSEC_GSS_NONE) {
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx].
		    flavor = RPCSEC_GSS;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_NONE;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
		    .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
		    .secinfo4_u.flavor_info.oid = v5oid;
	}

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_UNIX)
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
		    .flavor = AUTH_UNIX;

	if (op_ctx->export_perms->options & EXPORT_OPTION_AUTH_NONE)
		res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
		    .flavor = AUTH_NONE;

	res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_len = idx;

	if (data->minorversion != 0) {
		/* Need to clear out CurrentFH */
		set_current_entry(data, NULL);

		data->currentFH.nfs_fh4_len = 0;

		/* Release CurrentFH reference to export. */
		if (op_ctx->export) {
			put_gsh_export(op_ctx->export);
			op_ctx->export = NULL;
			op_ctx->fsal_export = NULL;
		}

		if (saved_gsh_export != NULL) {
			/* Don't need saved export */
			put_gsh_export(saved_gsh_export);
			saved_gsh_export = NULL;
		}
	}

	res_SECINFO4->status = NFS4_OK;

 out:

	if (saved_gsh_export != NULL) {
		/* Restore export stuff */
		if (op_ctx->export)
			put_gsh_export(op_ctx->export);

		*op_ctx->export_perms = save_export_perms;
		op_ctx->export = saved_gsh_export;
		op_ctx->fsal_export = op_ctx->export->fsal_export;

		/* Restore creds */
		if (nfs_req_creds(data->req) != NFS4_OK) {
			LogCrit(COMPONENT_EXPORT,
				"Failure to restore creds");
		}
	}

	if (obj_src)
		obj_src->obj_ops.put_ref(obj_src);

	if (secinfo_fh_name)
		gsh_free(secinfo_fh_name);

	return res_SECINFO4->status;
}				/* nfs4_op_secinfo */

/**
 * @brief Free memory allocated for SECINFO result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SECINFO operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_secinfo_Free(nfs_resop4 *res)
{
	SECINFO4res *resp = &res->nfs_resop4_u.opsecinfo;

	if ((resp->status == NFS4_OK)
	    && (resp->SECINFO4res_u.resok4.SECINFO4resok_val)) {
		gsh_free(resp->SECINFO4res_u.resok4.SECINFO4resok_val);
		resp->SECINFO4res_u.resok4.SECINFO4resok_val = NULL;
	}
}
