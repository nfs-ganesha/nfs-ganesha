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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

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

#define arg_SECINFO4 op->nfs_argop4_u.opsecinfo
#define res_SECINFO4 resp->nfs_resop4_u.opsecinfo

extern gss_OID_desc krb5oid;

int nfs4_op_secinfo(struct nfs_argop4 *op,
                    compound_data_t *data,
                    struct nfs_resop4 *resp)
{
  char *secinfo_fh_name = NULL;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t *entry_src = NULL;
  struct attrlist attr_secinfo;
  sec_oid4 v5oid = {krb5oid.length, (char *)krb5oid.elements};
  int num_entry = 0;

  resp->resop = NFS4_OP_SECINFO;
  res_SECINFO4.status = NFS4_OK;

  /* Read name from uft8 strings, if one is empty then returns NFS4ERR_INVAL */
  res_SECINFO4.status = nfs4_utf8string2dynamic(&arg_SECINFO4.name,
						UTF8_SCAN_ALL,
						&secinfo_fh_name);
  if (res_SECINFO4.status != NFS4_OK) {
      goto out;
  }

  /*
   * Do basic checks on a filehandle
   * SecInfo is done only on a directory
   */
  res_SECINFO4.status = nfs4_sanity_check_FH(data, DIRECTORY, FALSE);
  if(res_SECINFO4.status != NFS4_OK)
    goto out;

  if (nfs_in_grace())
    {
      res_SECINFO4.status = NFS4ERR_GRACE;
      goto out;
    }

  if((entry_src = cache_inode_lookup(data->current_entry,
                                         secinfo_fh_name,
                                         &attr_secinfo,
                                         data->req_ctx, &cache_status)) == NULL)
    {
      res_SECINFO4.status = nfs4_Errno(cache_status);
      goto out;
    }

  /* get the number of entries */
  if (data->pexport->options & EXPORT_OPTION_AUTH_NONE)
    num_entry++;
  if (data->pexport->options & EXPORT_OPTION_AUTH_UNIX)
    num_entry++;
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_NONE)
    num_entry++;
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_INTG)
    num_entry++;
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
    num_entry++;

  if((res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val =
      gsh_calloc(num_entry, sizeof(secinfo4))) == NULL)
    {
      res_SECINFO4.status = NFS4ERR_SERVERFAULT;
      cache_inode_put(entry_src);
      goto out;
    }

  /* XXX we have the opportunity to associate a preferred security triple
   * with a specific fs/export.  For now, list all implemented. */
  int idx = 0;
  if (data->pexport->options & EXPORT_OPTION_AUTH_NONE)
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].flavor
           = AUTH_NONE;
  if (data->pexport->options & EXPORT_OPTION_AUTH_UNIX)
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].flavor
           = AUTH_UNIX;
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_NONE)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor
           = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx]
           .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_NONE;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx]
           .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
           .secinfo4_u.flavor_info.oid = v5oid;
    }
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_INTG)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor
           = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u
           .flavor_info.service = RPCSEC_GSS_SVC_INTEGRITY;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.
           flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].secinfo4_u
           .flavor_info.oid = v5oid;
    }
  if (data->pexport->options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx]
           .flavor = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx]
           .secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_PRIVACY;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx]
           .secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++]
           .secinfo4_u.flavor_info.oid = v5oid;
    }
  res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_len = idx;

  cache_inode_put(entry_src);

 out:

  if (secinfo_fh_name) {
    gsh_free(secinfo_fh_name);
    secinfo_fh_name = NULL;
  }

  return res_SECINFO4.status;
} /* nfs4_op_secinfo */

/**
 * @brief Free memory allocated for SECINFO result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_SECINFO operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_secinfo_Free(SECINFO4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_secinfo_Free */
