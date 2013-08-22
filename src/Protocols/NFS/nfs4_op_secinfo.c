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
 * @file nfs4_op_secinfo.c
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
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "sal_functions.h"

/**
 * @brief The NFS4_OP_SECINFO
 *
 * Implements the NFS4_OP_SECINFO operation.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 373-4
 */

extern gss_OID_desc krb5oid;

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

int
nfs4_op_secinfo(struct nfs_argop4 *op,
                compound_data_t *data,
                struct nfs_resop4 *resp)
{
	SECINFO4args *const arg_SECINFO4 = &op->nfs_argop4_u.opsecinfo;
	SECINFO4res *const res_SECINFO4 = &resp->nfs_resop4_u.opsecinfo;
        char *secinfo_fh_name = NULL;
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        cache_entry_t *entry_src = NULL;
        sec_oid4 v5oid = {krb5oid.length, (char *)krb5oid.elements};
        int num_entry = 0;

        resp->resop = NFS4_OP_SECINFO;
        res_SECINFO4->status = NFS4_OK;

        /* Read name from uft8 strings, if one is empty then returns
           NFS4ERR_INVAL */
        res_SECINFO4->status = nfs4_utf8string2dynamic(&arg_SECINFO4->name,
						       UTF8_SCAN_ALL,
						       &secinfo_fh_name);
        if (res_SECINFO4->status != NFS4_OK) {
                goto out;
        }

        /* Do basic checks on a filehandle SecInfo is done only on a
           directory */
        res_SECINFO4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);
        if (res_SECINFO4->status != NFS4_OK) {
                goto out;
        }

	if(nfs4_Is_Fh_Pseudo(&(data->currentFH))) {
		/* Cheat and pretend we are a LOOKUP, this will
		 * set up the currentFH and related fields in the
		 * compound data. This includes calling nfs4_MakeCred.
		 */
		if ((nfs4_op_lookup_pseudo(op, data, resp) != NFS4_OK) &&
		    (res_SECINFO4->status != NFS4ERR_WRONGSEC)) {
			/* reuse lookup result, need to set the correct OP */
			resp->resop = NFS4_OP_SECINFO;
			return res_SECINFO4->status;
		}
		/* reuse lookup result, need to set the correct OP */
		resp->resop = NFS4_OP_SECINFO;
	} else {
		cache_status = cache_inode_lookup(data->current_entry,
						  secinfo_fh_name,
						  data->req_ctx,
						  &entry_src);
		if (entry_src == NULL) {
			res_SECINFO4->status = nfs4_Errno(cache_status);
			goto out;
	       }
	}

        /* Get the number of entries */
        if (data->export_perms.options & EXPORT_OPTION_AUTH_NONE) {
                num_entry++;
        }
        if (data->export_perms.options & EXPORT_OPTION_AUTH_UNIX) {
                num_entry++;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE) {
                num_entry++;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG) {
                num_entry++;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV) {
                num_entry++;
        }

        if ((res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val =
             gsh_calloc(num_entry, sizeof(secinfo4))) == NULL) {
                res_SECINFO4->status = NFS4ERR_SERVERFAULT;
                if(entry_src != NULL)
                	cache_inode_put(entry_src);
                goto out;
        }

        /**
         * @todo We have the opportunity to associate a preferred
         * security triple with a specific fs/export.  For now, list
         * all implemented.
         */
        int idx = 0;
        if (data->export_perms.options & EXPORT_OPTION_AUTH_NONE) {
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
                        .flavor = AUTH_NONE;
        }
        if (data->export_perms.options & EXPORT_OPTION_AUTH_UNIX) {
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
                        .flavor = AUTH_UNIX;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE) {
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor
                        = RPCSEC_GSS;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_NONE;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
                        .secinfo4_u.flavor_info.oid = v5oid;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG) {
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor
                        = RPCSEC_GSS;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.service
                        = RPCSEC_GSS_SVC_INTEGRITY;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
                        .secinfo4_u.flavor_info.oid = v5oid;
        }
        if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV) {
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .flavor = RPCSEC_GSS;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.service
                        = RPCSEC_GSS_SVC_PRIVACY;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx]
                        .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
                res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
                        .secinfo4_u.flavor_info.oid = v5oid;
        }
        res_SECINFO4->SECINFO4res_u.resok4.SECINFO4resok_len = idx;

        if(entry_src != NULL)
	        cache_inode_put(entry_src);

out:

        if (secinfo_fh_name) {
                gsh_free(secinfo_fh_name);
                secinfo_fh_name = NULL;
        }

        return res_SECINFO4->status;
} /* nfs4_op_secinfo */

/**
 * @brief Free memory allocated for SECINFO result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SECINFO operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_secinfo_Free(nfs_resop4 * res)
{
	SECINFO4res *resp = &res->nfs_resop4_u.opsecinfo;

        if ((resp->status = NFS4_OK) &&
            (resp->SECINFO4res_u.resok4.SECINFO4resok_val)) {
                gsh_free(resp->SECINFO4res_u.resok4.SECINFO4resok_val);
                resp->SECINFO4res_u.resok4.SECINFO4resok_val = NULL;
        }
        return;
} /* nfs4_op_secinfo_Free */
