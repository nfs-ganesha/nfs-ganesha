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
 * \file    nfs4_op_setclientid_confirm.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * nfs4_op_setclientid_confirm.c :  Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
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
 * nfs4_op_setclientid_confirm:  The NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * The NFS4_OP_SETCLIENTID_CONFIRM operation.
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

int nfs4_op_setclientid_confirm(struct nfs_argop4 *op,
                                compound_data_t * data, struct nfs_resop4 *resp)
{
  nfs_client_id_t nfs_clientid;
  clientid4 clientid = 0;
  nfs_worker_data_t *pworker = NULL;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

#define arg_SETCLIENTID_CONFIRM4 op->nfs_argop4_u.opsetclientid_confirm
#define res_SETCLIENTID_CONFIRM4 resp->nfs_resop4_u.opsetclientid_confirm

  resp->resop = NFS4_OP_SETCLIENTID_CONFIRM;
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
  clientid = arg_SETCLIENTID_CONFIRM4.clientid;

  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID_CONFIRM clientid = %"PRIx64, clientid);
  /* LogDebug(COMPONENT_NFS_V4,
              "SETCLIENTID_CONFIRM Verifier = #%s#",
              arg_SETCLIENTID_CONFIRM4.setclientid_confirm ) ; */

  /* Does this id already exists ? */
  if(nfs_client_id_get(clientid, &nfs_clientid) == CLIENT_ID_SUCCESS)
    {
      /* The client id should not be confirmed */
      if(nfs_clientid.confirmed == CONFIRMED_CLIENT_ID)
        {
          /* Client id was already confirmed and is then in use, this is NFS4ERR_CLID_INUSE if not same client */

          /* Check the verifier */
          if(strncmp
             (nfs_clientid.verifier, arg_SETCLIENTID_CONFIRM4.setclientid_confirm,
              NFS4_VERIFIER_SIZE))
            {
              /* Bad verifier */
              res_SETCLIENTID_CONFIRM4.status = NFS4ERR_CLID_INUSE;
              return res_SETCLIENTID_CONFIRM4.status;
            }
        }
      else
        {
          if(nfs_clientid.confirmed == REBOOTED_CLIENT_ID)
            {
              LogDebug(COMPONENT_NFS_V4,
                       "SETCLIENTID_CONFIRM clientid = %"PRIx64", client was rebooted, getting ride of old state from previous client instance",
                       clientid);
            }

          /* Regular situation, set the client id confirmed and returns */
          nfs_clientid.confirmed = CONFIRMED_CLIENT_ID;

          /* Set the time for the client id */
          nfs_clientid.last_renew = time(NULL);

          /* Set the new value */
          if(nfs_client_id_set(clientid, nfs_clientid, &pworker->clientid_pool) !=
             CLIENT_ID_SUCCESS)
            {
              res_SETCLIENTID_CONFIRM4.status = NFS4ERR_SERVERFAULT;
              return res_SETCLIENTID_CONFIRM4.status;
            }
        }
    }
  else
    {
      /* The client id does not exist: stale client id */
      res_SETCLIENTID_CONFIRM4.status = NFS4ERR_STALE_CLIENTID;
      return res_SETCLIENTID_CONFIRM4.status;
    }

  /* Successful exit */
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
  return res_SETCLIENTID_CONFIRM4.status;
}                               /* nfs4_op_setclientid_confirm */

/**
 * nfs4_op_setclientid_confirm_Free: frees what was allocared to handle nfs4_op_setclientid_confirm.
 * 
 * Frees what was allocared to handle nfs4_op_setclientid_confirm.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_setclientid_confirm_Free(SETCLIENTID_CONFIRM4res * resp)
{
  /* To be completed */
  return;
}                               /* nfs4_op_setclientid_confirm_Free */
