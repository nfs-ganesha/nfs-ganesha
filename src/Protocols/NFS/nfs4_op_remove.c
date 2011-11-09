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
 * \file    nfs4_op_remove.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.10 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_remove.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 * nfs4_op_rename: The NFS4_OP_REMOVE operation.
 * 
 * This functions handles the NFS4_OP_REMOVE operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_REMOVE4 op->nfs_argop4_u.opremove
#define res_REMOVE4 resp->nfs_resop4_u.opremove

int nfs4_op_remove(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t *parent_entry = NULL;

  fsal_attrib_list_t attr_parent;
  fsal_name_t name;

  cache_inode_status_t cache_status;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_remove";

  resp->resop = NFS4_OP_REMOVE;
  res_REMOVE4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_REMOVE4.status = NFS4ERR_NOFILEHANDLE;
      return res_REMOVE4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_REMOVE4.status = NFS4ERR_BADHANDLE;
      return res_REMOVE4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_REMOVE4.status = NFS4ERR_FHEXPIRED;
      return res_REMOVE4.status;
    }

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_REMOVE4.status = NFS4ERR_ROFS;
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
      (changeid4) parent_entry->internal_md.mod_time;

  /* The operation delete object named arg_REMOVE4.target in directory pointed bt cuurentFH */
  /* Make sur the currentFH is pointed a directory */
  if(data->current_filetype != DIRECTORY)
    {
      res_REMOVE4.status = NFS4ERR_NOTDIR;
      return res_REMOVE4.status;
    }

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
                                        data->ht,
                                        data->pclient,
                                        data->pcontext,
                                        &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_REMOVE4.status = nfs4_Errno(cache_status);
      return res_REMOVE4.status;
    }

  /* We have to keep track of the 'change' file attribute for reply structure */
  memset(&(res_REMOVE4.REMOVE4res_u.resok4.cinfo.before), 0, sizeof(changeid4));
  res_REMOVE4.REMOVE4res_u.resok4.cinfo.after =
      (changeid4) parent_entry->internal_md.mod_time;

  /* Operation was not atomic .... */
  res_REMOVE4.REMOVE4res_u.resok4.cinfo.atomic = TRUE;

  /* If you reach this point, everything was ok */

  res_REMOVE4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_remove */

/**
 * nfs4_op_remove_Free: frees what was allocared to handle nfs4_op_remove.
 * 
 * Frees what was allocared to handle nfs4_op_remove.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_remove_Free(REMOVE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_remove_Free */
