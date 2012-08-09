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
 * \file    nfs4_op_putpubfh.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4_OP_PUTPUBFH operation.
 *
 * nfs4_op_putpubfh.c : Routines used for managing the NFS4_OP_PUTPUBFH operation.
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
 * CreatePUBFH4: create the pseudo fs public filehandle .
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

static int CreatePUBFH4(nfs_fh4 * fh, compound_data_t * data)
{
  pseudofs_entry_t psfsentry;
  int              status = 0;

  /* For the moment, I choose to have rootFH = publicFH */
  psfsentry = *(data->pseudofs->reverse_tab[0]);

  /* If publicFH already set, return success */
  if(data->publicFH.nfs_fh4_len != 0)
    return NFS4_OK;

  if((status = nfs4_AllocateFH(&(data->publicFH))) != NFS4_OK)
    return status;

  if(!nfs4_PseudoToFhandle(&(data->publicFH), &psfsentry))
    return NFS4ERR_BADHANDLE;

  LogHandleNFS4("CREATE PUB FH: ", &data->publicFH);

  return NFS4_OK;
}                               /* CreatePUBFH4 */

/**
 *
 * nfs4_op_putpubfh: The NFS4_OP_PUTFH operation
 *
 * Sets the publicFH for the current compound requests as the current FH.
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

#define arg_PUTPUBFH4 op->nfs_argop4_u.opputpubfh
#define res_PUTPUBFH4 resp->nfs_resop4_u.opputpubfh

int nfs4_op_putpubfh(struct nfs_argop4 *op,
                     compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_putpubfh";

  /* First of all, set the reply to zero to make sure it contains no parasite information */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_PUTPUBFH;

  /* For now, GANESHA makes no difference betzeen PUBLICFH and ROOTFH */
  res_PUTPUBFH4.status = CreatePUBFH4(&(data->publicFH), data);
  if(res_PUTPUBFH4.status != NFS4_OK)
    return res_PUTPUBFH4.status;

  if (data->current_entry) {
      cache_inode_put(data->current_entry);
  }

  /* Fill in compound data */
  set_compound_data_for_pseudo(data);

  /* I copy the public FH to the currentFH */
  if(data->currentFH.nfs_fh4_len == 0)
    {
      res_PUTPUBFH4.status = nfs4_AllocateFH(&(data->currentFH));
      if(res_PUTPUBFH4.status != NFS4_OK)
        return res_PUTPUBFH4.status;
    }

  /* Copy the data from public FH to current FH */
  memcpy(data->currentFH.nfs_fh4_val, data->publicFH.nfs_fh4_val,
         data->publicFH.nfs_fh4_len);

  /* For initial mounted_on_FH, I'll use the publicFH, this will change at junction traversal */
  if(data->mounted_on_FH.nfs_fh4_len == 0)
    {
      res_PUTPUBFH4.status = nfs4_AllocateFH(&(data->mounted_on_FH));
      if(res_PUTPUBFH4.status != NFS4_OK)
        return res_PUTPUBFH4.status;
    }

  memcpy(data->mounted_on_FH.nfs_fh4_val, data->publicFH.nfs_fh4_val,
         data->publicFH.nfs_fh4_len);
  data->mounted_on_FH.nfs_fh4_len = data->publicFH.nfs_fh4_len;

  LogHandleNFS4("NFS4 PUTPUBFH PUBLIC  FH: ", &data->publicFH);
  LogHandleNFS4("NFS4 PUTPUBFH CURRENT FH: ", &data->currentFH);

  LogFullDebug(COMPONENT_NFS_V4,
                    "NFS4 PUTPUBFH: Ending on status %d",
                    res_PUTPUBFH4.status);

  return res_PUTPUBFH4.status;
}                               /* nfs4_op_putpubfh */

/**
 * nfs4_op_putpubfh_Free: frees what was allocared to handle nfs4_op_putpubfh.
 * 
 * Frees what was allocared to handle nfs4_op_putpubfh.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_putpubfh_Free(PUTPUBFH4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_putpubfh_Free */
