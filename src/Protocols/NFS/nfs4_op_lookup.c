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
 * @file    nfs4_op_lookup.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
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
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 * @brief NFS4_OP_LOOKUP
 *
 * This function implments the NFS4_OP_LOOKUP operation, which looks
 * a filename up in the FSAL.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 368-9
 *
 */
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup(struct nfs_argop4 *op,
                   compound_data_t *data,
                   struct nfs_resop4 *resp)
{
  char strname[MAXNAMLEN];
#ifndef _NO_XATTRD
  char objname[MAXNAMLEN];
#endif
  fsal_name_t            name;
  unsigned int           xattr_found = FALSE;
  cache_entry_t        * dir_entry = NULL;
  cache_entry_t        * file_entry = NULL;
  fsal_attrib_list_t     attrlookup;
  cache_inode_status_t   cache_status;

  resp->resop = NFS4_OP_LOOKUP;
  res_LOOKUP4.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_LOOKUP4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_LOOKUP4.status != NFS4_OK)
    return res_LOOKUP4.status;

  /* Check for empty name */
  if(op->nfs_argop4_u.oplookup.objname.utf8string_len == 0 ||
     op->nfs_argop4_u.oplookup.objname.utf8string_val == NULL)
    {
      res_LOOKUP4.status = NFS4ERR_INVAL;
      return res_LOOKUP4.status;
    }

  /* Check for name too long */
  if(op->nfs_argop4_u.oplookup.objname.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_LOOKUP4.status = NFS4ERR_NAMETOOLONG;
      return res_LOOKUP4.status;
    }

  /* If Filehandle points to a pseudo fs entry, manage it via pseudofs specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_lookup_pseudo(op, data, resp);

  if (nfs_export_check_security(data->reqp, data->pexport) == FALSE)
    {
      res_LOOKUP4.status = NFS4ERR_PERM;
      return res_LOOKUP4.status;
    }

#ifndef _NO_XATTRD
  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_lookup_xattr(op, data, resp);
#endif

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(strname, sizeof(strname), &arg_LOOKUP4.objname);

#ifndef _NO_XATTRD
  /* Is this a .xattr.d.<object> name ? */
  if(nfs_XattrD_Name(strname, objname))
    {
      strcpy(strname, objname);
      xattr_found = TRUE;
    }
#endif

  if((cache_status = cache_inode_error_convert(FSAL_str2name(strname,
                                                             MAXNAMLEN,
                                                             &name))) !=
     CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* No 'cd .' is allowed return NFS4ERR_BADNAME in this case */
  /* No 'cd .. is allowed, return EINVAL in this case. NFS4_OP_LOOKUPP should be use instead */
  if(!FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_LOOKUP4.status = NFS4ERR_BADNAME;
      return res_LOOKUP4.status;
    }

  /* Do the lookup in the HPSS Namespace */
  file_entry = NULL;
  dir_entry = data->current_entry;

  /* Sanity check: dir_entry should be ACTUALLY a directory */
  if(dir_entry->type != DIRECTORY)
    {
      /* This is not a directory */
      if(dir_entry->type == SYMBOLIC_LINK)
        res_LOOKUP4.status = NFS4ERR_SYMLINK;
      else
        res_LOOKUP4.status = NFS4ERR_NOTDIR;

      /* Return failed status */
      return res_LOOKUP4.status;
    }

  /* BUGAZOMEU: Faire la gestion des cross junction traverse */
  if((file_entry = cache_inode_lookup(dir_entry,
                                      &name,
                                      &attrlookup,
                                      data->req_ctx->creds,
                                      &cache_status)) != NULL)
    {
      /* Convert it to a file handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, file_entry->obj_handle, data))
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          cache_inode_put(file_entry);
          return res_LOOKUP4.status;
        }

      /* Copy this to the mounted on FH (if no junction is traversed */
      memcpy(data->mounted_on_FH.nfs_fh4_val,
             data->currentFH.nfs_fh4_val,
             data->currentFH.nfs_fh4_len);
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

#if 0
      print_buff((char *)&file_entry->handle, sizeof(fsal_handle_t));
      print_buff((char *)&dir_entry->handle, sizeof(fsal_handle_t));
#endif
      if(isFullDebug(COMPONENT_NFS_V4))
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "name=%s  dir_pentry=%p, obj_handle=%p, "
                       "looked up file_pentry=%p, obj_handle=%p",
                       strname, dir_entry, file_entry,
                       dir_entry->obj_handle, file_entry->obj_handle);
        }
      LogHandleNFS4("NFS4 LOOKUP CURRENT FH: ", &data->currentFH);

      /* Release dir_entry, as it is not reachable from anywhere in
         compound after this function returns.  Count on later
         operations or nfs4_Compound to clean up current_entry. */

      if (dir_entry)
        cache_inode_put(dir_entry);

      /* Keep the pointer within the compound data */
      data->current_entry = file_entry;
      data->current_filetype = file_entry->type;

      /* Return successfully */
      res_LOOKUP4.status = NFS4_OK;

#ifndef _NO_XATTRD
      /* If this is a xattr ghost directory name, update the FH */
      if(xattr_found == TRUE)
        res_LOOKUP4.status = nfs4_fh_to_xattrfh(&(data->currentFH), &(data->currentFH));
#endif

      if((data->current_entry->type == DIRECTORY) &&
         (data->current_entry->object.dir.referral != NULL))
        {
          if(!nfs4_Set_Fh_Referral(&(data->currentFH)))
            {
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              cache_inode_put(file_entry);
              return res_LOOKUP4.status;
            }
        }

      return NFS4_OK;

    }

  /* If the part of the code is reached, then something wrong occured in the lookup process, status is not HPSS_E_NOERROR 
   * and contains the code for the error */

  res_LOOKUP4.status = nfs4_Errno(cache_status);

  return res_LOOKUP4.status;
}                               /* nfs4_op_lookup */

/**
 * @brief Free memory allocated for LOOKUP result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOOKUP operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_lookup_Free(LOOKUP4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_lookup_Free */
