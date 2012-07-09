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
 * \file    nfs4_op_remove.c
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

/**
 * @brief The NFS4_OP_REMOVE operation.
 *
 * This function implements the NFS4_OP_REMOVE operation in
 * NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 372-3
 */

#define arg_REMOVE4 op->nfs_argop4_u.opremove
#define res_REMOVE4 resp->nfs_resop4_u.opremove

int nfs4_op_remove(struct nfs_argop4 *op,
                   compound_data_t *data,
                   struct nfs_resop4 *resp)
{
  cache_entry_t        * parent_entry = NULL;
  fsal_attrib_list_t     attr_parent;
  fsal_name_t            name;
  cache_inode_status_t   cache_status;

  resp->resop = NFS4_OP_REMOVE;
  res_REMOVE4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * Delete arg_REMOVE4.target in directory pointed by currentFH
   * Make sure the currentFH is pointed a directory
   */
  res_REMOVE4.status = nfs4_sanity_check_FH(data, DIRECTORY);
  if(res_REMOVE4.status != NFS4_OK)
    return res_REMOVE4.status;

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_REMOVE4.status = NFS4ERR_ROFS;
      return res_REMOVE4.status;
    }

  if (nfs_in_grace())
    {
      res_REMOVE4.status = NFS4ERR_GRACE;
      return res_REMOVE4.status;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_remove_xattr(op, data, resp);

  /* Get the parent entry (aka the current one in the compound data) */
  parent_entry = data->current_entry;

  /* We have to keep track of the 'change' file attribute for reply structure */
  memset(&(res_REMOVE4.REMOVE4res_u.resok4.cinfo.before), 0, sizeof(changeid4));
  res_REMOVE4.REMOVE4res_u.resok4.cinfo.before =
       cache_inode_get_changeid4(parent_entry);

  /* Check for name length */
  if(arg_REMOVE4.target.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_REMOVE4.status = NFS4ERR_NAMETOOLONG;
      return res_REMOVE4.status;
    }

  /* get the filename from the argument, it should not be empty */
  if(arg_REMOVE4.target.utf8string_len == 0)
    {
      res_REMOVE4.status = NFS4ERR_INVAL;
      return res_REMOVE4.status;
    }

  /* NFS4_OP_REMOVE can delete files as well as directory, it replaces NFS3_RMDIR and NFS3_REMOVE
   * because of this, we have to know if object is a directory or not */
  if((cache_status =
      cache_inode_error_convert(FSAL_buffdesc2name
                                ((fsal_buffdesc_t *) & arg_REMOVE4.target,
                                 &name))) != CACHE_INODE_SUCCESS)
    {
      res_REMOVE4.status = nfs4_Errno(cache_status);
      return res_REMOVE4.status;
    }

  /* Test RM7: remiving '.' should return NFS4ERR_BADNAME */
  if(!FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_REMOVE4.status = NFS4ERR_BADNAME;
      return res_REMOVE4.status;
    }

  if((cache_status = cache_inode_remove(parent_entry,
                                        &name,
                                        &attr_parent,
                                        data->req_ctx,
                                        &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_REMOVE4.status = nfs4_Errno(cache_status);
      return res_REMOVE4.status;
    }

  res_REMOVE4.REMOVE4res_u.resok4.cinfo.after
       = cache_inode_get_changeid4(parent_entry);

  /* Operation was not atomic .... */
  res_REMOVE4.REMOVE4res_u.resok4.cinfo.atomic = FALSE;

  /* If you reach this point, everything was ok */

  res_REMOVE4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_remove */

/**
 * @brief Free memory allocated for REMOVE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_REMOVE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_remove_Free(REMOVE4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_remove_Free */
