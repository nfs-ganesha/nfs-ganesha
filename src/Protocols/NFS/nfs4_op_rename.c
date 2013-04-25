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
 * \file    nfs4_op_rename.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_rename.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

/**
 * nfs4_op_rename: The NFS4_OP_RENAME operation.
 * 
 * This functions handles the NFS4_OP_RENAME operation in NFSv4. This function can be called only from nfs4_Compound
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_RENAME4 op->nfs_argop4_u.oprename
#define res_RENAME4 resp->nfs_resop4_u.oprename

int nfs4_op_rename(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_rename";

  cache_entry_t        * dst_entry = NULL;
  cache_entry_t        * src_entry = NULL;
  cache_inode_status_t   cache_status;
  fsal_name_t            oldname;
  fsal_name_t            newname;

  resp->resop = NFS4_OP_RENAME;
  res_RENAME4.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_RENAME4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_RENAME4.status != NFS4_OK)
    return res_RENAME4.status;

  LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                     "Saved FH %s",
                     LEN_FH_STR,
                     data->savedFH.nfs_fh4_val,
                     data->savedFH.nfs_fh4_len);

  /* Do basic checks on saved filehandle */
  res_RENAME4.status = nfs4_sanity_check_SavedFH(data, 0LL);
  if(res_RENAME4.status != NFS4_OK)
    return res_RENAME4.status;

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->savedFH)))
    {
      res_RENAME4.status = NFS4ERR_ROFS;
      return res_RENAME4.status;
    }

  if (nfs_in_grace())
    {
      res_RENAME4.status = NFS4ERR_GRACE;
      return res_RENAME4.status;
    }

  /* get the names from the RPC input */
  cache_status = utf8_to_name(&arg_RENAME4.oldname, &oldname);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      return res_RENAME4.status;
    }

  cache_status = utf8_to_name(&arg_RENAME4.newname, &newname);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      return res_RENAME4.status;
    }

  /*
   * This operation renames 
   *             - the object in directory pointed by savedFH, named arg_RENAME4.oldname
   *       into
   *             - an object in directory pointed by currentFH, named arg_RENAME4.newname
   *
   * Because of this, we will use 2 entry and we have verified both currentFH and savedFH */

  /* No Cross Device */
  if(((file_handle_v4_t *) (data->currentFH.nfs_fh4_val))->exportid !=
     ((file_handle_v4_t *) (data->savedFH.nfs_fh4_val))->exportid)
    {
      res_RENAME4.status = NFS4ERR_XDEV;
      return res_RENAME4.status;
    }

  /* CurrentFH is destination directory, SavedFH is source directory */
  dst_entry = data->current_entry;
  src_entry = data->saved_entry;

  res_RENAME4.RENAME4res_u.resok4.source_cinfo.before
       = cache_inode_get_changeid4(src_entry, data->pcontext);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.before
       = cache_inode_get_changeid4(dst_entry, data->pcontext);

  /* New entry does not already exist, call cache_entry_rename */
  if(cache_inode_rename(src_entry,
                        &oldname,
                        dst_entry,
                        &newname,
                        data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      goto release;
    }

  /* If you reach this point, then everything was alright */
  /* For the change_info4, get the 'change' attributes for both directories */

  res_RENAME4.RENAME4res_u.resok4.source_cinfo.after =
       cache_inode_get_changeid4(src_entry, data->pcontext);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.after =
       cache_inode_get_changeid4(dst_entry, data->pcontext);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.atomic = FALSE;
  res_RENAME4.RENAME4res_u.resok4.source_cinfo.atomic = FALSE;
  res_RENAME4.status = nfs4_Errno(cache_status);

release:

  return (res_RENAME4.status);
}                               /* nfs4_op_rename */

/**
 * nfs4_op_rename_Free: frees what was allocared to handle nfs4_op_rename.
 * 
 * Frees what was allocared to handle nfs4_op_rename.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_rename_Free(RENAME4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_rename_Free */
