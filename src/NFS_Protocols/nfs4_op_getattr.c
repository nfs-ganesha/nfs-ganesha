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
 * --------------------------------------- */
/**
 * \file    nfs4_op_getattr.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:52 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_getattr.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"

#define arg_GETATTR4 op->nfs_argop4_u.opgetattr
#define res_GETATTR4 resp->nfs_resop4_u.opgetattr
#define res_FATTR4  res_GETATTR4.GETATTR4res_u.resok4.obj_attributes

/**
 *
 * nfs4_op_getattr: Gets attributes for an entry in the FSAL.
 * 
 * Gets attributes for an entry in the FSAL.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */
int nfs4_op_getattr(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t attr;
  cache_inode_status_t cache_status;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getattr";

  /* This is a NFS4_OP_GETTAR */
  resp->resop = NFS4_OP_GETATTR;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_GETATTR4.status = NFS4ERR_NOFILEHANDLE;
      return NFS4ERR_NOFILEHANDLE;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_GETATTR4.status = NFS4ERR_BADHANDLE;
      return NFS4ERR_BADHANDLE;
    }

  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_GETATTR4.status = NFS4ERR_FHEXPIRED;
      return NFS4ERR_FHEXPIRED;
    }

  /* Pseudo Fs management */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_getattr_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_getattr_xattr(op, data, resp);

  if(isFullDebug(COMPONENT_NFS_V4))
    {
      char str[LEN_FH_STR];
      sprint_fhandle4(str, &data->currentFH);
      LogFullDebug(COMPONENT_NFS_V4, "NFS4_OP_GETATTR: Current FH %s", str);
    }

  /* Sanity check: if no attributes are wanted, nothing is to be done.
   * In this case NFS4_OK is to be returned */
  if(arg_GETATTR4.attr_request.bitmap4_len == 0)
    {
      res_GETATTR4.status = NFS4_OK;
      return res_GETATTR4.status;
    }

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access_Bitmap(&arg_GETATTR4.attr_request, FATTR4_ATTR_READ))
    {
      res_GETATTR4.status = NFS4ERR_INVAL;
      return res_GETATTR4.status;
    }

  if( !nfs4_bitmap4_Remove_Unsupported( &arg_GETATTR4.attr_request ) )
    {
      res_GETATTR4.status = NFS4ERR_SERVERFAULT ;
      return res_GETATTR4.status;
    }
   

  /*
   * Get attributes.
   */
  if(cache_inode_getattr(data->current_entry,
                         &attr,
                         data->ht,
                         data->pclient,
                         data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      if(nfs4_FSALattr_To_Fattr(data->pexport,
                                &attr,
                                &(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                                data,
                                &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
        res_GETATTR4.status = NFS4ERR_SERVERFAULT;
      else
        res_GETATTR4.status = NFS4_OK;

      return res_GETATTR4.status;
    }
  res_GETATTR4.status = nfs4_Errno(cache_status);

  return res_GETATTR4.status;
}                               /* nfs4_op_getattr */

/**
 * nfs4_op_getattr_Free: frees what was allocared to handle nfs4_op_getattr.
 * 
 * Frees what was allocared to handle nfs4_op_getattr.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_getattr_Free(GETATTR4res * resp)
{
  if(resp->status == NFS4_OK)
    {
      if(resp->GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val != NULL)
        Mem_Free((char *)resp->GETATTR4res_u.resok4.obj_attributes.attrmask.bitmap4_val);

      if(resp->GETATTR4res_u.resok4.obj_attributes.attr_vals.attrlist4_val != NULL)
        Mem_Free((char *)resp->GETATTR4res_u.resok4.obj_attributes.attr_vals.
                 attrlist4_val);
    }
  return;
}                               /* nfs4_op_getattr_Free */
