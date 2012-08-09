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
#include <assert.h>
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
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_putfh";

  int                rc;
  fsal_attrib_list_t attr;


  resp->resop = NFS4_OP_PUTFH;
  res_PUTFH4.status = NFS4_OK;

  /* If no currentFH were set, allocate one */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTFH4.status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTFH4.status != NFS4_OK)
        return res_PUTFH4.status;
    }

  /* The same is to be done with mounted_on_FH */
  if(data->mounted_on_FH.nfs_fh4_len == 0)
    {
      res_PUTFH4.status = nfs4_AllocateFH(&(data->mounted_on_FH));
      if(res_PUTFH4.status != NFS4_OK)
        return res_PUTFH4.status;
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

  /* If the filehandle is not pseudo fs file handle, get the entry
   * related to it, otherwise use fake values */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
        if (data->current_entry) {
            cache_inode_put(data->current_entry);
        }

        /* Fill in compound data */
        set_compound_data_for_pseudo(data);
    }
  else
    {
      /* Set up the export (and nfs4_MakeCred) for the new currentFH */
      res_PUTFH4.status = nfs4_SetCompoundExport(data);
      if(res_PUTFH4.status != NFS4_OK)
        return res_PUTFH4.status;

#ifdef _PNFS_DS
      /* As usual, protect existing refcounts */
      if (data->current_entry) {
          cache_inode_put(data->current_entry);
          data->current_entry = NULL;
          data->current_filetype = UNASSIGNED;
      }
      /* The export and fsalid should be updated, but DS handles
         don't support metdata operations.  Thus, we can't call into
         Cache_inode to populate the metadata cache. */
      if(nfs4_Is_Fh_DSHandle(&data->currentFH))
        {
          data->current_entry = NULL;
          data->current_filetype = REGULAR_FILE;
        }
      else
#endif /* _PNFS_DS */
        {
          if (data->current_entry)
            cache_inode_put(data->current_entry);
          /* Build the pentry.  Refcount +1. */
          if((data->current_entry = nfs_FhandleToCache(NFS_V4,
                                                       NULL,
                                                       NULL,
                                                       &(data->currentFH),
                                                       NULL,
                                                       NULL,
                                                       &(res_PUTFH4.status),
                                                       &attr,
                                                       data->pcontext,
                                                       &rc)) == NULL)
            {
              return res_PUTFH4.status;
            }
          /* Extract the filetype */
          data->current_filetype = cache_inode_fsal_type_convert(attr.type);
        }
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
