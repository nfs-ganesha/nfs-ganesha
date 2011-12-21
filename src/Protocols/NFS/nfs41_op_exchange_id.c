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
 * \file    nfs41_op_exchange_id.c
 * \author  $Author: deniel $
 * \date    $Date: 2009/08/19 16:02:52 $
 * \brief   Routines used for managing the NFS4_OP_EXCHANGE_ID operation.
 *
 * nfs4_op_exchange_id.c :  Routines used for managing the EXCHANGE_ID operation.
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
 * nfs41_op_exchange_id:  The NFS4_OP_EXCHANGE_ID operation.
 *
 * Gets the currentFH for the current compound requests.
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

#if 0
static uint32_t all_eia_flags = 
    EXCHGID4_FLAG_SUPP_MOVED_MIGR |
    EXCHGID4_FLAG_BIND_PRINC_STATEID |
    EXCHGID4_FLAG_USE_NON_PNFS |
    EXCHGID4_FLAG_USE_PNFS_MDS |
    EXCHGID4_FLAG_USE_PNFS_DS |
    EXCHGID4_FLAG_MASK_PNFS |
    EXCHGID4_FLAG_UPD_CONFIRMED_REC_A | EXCHGID4_FLAG_CONFIRMED_R;
#endif

int nfs41_op_exchange_id(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_setclientid";
  char str_verifier[MAXNAMLEN];
  char str_client[MAXNAMLEN];

#define arg_EXCHANGE_ID4  op->nfs_argop4_u.opexchange_id
#define res_EXCHANGE_ID4  resp->nfs_resop4_u.opexchange_id

  clientid4 clientid;
  nfs_client_id_t nfs_clientid;
  nfs_worker_data_t *pworker = NULL;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

  strncpy(str_verifier, arg_EXCHANGE_ID4.eia_clientowner.co_verifier, MAXNAMLEN);
  strncpy(str_client, arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val,
          arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len);
  str_client[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len] = '\0';

  LogDebug(COMPONENT_NFS_V4, "EXCHANGE_ID Client id len = %u",
           arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len);
  LogDebug(COMPONENT_NFS_V4, "EXCHANGE_ID Client name = #%s#", str_client);
  //LogDebug(COMPONENT_NFS_V4, "EXCHANGE_ID Verifier = #%s#", str_verifier ) ; 

  /* There was no pb, returns the clientid */
  resp->resop = NFS4_OP_EXCHANGE_ID;
  res_EXCHANGE_ID4.eir_status = NFS4_OK;

  /* Compute the client id */
  if(nfs_client_id_basic_compute(str_client, &clientid) != CLIENT_ID_SUCCESS)
    {
      res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;
      return res_EXCHANGE_ID4.eir_status;
    }
  LogDebug(COMPONENT_NFS_V4,
           "EXCHANGE_ID computed clientid4=%llux for name='%s'",
           (long long unsigned int)clientid, str_client);

#if 0 //plante le client sous windows. Ai-je réellement besoin de cela ???? //
  /* Check flags value (test EID4) */
  if(arg_EXCHANGE_ID4.eia_flags & all_eia_flags != arg_EXCHANGE_ID4.eia_flags)
    {
      res_EXCHANGE_ID4.eir_status = NFS4ERR_INVAL;
      return res_EXCHANGE_ID4.eir_status;
    }
#endif 

  /* Does this id already exists ? */
  if(nfs_client_id_get(clientid, &nfs_clientid) == CLIENT_ID_SUCCESS)
    {
      /* Client id already in use */
      LogDebug(COMPONENT_NFS_V4,
               "EXCHANGE_ID ClientId %llx already in use for client '%s', check if same",
               (long long unsigned int)clientid, nfs_clientid.client_name);

      /* Principals are the same, check content of the setclientid request */
      if(nfs_clientid.confirmed == CONFIRMED_CLIENT_ID)
        {
#ifdef _NFSV4_COMPARE_CRED_IN_EXCHANGE_ID
          /* Check if client id has same credentials */
          if(nfs_compare_clientcred(&(nfs_clientid.credential), &(data->credential)) ==
             FALSE)
            {
              LogDebug(COMPONENT_NFS_V4,
                       "EXCHANGE_ID Confirmed ClientId %llx -> '%s': Credential do not match... Return NFS4ERR_CLID_INUSE",
                       clientid, nfs_clientid.client_name);

              res_EXCHANGE_ID4.eir_status = NFS4ERR_CLID_INUSE;
#ifdef _USE_NFS4_1
              res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.na_r_netid =
                  nfs_clientid.client_r_netid;
              res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.na_r_addr =
                  nfs_clientid.client_r_addr;
#else
              res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.r_netid =
                  nfs_clientid.client_r_netid;
              res_EXCHANGE_ID4.EXCHANGE_ID4res_u.client_using.r_addr =
                  nfs_clientid.client_r_addr;
#endif
              return res_EXCHANGE_ID4.eir_status;
            }
          else
            LogDebug(COMPONENT_NFS_V4,
                     "EXCHANGE_ID ClientId %llx is set again by same principal",
                     clientid);
#endif

          /* Ask for a different client with the same client id... returns an error if different client */
          LogDebug(COMPONENT_NFS_V4,
                   "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s'",
                   (long long unsigned int)clientid, nfs_clientid.client_name);

          if(strncmp
             (nfs_clientid.incoming_verifier,
              arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE))
            {
              LogDebug(COMPONENT_NFS_V4,
                       "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s', verifier do not match...",
                       (long long unsigned int)clientid, nfs_clientid.client_name);

              /* A client has rebooted and rebuilds its state */
              LogDebug(COMPONENT_NFS_V4,
                       "Probably something to be done here: a client has rebooted and try recovering its state. Update the record for this client");

              /* Update the record, but set it as REBOOTED */
              strncpy(nfs_clientid.client_name,
                      arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val,
                      arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len);
              nfs_clientid.client_name[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.
                                       co_ownerid_len] = '\0';

              strncpy(nfs_clientid.incoming_verifier,
                      arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE);
              snprintf(nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u",
                       (unsigned int)ServerBootTime);
              nfs_clientid.confirmed = REBOOTED_CLIENT_ID;
              nfs_clientid.clientid = clientid;
              nfs_clientid.last_renew = 0;

              if(nfs_client_id_set(clientid, nfs_clientid, &pworker->clientid_pool) !=
                 CLIENT_ID_SUCCESS)
                {
                  res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;
                  return res_EXCHANGE_ID4.eir_status;
                }

            }
          else
            {
              LogDebug(COMPONENT_NFS_V4,
                       "EXCHANGE_ID Confirmed ClientId %llx already in use for client '%s', verifier matches. Now check callback",
                       (long long unsigned int)clientid, nfs_clientid.client_name);
            }
        }
      else
        {
          LogDebug(COMPONENT_NFS_V4,
                   "EXCHANGE_ID ClientId %llx already in use for client '%s', but unconfirmed",
                   (long long unsigned int)clientid, nfs_clientid.client_name);
          LogCrit(COMPONENT_NFS_V4,
	          "Reuse of a formerly obtained clientid that is not yet confirmed."); // Code needs to be improved here.
        }
    }
  else
    {
      /* Build the client record */
      strncpy(nfs_clientid.client_name,
              arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val,
              arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len);
      nfs_clientid.client_name[arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.
                               co_ownerid_len] = '\0';

      strncpy(nfs_clientid.incoming_verifier,
              arg_EXCHANGE_ID4.eia_clientowner.co_verifier, NFS4_VERIFIER_SIZE);
      snprintf(nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u",
               (unsigned int)ServerBootTime);
      nfs_clientid.confirmed = UNCONFIRMED_CLIENT_ID;
      nfs_clientid.cb_program = 0;      /* to be set at create_session time */
      nfs_clientid.clientid = clientid;
      nfs_clientid.last_renew = 0;
      nfs_clientid.nb_session = 0;
      nfs_clientid.create_session_sequence = 1;
      nfs_clientid.credential = data->credential;

      if(gethostname(nfs_clientid.server_owner, MAXNAMLEN) == -1)
        {
          res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;
          return res_EXCHANGE_ID4.eir_status;
        }
//      strncpy(nfs_clientid.server_scope, nfs_clientid.server_owner, MAXNAMLEN);

      snprintf( nfs_clientid.server_scope, MAXNAMLEN, "%s_NFS-Ganesha", nfs_clientid.server_owner ) ;

      if(nfs_client_id_add(clientid, nfs_clientid, &pworker->clientid_pool) !=
         CLIENT_ID_SUCCESS)
        {
          res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;
          return res_EXCHANGE_ID4.eir_status;
        }
    }

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_clientid = clientid;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_sequenceid =
      nfs_clientid.create_session_sequence;
#ifdef _USE_PNFS
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_flags =
      EXCHGID4_FLAG_USE_PNFS_MDS | EXCHGID4_FLAG_USE_PNFS_DS |
      EXCHGID4_FLAG_SUPP_MOVED_REFER;
#else
#ifdef _USE_DS
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_flags =
      EXCHGID4_FLAG_USE_PNFS_MDS | EXCHGID4_FLAG_USE_PNFS_DS |
      EXCHGID4_FLAG_SUPP_MOVED_REFER;
#else
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_flags =
      EXCHGID4_FLAG_USE_NON_PNFS | EXCHGID4_FLAG_SUPP_MOVED_REFER;
#endif                          /* USE_DS */
#endif                          /* USE_PNFS */

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_state_protect.spr_how = SP4_NONE;

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.
      so_major_id_len = strlen(nfs_clientid.server_owner);
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.
      so_major_id_val = Mem_Alloc(strlen(nfs_clientid.server_owner));
  memcpy(res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.
         so_major_id_val, nfs_clientid.server_owner, strlen(nfs_clientid.server_owner));
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_minor_id = 0;

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_len =
      strlen(nfs_clientid.server_scope);
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val =
      Mem_Alloc(strlen(nfs_clientid.server_scope));
  memcpy(res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.
         eir_server_scope_val, nfs_clientid.server_scope,
         strlen(nfs_clientid.server_scope));

  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.
      eir_server_impl_id_len = 0;
  res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.
      eir_server_impl_id_val = NULL;

  LogDebug(COMPONENT_NFS_V4, "EXCHANGE_ID reply :ClientId=%llx",
           (long long unsigned int)res_EXCHANGE_ID4.EXCHANGE_ID4res_u.eir_resok4.eir_clientid);

  res_EXCHANGE_ID4.eir_status = NFS4_OK;
  return res_EXCHANGE_ID4.eir_status;
}                               /* nfs41_op_exchange_id */

/**
 * nfs4_op_setclientid_Free: frees what was allocared to handle nfs4_op_setclientid.
 * 
 * Frees what was allocared to handle nfs4_op_setclientid.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_exchange_id_Free(EXCHANGE_ID4res * resp)
{
  Mem_Free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val);
  Mem_Free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.
           so_major_id_val);
  return;
}                               /* nfs41_op_exchange_id_Free */
