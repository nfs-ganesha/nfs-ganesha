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
 * \file    nfs4_op_create_session.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 17:02:52 $
 * \brief   Routines used for managing the NFS4_OP_CREATE_SESSION operation.
 *
 * nfs4_op_create_session.c :  Routines used for managing the NFS4_OP_CREATE_SESSION operation.
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
 * nfs4_op_create_session:  The NFS4_OP_CREATE_SESSION operation.
 *
 * The NFS4_OP_CREATE_SESSION operation.
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

int nfs41_op_create_session(struct nfs_argop4 *op,
                            compound_data_t * data, struct nfs_resop4 *resp)
{
  nfs_client_id_t *pnfs_clientid;
  nfs41_session_t *pnfs41_session = NULL;
  clientid4 clientid = 0;
  nfs_worker_data_t *pworker = NULL;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

#define arg_CREATE_SESSION4 op->nfs_argop4_u.opcreate_session
#define res_CREATE_SESSION4 resp->nfs_resop4_u.opcreate_session

  resp->resop = NFS4_OP_CREATE_SESSION;
  res_CREATE_SESSION4.csr_status = NFS4_OK;
  clientid = arg_CREATE_SESSION4.csa_clientid;

  LogDebug(COMPONENT_NFS_V4,
           "CREATE_SESSION clientid = %llx",
           (long long unsigned int)clientid);

  /* Does this id already exists ? */
  if(nfs_client_id_Get_Pointer(clientid, &pnfs_clientid) != CLIENT_ID_SUCCESS)
    {
      /* The client id does not exist: stale client id */
      res_CREATE_SESSION4.csr_status = NFS4ERR_STALE_CLIENTID;
      return res_CREATE_SESSION4.csr_status;
    }

  data->use_drc = FALSE;

  if(data->oppos == 0)
    {
      /* Special case : the request is used without use of OP_SEQUENCE */
      if((arg_CREATE_SESSION4.csa_sequence + 1 == pnfs_clientid->create_session_sequence)
         && (pnfs_clientid->create_session_slot.cache_used == TRUE))
        {
          data->use_drc = TRUE;
          data->pcached_res = pnfs_clientid->create_session_slot.cached_result;

          res_CREATE_SESSION4.csr_status = NFS4_OK;
          return res_CREATE_SESSION4.csr_status;
        }
      else if(arg_CREATE_SESSION4.csa_sequence != pnfs_clientid->create_session_sequence)
        {
          res_CREATE_SESSION4.csr_status = NFS4ERR_SEQ_MISORDERED;
          return res_CREATE_SESSION4.csr_status;
        }

    }

  pnfs_clientid->confirmed = CONFIRMED_CLIENT_ID;
  pnfs_clientid->cb_program = arg_CREATE_SESSION4.csa_cb_program;

  pnfs_clientid->create_session_sequence += 1;
  /** @todo: BUGAZOMEU Gerer les parametres de secu */

  /* Record session related information at the right place */
  GetFromPool(pnfs41_session, &data->pclient->pool_session, nfs41_session_t);

  if(pnfs41_session == NULL)
    {
      res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;
      return res_CREATE_SESSION4.csr_status;
    }

  /* Check flags value (test CSESS15) */
  if(arg_CREATE_SESSION4.csa_flags > CREATE_SESSION4_FLAG_CONN_RDMA)
    {
      res_CREATE_SESSION4.csr_status = NFS4ERR_INVAL;
      return res_CREATE_SESSION4.csr_status;
    }

  memset((char *)pnfs41_session, 0, sizeof(nfs41_session_t));
  pnfs41_session->clientid = clientid;
  pnfs41_session->sequence = 1;
  pnfs41_session->session_flags = CREATE_SESSION4_FLAG_CONN_BACK_CHAN;
  pnfs41_session->fore_channel_attrs = arg_CREATE_SESSION4.csa_fore_chan_attrs;
  pnfs41_session->back_channel_attrs = arg_CREATE_SESSION4.csa_back_chan_attrs;

  /* Set ca_maxrequests */
  pnfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS;
  pnfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS;

  if(nfs41_Build_sessionid(&clientid, pnfs41_session->session_id) != 1)
    {
      res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;
      return res_CREATE_SESSION4.csr_status;
    }

  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_sequence = 1;
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_flags =
      CREATE_SESSION4_FLAG_CONN_BACK_CHAN;

  /* return the input for wantinf of something better (will change in later versions) */
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_fore_chan_attrs =
      pnfs41_session->fore_channel_attrs;
  res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_back_chan_attrs =
      pnfs41_session->back_channel_attrs;

  memcpy(res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4.csr_sessionid,
         pnfs41_session->session_id, NFS4_SESSIONID_SIZE);

  /* Create Session replay cache */
  data->pcached_res = pnfs_clientid->create_session_slot.cached_result;
  pnfs_clientid->create_session_slot.cache_used = TRUE;

  if(!nfs41_Session_Set(pnfs41_session->session_id, pnfs41_session))
    {
      res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;     /* Maybe a more precise status would be better */
      return res_CREATE_SESSION4.csr_status;
    }

  /* Successful exit */
  res_CREATE_SESSION4.csr_status = NFS4_OK;
  return res_CREATE_SESSION4.csr_status;
}                               /* nfs41_op_create_session */

/**
 * nfs41_op_create_session_Free: frees what was allocared to handle nfs41_op_create_session.
 * 
 * Frees what was allocared to handle nfs41_op_create_session.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_create_session_Free(CREATE_SESSION4res * resp)
{
  /* To be completed */
  return;
}                               /* nfs41_op_create_session_Free */
