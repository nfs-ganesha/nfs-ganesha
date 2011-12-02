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
 * \file    nfs4_op_putfh.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4_OP_PUTFH operation.
 *
 * nfs4_op_putfh.c : Routines used for managing the NFS4_OP_PUTFH operation.
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

#define arg_PUTFH4 op->nfs_argop4_u.opputfh
#define res_PUTFH4 resp->nfs_resop4_u.opputfh

/**
 *
 * nfs4_op_putfh: The NFS4_OP_PUTFH operation
 *
 * Sets the current FH with the value given in argument. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */

int nfs4_op_putfh(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  int rc;
  int error;
  fsal_attrib_list_t attr;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_putfh";

  resp->resop = NFS4_OP_PUTFH;
  res_PUTFH4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(arg_PUTFH4.object)))
    {
      res_PUTFH4.status = NFS4ERR_NOFILEHANDLE;
      return res_PUTFH4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(arg_PUTFH4.object)))
    {
      res_PUTFH4.status = NFS4ERR_BADHANDLE;
      return res_PUTFH4.status;
    }

  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(arg_PUTFH4.object)))
    {
      res_PUTFH4.status = NFS4ERR_FHEXPIRED;
      return res_PUTFH4.status;
    }

  /* If no currentFH were set, allocate one */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      if((error = nfs4_AllocateFH(&(data->currentFH))) != NFS4_OK)
        {
          res_PUTFH4.status = error;
          return res_PUTFH4.status;
        }
    }

  /* The same is to be done with mounted_on_FH */
  if(data->mounted_on_FH.nfs_fh4_len == 0)
    {
      if((error = nfs4_AllocateFH(&(data->mounted_on_FH))) != NFS4_OK)
        {
          res_PUTFH4.status = error;
          return res_PUTFH4.status;
        }
    }

  /* Copy the filehandle from the reply structure */
  data->currentFH.nfs_fh4_len = arg_PUTFH4.object.nfs_fh4_len;
  data->mounted_on_FH.nfs_fh4_len = arg_PUTFH4.object.nfs_fh4_len;

  /* Put the data in place */
  memcpy(data->currentFH.nfs_fh4_val, arg_PUTFH4.object.nfs_fh4_val,
         arg_PUTFH4.object.nfs_fh4_len);
  memcpy(data->mounted_on_FH.nfs_fh4_val, arg_PUTFH4.object.nfs_fh4_val,
         arg_PUTFH4.object.nfs_fh4_len);

  LogHandleNFS4("NFS4_OP_PUTFH CURRENT FH: ", &arg_PUTFH4.object);

  /* If the filehandle is not pseudo hs file handle, get the entry related to it, otherwise use fake values */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      data->current_entry = NULL;
      data->current_filetype = DIRECTORY;
      data->pexport = NULL;     /* No exportlist is related to pseudo fs */
    }
  else
    {
      /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
      if(data->pexport == NULL)
        {
          if((error = nfs4_SetCompoundExport(data)) != NFS4_OK)
            {
              res_PUTFH4.status = error;
              return res_PUTFH4.status;
            }
#ifdef _USE_SHARED_FSAL
	FSAL_SetId( data->pexport->fsalid ) ;
#endif 
        }

      /* Build the pentry */
      if((data->current_entry = nfs_FhandleToCache(NFS_V4,
                                                   NULL,
                                                   NULL,
                                                   &(data->currentFH),
                                                   NULL,
                                                   NULL,
                                                   &(res_PUTFH4.status),
                                                   &attr,
                                                   data->pcontext,
                                                   data->pclient, data->ht, &rc)) == NULL)
        {
          res_PUTFH4.status = NFS4ERR_STALE;
          return res_PUTFH4.status;
        }

      /* Extract the filetype */
      data->current_filetype = cache_inode_fsal_type_convert(attr.type);

    }

  return NFS4_OK;
}                               /* nfs4_op_putfh */

/**
 * nfs4_op_create_Free: frees what was allocared to handle nfs4_op_create.
 * 
 * Frees what was allocared to handle nfs4_op_create.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_putfh_Free(PUTFH4res * resp)
{
  /* Nothing to be freed */
  return;
}                               /* nfs4_op_create_Free */
