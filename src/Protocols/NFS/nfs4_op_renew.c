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
 * \file    nfs4_op_renew.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_renew.c : Routines used for managing the NFS4 COMPOUND functions.
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

/**
 * 
 * nfs4_op_renew: The NFS4_OP_RENEW operation. 
 *
 * This function implements the NFS4_OP_RENEW operation.
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

#define arg_RENEW4 op->nfs_argop4_u.oprenew
#define res_RENEW4 resp->nfs_resop4_u.oprenew

int nfs4_op_renew(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_renew";
  nfs_client_id_t nfs_clientid;

  /* Lock are not supported */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_RENEW;

  /* Tell the admin what I am doing... */
  LogFullDebug(COMPONENT_NFS_V4, "RENEW Client id = %"PRIx64, arg_RENEW4.clientid);

  /* Is this an existing client id ? */
  if(nfs_client_id_get(arg_RENEW4.clientid, &nfs_clientid) == CLIENT_ID_SUCCESS)
    {
      nfs_clientid.last_renew = time(NULL);
      res_RENEW4.status = NFS4_OK;      /* Regular exit */
    }
  else
    {
      /* Unknown client id */
      res_RENEW4.status = NFS4ERR_STALE_CLIENTID;
    }

  /* If you reach this point, then an error occured */
  return res_RENEW4.status;
}                               /* nfs4_op_renew */

/**
 * nfs4_op_renew_Free: frees what was allocared to handle nfs4_op_renew.
 * 
 * Frees what was allocared to handle nfs4_op_renew.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_renew_Free(RENEW4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_renew_Free */
