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
 * \file    nfs41_op_getdeviceinfo.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_GETDEVICEINFO operation.
 *
 * nfs41_op_getdeviceinfo.c :  Routines used for managing the GETDEVICEINFO operation.
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
 * nfs41_op_getdeviceinfo:  The NFS4_OP_GETDEVICEINFO operation.
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

#define arg_GETDEVICEINFO4  op->nfs_argop4_u.opgetdeviceinfo
#define res_GETDEVICEINFO4  resp->nfs_resop4_u.opgetdeviceinfo

int nfs41_op_getdeviceinfo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_getdeviceinfo";
#ifdef _USE_PNFS
  nfsstat4 rc = 0 ;
#endif

  resp->resop = NFS4_OP_GETDEVICEINFO;

#ifndef _USE_PNFS
  res_GETDEVICEINFO4.gdir_status = NFS4ERR_NOTSUPP;
  return res_GETDEVICEINFO4.gdir_status;
#else

  char *buffin = NULL;
  unsigned int lenbuffin = 0;

  char *buff = NULL;
  unsigned int lenbuff = 0;

  if((buff = Mem_Alloc(1024)) == NULL)
    {
      res_GETDEVICEINFO4.gdir_status = NFS4ERR_SERVERFAULT;
      return res_GETDEVICEINFO4.gdir_status;
    }

  /** @todo handle multiple DS here when this will be implemented (switch on deviceid arg) */
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_notification.bitmap4_len = 0;
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_notification.bitmap4_val = NULL;

  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_layout_type =
      LAYOUT4_NFSV4_1_FILES;

  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.
      da_addr_body_len = 1024; /** @todo For wanting of something better */
  res_GETDEVICEINFO4.GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.
      da_addr_body_val = buff;

  if( ( rc = pnfs_getdeviceinfo( &arg_GETDEVICEINFO4, data, &res_GETDEVICEINFO4 ) ) != NFS4_OK )
    {
       res_GETDEVICEINFO4.gdir_status = rc ; 
       return res_GETDEVICEINFO4.gdir_status;
    }


  res_GETDEVICEINFO4.gdir_status = NFS4_OK;

  return res_GETDEVICEINFO4.gdir_status;
#endif                          /* _USE_PNFS */
}                               /* nfs41_op_exchange_id */

/**
 * nfs4_op_getdeviceinfo_Free: frees what was allocared to handle nfs4_op_getdeviceinfo.
 * 
 * Frees what was allocared to handle nfs4_op_getdeviceinfo.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_getdeviceinfo_Free(GETDEVICEINFO4res * resp)
{
  if(resp->gdir_status == NFS4_OK)
    if(resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.
       da_addr_body_val != NULL)
      Mem_Free(resp->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.
               da_addr_body_val);

  return;
}                               /* nfs41_op_exchange_id_Free */
