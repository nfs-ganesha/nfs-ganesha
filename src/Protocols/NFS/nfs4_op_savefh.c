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
 * \file    nfs4_op_savefh.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:52 $
 * \version $Revision: 1.10 $
 * \brief   Routines used for managing the NFS4_OP_SAVEFH operation.
 *
 * nfs4_op_getfh.c : Routines used for managing the NFS4_OP_SAVEFH operation.
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
 * nfs4_op_savefh: the NFS4_OP_SAVEFH operation
 *
 * This functions handles the NFS4_OP_SAVEFH operation in NFSv4. This function can be called only from nfs4_Compound.
 * The operation set the savedFH with the value of the currentFH.
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

int nfs4_op_savefh(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  int error;

  /* First of all, set the reply to zero to make sure it contains no parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));

  resp->resop = NFS4_OP_SAVEFH;
  resp->nfs_resop4_u.opsavefh.status = NFS4_OK;

  /* If there is no currentFH, teh  return an error */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      /* There is no current FH, return NFS4ERR_NOFILEHANDLE */
      resp->nfs_resop4_u.opsavefh.status = NFS4ERR_NOFILEHANDLE;
      return NFS4ERR_NOFILEHANDLE;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      resp->nfs_resop4_u.opgetfh.status = NFS4ERR_BADHANDLE;
      return NFS4ERR_BADHANDLE;
    }

  /* Tests if teh Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      resp->nfs_resop4_u.opgetfh.status = NFS4ERR_FHEXPIRED;
      return NFS4ERR_FHEXPIRED;
    }

  /* If the savefh is not allocated, do it now */
  if(data->savedFH.nfs_fh4_len == 0)
    {
      if((error = nfs4_AllocateFH(&(data->savedFH))) != NFS4_OK)
        {
          resp->nfs_resop4_u.opsavefh.status = error;
          return error;
        }
    }

  /* Copy the data from current FH to saved FH */
  memcpy((char *)(data->savedFH.nfs_fh4_val), (char *)(data->currentFH.nfs_fh4_val),
         data->currentFH.nfs_fh4_len);

  /* Keep the vnodep in mind */
  data->saved_entry = data->current_entry;
  data->saved_filetype = data->current_filetype;

  if(isFullDebug(COMPONENT_NFS_V4))
    {
      char str[LEN_FH_STR];
      sprint_fhandle4(str, &data->savedFH);
      LogFullDebug(COMPONENT_NFS_V4, "SAVE FH: Saved FH %s", str);
    }

  return NFS4_OK;
}                               /* nfs4_op_savefh */

/**
 * nfs4_op_savefh_Free: frees what was allocared to handle nfs4_op_savefh.
 * 
 * Frees what was allocared to handle nfs4_op_savefh.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_savefh_Free(SAVEFH4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_savefh_Free */
