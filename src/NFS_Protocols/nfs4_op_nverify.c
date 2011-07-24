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
 * \file    nfs4_op_nverify.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:52 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_nverify.c : Routines used for managing the NFS4 COMPOUND functions.
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
 *
 * nfs4_op_nverify: Implemtation of NFS4_OP_NVERIFY
 * 
 * Implemtation of NFS4_OP_NVERIFY. This is usually made for cache validator implementation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

#define arg_NVERIFY4 op->nfs_argop4_u.opnverify
#define res_NVERIFY4 resp->nfs_resop4_u.opnverify

int nfs4_op_nverify(struct nfs_argop4 *op,
                    compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t file_attr;
  cache_inode_status_t cache_status;
  fattr4 file_attr4;
  int rc = 0;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_nverify";

  resp->resop = NFS4_OP_NVERIFY;
  res_NVERIFY4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_NOFILEHANDLE;
      return NFS4ERR_NOFILEHANDLE;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_BADHANDLE;
      return NFS4ERR_BADHANDLE;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4ERR_FHEXPIRED;
      return NFS4ERR_FHEXPIRED;
    }

  /* operation is always permitted on pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_NVERIFY4.status = NFS4_OK;
      return res_NVERIFY4.status;
    }

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access(&arg_NVERIFY4.obj_attributes, FATTR4_ATTR_READ))
    {
      res_NVERIFY4.status = NFS4ERR_INVAL;
      return res_NVERIFY4.status;
    }

  /* Ask only for supported attributes */
  if(!nfs4_Fattr_Supported(&arg_NVERIFY4.obj_attributes))
    {
      res_NVERIFY4.status = NFS4ERR_ATTRNOTSUPP;
      return res_NVERIFY4.status;
    }

  /* Get the cache inode attribute */
  if((cache_status = cache_inode_getattr(data->current_entry,
                                         &file_attr,
                                         data->ht,
                                         data->pclient,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_NVERIFY4.status = NFS4ERR_INVAL;
      return res_NVERIFY4.status;
    }

  if(nfs4_FSALattr_To_Fattr(data->pexport,
                            &file_attr,
                            &file_attr4,
                            data,
                            &(data->currentFH),
                            &(arg_NVERIFY4.obj_attributes.attrmask)) != 0)
    {
      res_NVERIFY4.status = NFS4ERR_SERVERFAULT;
      return res_NVERIFY4.status;
    }

  if((rc = nfs4_Fattr_cmp(&(arg_NVERIFY4.obj_attributes), &file_attr4)) == FALSE)
    res_NVERIFY4.status = NFS4_OK;
  else
    {
      if(rc == -1)
        res_NVERIFY4.status = NFS4ERR_INVAL;
      else
        res_NVERIFY4.status = NFS4ERR_SAME;
    }

  return res_NVERIFY4.status;
}                               /* nfs4_op_nverify */

/**
 * nfs4_op_nverify_Free: frees what was allocared to handle nfs4_op_nverify.
 * 
 * Frees what was allocared to handle nfs4_op_nverify.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_nverify_Free(NVERIFY4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_nverify_Free */
