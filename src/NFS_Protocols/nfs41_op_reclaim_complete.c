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
 * \file    nfs41_op_reclaim_complete.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_reclaim_complete.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 * 
 * nfs41_op_reclaim_complete: The NFS4_OP_RECLAIM_COMPLETE4 operation. 
 *
 * This function implements the NFS4_OP_RECLAIM_COMPLETE4 operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs41_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_RECLAIM_COMPLETE4 op->nfs_argop4_u.opreclaim_complete
#define res_RECLAIM_COMPLETE4 resp->nfs_resop4_u.opreclaim_complete

int nfs41_op_reclaim_complete(struct nfs_argop4 *op, compound_data_t * data,
                              struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs41_op_reclaim_complete";

  resp->resop = NFS4_OP_RECLAIM_COMPLETE;

  res_RECLAIM_COMPLETE4.rcr_status = NFS4_OK;
  return res_RECLAIM_COMPLETE4.rcr_status;
}                               /* nfs41_op_reclaim_complete */

/**
 * nfs41_op_reclaim_complete_Free: frees what was allocared to handle nfs41_op_layoutreturn.
 * 
 * Frees what was allocared to handle nfs41_op_reclaim_complete
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_reclaim_complete_Free(RECLAIM_COMPLETE4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}                               /* nfs41_op_reclaim_complete_Free */
