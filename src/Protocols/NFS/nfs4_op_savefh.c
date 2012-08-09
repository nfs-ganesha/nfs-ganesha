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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
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
 * nfs4_op_savefh.c : Routines used for managing the NFS4_OP_SAVEFH operation.
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
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 *
 * \brief the NFS4_OP_SAVEFH operation
 *
 * This functions handles the NFS4_OP_SAVEFH operation in NFSv4. This
 * function can be called only from nfs4_Compound.  The operation set
 * the savedFH with the value of the currentFH.
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

#define arg_SAVEFH op->nfs_argop4_u.opsavefh
#define res_SAVEFH resp->nfs_resop4_u.opsavefh

int nfs4_op_savefh(struct nfs_argop4 *op,
                   compound_data_t * data,
                   struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_savefh";

  /* First of all, set the reply to zero to make sure it contains no
     parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_SAVEFH;
  res_SAVEFH.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_SAVEFH.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_SAVEFH.status != NFS4_OK)
    return res_SAVEFH.status;

  /* If the savefh is not allocated, do it now */
  if(data->savedFH.nfs_fh4_len == 0)
    {
      res_SAVEFH.status = nfs4_AllocateFH(&(data->savedFH));
      if(res_SAVEFH.status != NFS4_OK)
        return res_SAVEFH.status;
    }

  /* Copy the data from current FH to saved FH */
  memcpy(data->savedFH.nfs_fh4_val,
         data->currentFH.nfs_fh4_val,
         data->currentFH.nfs_fh4_len);
  data->savedFH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

  /* Save the export information */
  data->saved_pexport      = data->pexport;
  data->saved_export_perms = data->export_perms;

  /* If saved and current entry are equal, skip the following. */

  if (data->saved_entry == data->current_entry) {
      goto out;
  }

  if (data->saved_entry) {
      cache_inode_put(data->saved_entry);
      data->saved_entry = NULL;
  }

  data->saved_entry = data->current_entry;
  data->saved_filetype = data->current_filetype;

  /* Take another reference.  As of now the filehandle is both saved
     and current and both must be counted.  Guard this, in case we
     have a pseudofs handle. */

  if (data->saved_entry) {
       if (cache_inode_lru_ref(data->saved_entry,
                               LRU_FLAG_NONE) != CACHE_INODE_SUCCESS) {
            data->saved_entry = NULL;
            resp->nfs_resop4_u.opsavefh.status = NFS4ERR_STALE;
            return resp->nfs_resop4_u.opsavefh.status;
       }
  }

 out:

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
