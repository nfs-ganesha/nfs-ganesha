/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    nfs4_op_secinfo.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_secinfo.c : Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "sal_functions.h"

/**
 * nfs4_op_secinfo: The NFS4_OP_SECINFO
 * 
 * Implements the NFS4_OP_SECINFO
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
#define arg_SECINFO4 op->nfs_argop4_u.opsecinfo
#define res_SECINFO4 resp->nfs_resop4_u.opsecinfo

extern gss_OID_desc krb5oid;

int nfs4_op_secinfo(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_secinfo";

  fsal_name_t secinfo_fh_name;
  cache_inode_status_t cache_status;
  cache_entry_t *entry_src=NULL;
  fsal_attrib_list_t attr_secinfo;
  sec_oid4 v5oid = {krb5oid.length, (char *)krb5oid.elements};
  int num_entry = 0;

  resp->resop = NFS4_OP_SECINFO;
  res_SECINFO4.status = NFS4_OK;

  /* Read name from uft8 strings, if one is empty then returns NFS4ERR_INVAL */
  if(arg_SECINFO4.name.utf8string_len == 0)
    {
      res_SECINFO4.status = NFS4ERR_INVAL;
      return res_SECINFO4.status;
    }

  /*
   * Do basic checks on a filehandle
   * SecInfo is done only on a directory
   */
  res_SECINFO4.status = nfs4_sanity_check_FH(data, DIRECTORY);
  if(res_SECINFO4.status != NFS4_OK)
    return res_SECINFO4.status;

  if (nfs_in_grace())
    {
      res_SECINFO4.status = NFS4ERR_GRACE;
      return res_SECINFO4.status;
    }

  /* get the names from the RPC input */
  if((cache_status =
      cache_inode_error_convert(FSAL_buffdesc2name
                                ((fsal_buffdesc_t *) & arg_SECINFO4.name,
                                 &secinfo_fh_name))) != CACHE_INODE_SUCCESS)
    {
      res_SECINFO4.status = NFS4ERR_INVAL;
      return res_SECINFO4.status;
    }

  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      /* Cheat and pretend we are a LOOKUP, this will
       * set up the currentFH and related fields in the
       * compound data. This includes calling nfs4_MakeCred.
       */
      if ((nfs4_op_lookup_pseudo(op, data, resp) != NFS4_OK) &&
          (res_SECINFO4.status != NFS4ERR_WRONGSEC))
        {
          /* reuse lookup result, need to set the correct OP */
          resp->resop = NFS4_OP_SECINFO;
          return res_SECINFO4.status;
        }
        /* reuse lookup routine, need to set the correct OP */
        resp->resop = NFS4_OP_SECINFO;
    }
  else 
    {
      if((entry_src = cache_inode_lookup(data->current_entry,
                                         &secinfo_fh_name,
                                         &attr_secinfo,
                                         data->pcontext, &cache_status)) == NULL)
      {
        res_SECINFO4.status = nfs4_Errno(cache_status);
        return res_SECINFO4.status;
      }
    }

  /* get the number of entries */
  if (data->export_perms.options & EXPORT_OPTION_AUTH_NONE)
    num_entry++;
  if (data->export_perms.options & EXPORT_OPTION_AUTH_UNIX)
    num_entry++;
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE)
    num_entry++;
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG)
    num_entry++;
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
    num_entry++;

  if((res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val =
      gsh_calloc(num_entry, sizeof(secinfo4))) == NULL)
    {
      res_SECINFO4.status = NFS4ERR_SERVERFAULT;
      if (entry_src)
        cache_inode_put(entry_src);
      return res_SECINFO4.status;
    }

  /* XXX we have the opportunity to associate a preferred security triple
   * with a specific fs/export.  For now, list all implemented. */
  int idx=0;
  if (data->export_perms.options & EXPORT_OPTION_AUTH_NONE)
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].flavor = AUTH_NONE;
  if (data->export_perms.options & EXPORT_OPTION_AUTH_UNIX)
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].flavor = AUTH_UNIX;
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_NONE)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_NONE;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
    }
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_INTG)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_INTEGRITY;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
    }
  if (data->export_perms.options & EXPORT_OPTION_RPCSEC_GSS_PRIV)
    {
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].flavor = RPCSEC_GSS;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.service = RPCSEC_GSS_SVC_PRIVACY;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx].secinfo4_u.flavor_info.qop = GSS_C_QOP_DEFAULT;
      res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_val[idx++].secinfo4_u.flavor_info.oid = v5oid;
    }
  res_SECINFO4.SECINFO4res_u.resok4.SECINFO4resok_len = idx;

  if (entry_src)
    cache_inode_put(entry_src);

  res_SECINFO4.status = NFS4_OK;
  return res_SECINFO4.status;
}                               /* nfs4_op_secinfo */

/**
 * nfs4_op_secinfo_Free: frees what was allocared to handle nfs4_op_secinfo.
 *
 * Frees what was allocared to handle nfs4_op_secinfo.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_secinfo_Free(SECINFO4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_secinfo_Free */
