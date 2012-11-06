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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

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
  nfs_client_id_t *pclientid;
  int              rc;

  /* Lock are not supported */
  memset(resp, 0, sizeof(struct nfs_resop4));
  resp->resop = NFS4_OP_RENEW;

  /* Tell the admin what I am doing... */
  LogFullDebug(COMPONENT_CLIENTID, "RENEW Client id = %"PRIx64, arg_RENEW4.clientid);

  /* Is this an existing client id ? */
  rc = nfs_client_id_get_confirmed(arg_RENEW4.clientid, &pclientid);

  if(rc != CLIENT_ID_SUCCESS)
    {
      /* Unknown client id */
      res_RENEW4.status = clientid_error_to_nfsstat(rc);
      return res_RENEW4.status;
    }

  P(pclientid->cid_mutex);

  if(!reserve_lease(pclientid))
    {
      res_RENEW4.status = NFS4ERR_EXPIRED;
    }
  else
    {
      update_lease(pclientid);
      res_RENEW4.status = NFS4_OK;      /* Regular exit */
    }

  V(pclientid->cid_mutex);

  dec_client_id_ref(pclientid);

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
