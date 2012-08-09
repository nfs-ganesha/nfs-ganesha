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
 * \file    nfs4_op_putrootfh.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:52:14 $
 * \version $Revision: 1.13 $
 * \brief   Routines used for managing the NFS4_OP_PUTROOTFH operation. 
 *
 * nfs4_op_putrootfh.c : Routines used for managing the NFS4_OP_PUTROOTFH operation.
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
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * CreateROOTFH4: create the pseudo fs root filehandle .
 *
 * Creates the pseudo fs root filehandle .
 *
 * @param fh   [OUT]   the file handle to be built.
 * @param data [INOUT] request compound data
 *
 * @return NFS4_OK is successful, NFS4ERR_BADHANDLE otherwise (for an error).
 *
 * @see nfs4_op_putrootfh
 *
 */

static int CreateROOTFH4(nfs_fh4 * fh, compound_data_t * data)
{
  pseudofs_entry_t psfsentry;
  int              status = 0;

  psfsentry = *(data->pseudofs->reverse_tab[0]);

  /* If rootFH already set, return success */
  if(data->rootFH.nfs_fh4_len != 0)
    return NFS4_OK;

  if((status = nfs4_AllocateFH(&(data->rootFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->rootFH), &psfsentry))
    return NFS4ERR_BADHANDLE;

  LogHandleNFS4("CREATE ROOT FH: ", &data->rootFH);

  return NFS4_OK;
}                               /* CreateROOTFH4 */

/**
 *
 * nfs4_op_putroothfh: The NFS4_OP_PUTROOTFH operation.
 *
 * This functions handles the NFS4_OP_PUTROOTFH operation in NFSv4. This function can be called only from nfs4_Compound. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see CreateROOTFH4
 *
 */

#define arg_PUTROOTFH4 op->nfs_argop4_u.opputrootfh
#define res_PUTROOTFH4 resp->nfs_resop4_u.opputrootfh

int nfs4_op_putrootfh(struct nfs_argop4 *op,
                      compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_putrootfh";

  /* This NFS4 Operation has no argument, it just get then ROOTFH (replace MOUNTPROC3_MNT) */

  /* First of all, set the reply to zero to make sure it contains no parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_PUTROOTFH;

  res_PUTROOTFH4.status = CreateROOTFH4(&(data->rootFH), data);
  if(res_PUTROOTFH4.status != NFS4_OK)
    return res_PUTROOTFH4.status;

  if (data->current_entry) {
      cache_inode_put(data->current_entry);
  }

  /* Fill in compound data */
  set_compound_data_for_pseudo(data);

  /* I copy the root FH to the currentFH */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTROOTFH4.status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTROOTFH4.status != NFS4_OK)
        return res_PUTROOTFH4.status;
    }

  /* Copy the data from root FH to current FH */
  memcpy(data->currentFH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
         data->rootFH.nfs_fh4_len);
  data->currentFH.nfs_fh4_len = data->rootFH.nfs_fh4_len;

  /* For initial mounted_on_FH, I'll use the rootFH, this will change at junction traversal */
  if(data->mounted_on_FH.nfs_fh4_len == 0)
    {
      res_PUTROOTFH4.status = nfs4_AllocateFH(&(data->mounted_on_FH));
      if(res_PUTROOTFH4.status != NFS4_OK)
        return res_PUTROOTFH4.status;
    }

  memcpy(data->mounted_on_FH.nfs_fh4_val, data->rootFH.nfs_fh4_val,
         data->rootFH.nfs_fh4_len);
  data->mounted_on_FH.nfs_fh4_len = data->rootFH.nfs_fh4_len;

  LogHandleNFS4("NFS4 PUTROOTFH ROOT    FH: ", &data->rootFH);
  LogHandleNFS4("NFS4 PUTROOTFH CURRENT FH: ", &data->currentFH);

  LogFullDebug(COMPONENT_NFS_V4,
                    "NFS4 PUTROOTFH: Ending on status %d",
                    res_PUTROOTFH4.status);

  return res_PUTROOTFH4.status;
}                               /* nfs4_op_putrootfh */

/**
 * nfs4_op_putrootfh_Free: frees what was allocared to handle nfs4_op_putrootfh.
 * 
 * Frees what was allocared to handle nfs4_op_putrootfh.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_putrootfh_Free(PUTROOTFH4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_putrootfh_Free */
