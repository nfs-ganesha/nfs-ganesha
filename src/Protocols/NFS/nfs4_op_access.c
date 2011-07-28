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
 * \file    nfs4_op_access.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_access.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"

/**
 * nfs4_op_access: NFS4_OP_ACCESS, checks for file's accessibility. 
 * 
 * NFS4_OP_ACCESS, checks for file's accessibility. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */
#define arg_ACCESS4 op->nfs_argop4_u.opaccess
#define res_ACCESS4 resp->nfs_resop4_u.opaccess

int nfs4_op_access(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t attr;
  fsal_accessflags_t  access_mask = 0;
  cache_inode_status_t cache_status;

  uint32_t max_access =
      (ACCESS4_READ | ACCESS4_LOOKUP | ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE |
       ACCESS4_EXECUTE);
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_access";

  /* initialize output */
  res_ACCESS4.ACCESS4res_u.resok4.supported = 0;
  res_ACCESS4.ACCESS4res_u.resok4.access = 0;

  resp->resop = NFS4_OP_ACCESS;
  res_ACCESS4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_NOFILEHANDLE;
      return res_ACCESS4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_BADHANDLE;
      return res_ACCESS4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_FHEXPIRED;
      return res_ACCESS4.status;
    }

  /* If Filehandle points to a pseudo fs entry, manage it via pseudofs specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_access_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_access_xattr(op, data, resp);

  /* Check for input parameter's sanity */
  if(arg_ACCESS4.access > max_access)
    {
      res_ACCESS4.status = NFS4ERR_INVAL;
      return res_ACCESS4.status;
    }

  /* Get the attributes for the object */
  cache_inode_get_attributes(data->current_entry, &attr);

  /* determine the rights to be tested in FSAL */

  if(arg_ACCESS4.access & ACCESS4_READ)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_READ;
      access_mask |= nfs_get_access_mask(ACCESS4_READ, &attr);
    }

  if((arg_ACCESS4.access & ACCESS4_LOOKUP) && (attr.type == FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_LOOKUP;
      access_mask |= nfs_get_access_mask(ACCESS4_LOOKUP, &attr);
    }

  if(arg_ACCESS4.access & ACCESS4_MODIFY)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_MODIFY;
      access_mask |= nfs_get_access_mask(ACCESS4_MODIFY, &attr);
    }

  if(arg_ACCESS4.access & ACCESS4_EXTEND)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXTEND;
      access_mask |= nfs_get_access_mask(ACCESS4_EXTEND, &attr);
    }

  if((arg_ACCESS4.access & ACCESS4_DELETE) && (attr.type == FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_DELETE;
      access_mask |= nfs_get_access_mask(ACCESS4_DELETE, &attr);
    }

  if((arg_ACCESS4.access & ACCESS4_EXECUTE) && (attr.type != FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXECUTE;
      access_mask |= nfs_get_access_mask(ACCESS4_EXECUTE, &attr);
    }

  nfs4_access_debug("requested access", arg_ACCESS4.access, FSAL_ACE4_MASK(access_mask));

  /* Perform the 'access' call */
  if(cache_inode_access(data->current_entry,
                        access_mask,
                        data->ht, data->pclient,
                        data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        {
      res_ACCESS4.ACCESS4res_u.resok4.access = res_ACCESS4.ACCESS4res_u.resok4.supported;
      nfs4_access_debug("granted access", arg_ACCESS4.access, 0);
    }

  if(cache_status == CACHE_INODE_FSAL_EACCESS)
        {
      /*
       * We have to determine which access bits are good one by one
       */
      res_ACCESS4.ACCESS4res_u.resok4.access = 0;

      access_mask = nfs_get_access_mask(ACCESS4_READ, &attr);
      if(cache_inode_access(data->current_entry,
                            access_mask,
                            data->ht, data->pclient,
                            data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_READ;

      if(attr.type == FSAL_TYPE_DIR)
        {
          access_mask = nfs_get_access_mask(ACCESS4_LOOKUP, &attr);
          if(cache_inode_access(data->current_entry,
                                access_mask,
                                data->ht, data->pclient,
                                data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
            res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_LOOKUP;
    }

      access_mask = nfs_get_access_mask(ACCESS4_MODIFY, &attr);
      if(cache_inode_access(data->current_entry,
                            access_mask,
                            data->ht, data->pclient,
                            data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_MODIFY;

      access_mask = nfs_get_access_mask(ACCESS4_EXTEND, &attr);
      if(cache_inode_access(data->current_entry,
                            access_mask,
                            data->ht, data->pclient,
                            data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
        res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_EXTEND;

      if(attr.type == FSAL_TYPE_DIR)
        {
          access_mask = nfs_get_access_mask(ACCESS4_DELETE, &attr);
          if(cache_inode_access(data->current_entry,
                                access_mask,
                                data->ht, data->pclient,
                                data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
            res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_DELETE;
        }

      if(attr.type != FSAL_TYPE_DIR)
        {
          access_mask = nfs_get_access_mask(ACCESS4_EXECUTE, &attr);
          if(cache_inode_access(data->current_entry,
                                access_mask,
                                data->ht, data->pclient,
                                data->pcontext, &cache_status) == CACHE_INODE_SUCCESS)
            res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_EXECUTE;
        }

      nfs4_access_debug("reduced access", res_ACCESS4.ACCESS4res_u.resok4.access, 0);
    }

  res_ACCESS4.status = NFS4_OK;

  return res_ACCESS4.status;
}                               /* nfs4_op_access */

/**
 * nfs4_op_access_Free: frees what was allocared to handle nfs4_op_access.
 * 
 * Frees what was allocared to handle nfs4_op_access.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_access_Free(ACCESS4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_access_Free */
