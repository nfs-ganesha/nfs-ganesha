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
 * \file    nfs41_op_getdevicelist.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_GETDEVICELIST operation.
 *
 * nfs41_op_getdevicelist.c :  Routines used for managing the GETDEVICELIST operation.
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
 * nfs41_op_getdevicelist:  The NFS4_OP_GETDEVICELIST operation.
 *
 * Gets the list of pNFS devices
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see nfs4_Compound
 *
 */

int nfs41_op_getdevicelist(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getdevicelist";
#ifdef _USE_PNFS
  nfsstat4 rc = 0 ;
#endif

#define arg_GETDEVICELIST4  op->nfs_argop4_u.opgetdevicelist
#define res_GETDEVICELIST4  resp->nfs_resop4_u.opgetdevicelist

#ifndef _USE_PNFS
  resp->resop = NFS4_OP_GETDEVICELIST;
  res_GETDEVICELIST4.gdlr_status = NFS4ERR_NOTSUPP;
  return res_GETDEVICELIST4.gdlr_status;
#else

  resp->resop = NFS4_OP_GETDEVICELIST;
  res_GETDEVICELIST4.gdlr_status = NFS4_OK;

  if( ( rc = pnfs_getdevicelist( &arg_GETDEVICELIST4, data, &res_GETDEVICELIST4 ) ) != NFS4_OK )
  {
     res_GETDEVICELIST4.gdlr_status = rc ; 
     return res_GETDEVICELIST4.gdlr_status;
  }


  return res_GETDEVICELIST4.gdlr_status;
#endif /* _USE_PNFS */
}                               /* nfs41_op_exchange_id */

/**
 * nfs4_op_getdevicelist_Free: frees what was allocared to handle nfs4_op_getdevicelist.
 * 
 * Frees what was allocared to handle nfs4_op_getdevicelist.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_getdevicelist_Free(GETDEVICELIST4res * resp)
{
  return;
}                               /* nfs41_op_exchange_id_Free */
