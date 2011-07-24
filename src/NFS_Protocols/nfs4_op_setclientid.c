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
 * \file    nfs4_op_setclientid.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.9 $
 * \brief   Routines used for managing the NFS4_OP_SETCLIENTID operation.
 *
 * nfs4_op_setclientid.c :  Routines used for managing the NFS4_OP_SETCLIENTID operation.
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
 * nfs4_op_setclientid:  The NFS4_OP_SETCLIENTID operation.
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

int nfs4_op_setclientid(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_setclientid";
  char str_verifier[MAXNAMLEN];
  char str_client[MAXNAMLEN];

#define arg_SETCLIENTID4  op->nfs_argop4_u.opsetclientid
#define res_SETCLIENTID4  resp->nfs_resop4_u.opsetclientid

  clientid4 clientid;
  nfs_client_id_t nfs_clientid;
  nfs_worker_data_t *pworker = NULL;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

  strncpy(str_verifier, arg_SETCLIENTID4.client.verifier, MAXNAMLEN);
  strncpy(str_client, arg_SETCLIENTID4.client.id.id_val,
          arg_SETCLIENTID4.client.id.id_len);
  str_client[arg_SETCLIENTID4.client.id.id_len] = '\0';

  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID Client id len = %u",
           arg_SETCLIENTID4.client.id.id_len);
  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID Client name = #%s#", str_client);
  /*LogDebug(COMPONENT_NFS_V4,
             "SETCLIENTID Verifier = #%s#", str_verifier ) ; */
  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID Callback: cb_program = %u|0x%x, cb_location = { r_addr = %s   r_netid = %s }",
           arg_SETCLIENTID4.callback.cb_program,
           arg_SETCLIENTID4.callback.cb_program,
#ifdef _USE_NFS4_1
           arg_SETCLIENTID4.callback.cb_location.na_r_addr,
           arg_SETCLIENTID4.callback.cb_location.na_r_netid);
#else
           arg_SETCLIENTID4.callback.cb_location.r_addr,
           arg_SETCLIENTID4.callback.cb_location.r_netid);
#endif

  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID callback_ident : %u",
           arg_SETCLIENTID4.callback_ident);

  /* First build the clientid4 nickname */

  /* There was no pb, returns the clientid */
  resp->resop = NFS4_OP_SETCLIENTID;
  res_SETCLIENTID4.status = NFS4_OK;

  /* Compute the client id */
  if(nfs_client_id_basic_compute(str_client, &clientid) != CLIENT_ID_SUCCESS)
    {
      res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;
      return res_SETCLIENTID4.status;
    }
  LogDebug(COMPONENT_NFS_V4,
           "SETCLIENTID computed clientid4=%"PRIx64" for name='%s'",
           clientid, str_client);

  /* Does this id already exists ? */
  if(nfs_client_id_get(clientid, &nfs_clientid) == CLIENT_ID_SUCCESS)
    {
      /* Client id already in use */
      LogDebug(COMPONENT_NFS_V4,
               "SETCLIENTID ClientId %"PRIx64" already in use for client '%s', check if same",
               clientid, nfs_clientid.client_name);

      /* Principals are the same, check content of the setclientid request */
      if(nfs_clientid.confirmed == CONFIRMED_CLIENT_ID)
        {
#ifdef _NFSV4_COMPARE_CRED_IN_SETCLIENTID
          /* Check if client id has same credentials */
          if(nfs_compare_clientcred(&(nfs_clientid.credential), &(data->credential)) ==
             FALSE)
            {
              LogDebug(COMPONENT_NFS_V4,
                       "SETCLIENTID Confirmed ClientId %"PRIx64" -> '%s': Credential do not match... Return NFS4ERR_CLID_INUSE",
                       clientid, nfs_clientid.client_name);

              res_SETCLIENTID4.status = NFS4ERR_CLID_INUSE;
#ifdef _USE_NFS4_1
              res_SETCLIENTID4.SETCLIENTID4res_u.client_using.na_r_netid =
                  nfs_clientid.client_r_netid;
              res_SETCLIENTID4.SETCLIENTID4res_u.client_using.na_r_addr =
                  nfs_clientid.client_r_addr;
#else
              res_SETCLIENTID4.SETCLIENTID4res_u.client_using.r_netid =
                  nfs_clientid.client_r_netid;
              res_SETCLIENTID4.SETCLIENTID4res_u.client_using.r_addr =
                  nfs_clientid.client_r_addr;
#endif
              return res_SETCLIENTID4.status;
            }
          else
            LogDebug(COMPONENT_NFS_V4,
                     "SETCLIENTID ClientId %"PRIx64" is set again by same principal",
                     clientid);
#endif

          /* Ask for a different client with the same client id... returns an error if different client */
          LogDebug(COMPONENT_NFS_V4,
                   "SETCLIENTID Confirmed ClientId %"PRIx64" already in use for client '%s'",
                   clientid, nfs_clientid.client_name);

          if(strncmp
             (nfs_clientid.incoming_verifier, arg_SETCLIENTID4.client.verifier,
              NFS4_VERIFIER_SIZE))
            {
              LogDebug(COMPONENT_NFS_V4,
                       "SETCLIENTID Confirmed ClientId %"PRIx64" already in use for client '%s', verifier do not match...",
                       clientid, nfs_clientid.client_name);

              /* A client has rebooted and rebuilds its state */
              LogDebug(COMPONENT_NFS_V4,
                       "Probably something to be done here: a client has rebooted and try recovering its state. Update the record for this client");

              /* Update the record, but set it as REBOOTED */
              strncpy(nfs_clientid.client_name, arg_SETCLIENTID4.client.id.id_val,
                      arg_SETCLIENTID4.client.id.id_len);
              nfs_clientid.client_name[arg_SETCLIENTID4.client.id.id_len] = '\0';
#ifdef _USE_NFS4_1
              strncpy(nfs_clientid.client_r_addr,
                      arg_SETCLIENTID4.callback.cb_location.na_r_addr, SOCK_NAME_MAX);
              strncpy(nfs_clientid.client_r_netid,
                      arg_SETCLIENTID4.callback.cb_location.na_r_netid, MAXNAMLEN);
#else
              strncpy(nfs_clientid.client_r_addr,
                      arg_SETCLIENTID4.callback.cb_location.r_addr, SOCK_NAME_MAX);
              strncpy(nfs_clientid.client_r_netid,
                      arg_SETCLIENTID4.callback.cb_location.r_netid, MAXNAMLEN);
#endif
              strncpy(nfs_clientid.incoming_verifier, arg_SETCLIENTID4.client.verifier,
                      NFS4_VERIFIER_SIZE);
              snprintf(nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u",
                       (unsigned int)ServerBootTime);
              nfs_clientid.confirmed = REBOOTED_CLIENT_ID;
              nfs_clientid.cb_program = arg_SETCLIENTID4.callback.cb_program;
              nfs_clientid.clientid = clientid;
              nfs_clientid.last_renew = 0;

              if(nfs_client_id_set(clientid, nfs_clientid, &pworker->clientid_pool) !=
                 CLIENT_ID_SUCCESS)
                {
                  res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;
                  return res_SETCLIENTID4.status;
                }

            }
          else
            {
              LogDebug(COMPONENT_NFS_V4,
                       "SETCLIENTID Confirmed ClientId %"PRIx64" already in use for client '%s', verifier matches. Now check callback",
                       clientid, nfs_clientid.client_name);

              if(nfs_clientid.cb_program == arg_SETCLIENTID4.callback.cb_program)
                {
                  LogDebug(COMPONENT_NFS_V4,
                           "SETCLIENTID with same arguments for aleady confirmed client '%s'",
                           nfs_clientid.client_name);
                  LogDebug(COMPONENT_NFS_V4,
                           "SETCLIENTID '%s' will set the client UNCONFIRMED and returns NFS4_OK",
                           nfs_clientid.client_name);

                  /* Set the client UNCONFIRMED */
                  nfs_clientid.confirmed = UNCONFIRMED_CLIENT_ID;

                  res_SETCLIENTID4.status = NFS4_OK;

                  /* Update the stateid hash */
                  if(nfs_client_id_set(clientid, nfs_clientid, &pworker->clientid_pool) !=
                     CLIENT_ID_SUCCESS)
                    {
                      res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;
                      return res_SETCLIENTID4.status;
                    }

                }
              else
                {
                  LogDebug(COMPONENT_NFS_V4,
                           "SETCLIENTID Confirmed ClientId %"PRIx64" already in use for client '%s', verifier matches. Different callback program 0x%x != 0x%x",
                           clientid, nfs_clientid.client_name,
                           nfs_clientid.cb_program,
                           arg_SETCLIENTID4.callback.cb_program);
                }
            }
        }
      else
        LogDebug(COMPONENT_NFS_V4,
                 "SETCLIENTID ClientId %"PRIx64" already in use for client '%s', but unconfirmed",
                 clientid, nfs_clientid.client_name);
    }
  else
    {
      /* Build the client record */
      strncpy(nfs_clientid.client_name, arg_SETCLIENTID4.client.id.id_val,
              arg_SETCLIENTID4.client.id.id_len);
      nfs_clientid.client_name[arg_SETCLIENTID4.client.id.id_len] = '\0';
#ifdef _USE_NFS4_1
      strncpy(nfs_clientid.client_r_addr, arg_SETCLIENTID4.callback.cb_location.na_r_addr,
              SOCK_NAME_MAX);
      strncpy(nfs_clientid.client_r_netid,
              arg_SETCLIENTID4.callback.cb_location.na_r_netid, MAXNAMLEN);
#else
      strncpy(nfs_clientid.client_r_addr, arg_SETCLIENTID4.callback.cb_location.r_addr,
              SOCK_NAME_MAX);
      strncpy(nfs_clientid.client_r_netid, arg_SETCLIENTID4.callback.cb_location.r_netid,
              MAXNAMLEN);
#endif
      strncpy(nfs_clientid.incoming_verifier, arg_SETCLIENTID4.client.verifier,
              NFS4_VERIFIER_SIZE);
      snprintf(nfs_clientid.verifier, NFS4_VERIFIER_SIZE, "%u",
               (unsigned int)ServerBootTime);
      nfs_clientid.confirmed = UNCONFIRMED_CLIENT_ID;
      nfs_clientid.cb_program = arg_SETCLIENTID4.callback.cb_program;
      nfs_clientid.clientid = clientid;
      nfs_clientid.last_renew = 0;
      nfs_clientid.credential = data->credential;

      if(nfs_client_id_add(clientid, nfs_clientid, &pworker->clientid_pool) !=
         CLIENT_ID_SUCCESS)
        {
          res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;
          return res_SETCLIENTID4.status;
        }
    }

  res_SETCLIENTID4.SETCLIENTID4res_u.resok4.clientid = clientid;
  memset(res_SETCLIENTID4.SETCLIENTID4res_u.resok4.setclientid_confirm, 0,
         NFS4_VERIFIER_SIZE);
  snprintf(res_SETCLIENTID4.SETCLIENTID4res_u.resok4.setclientid_confirm,
           NFS4_VERIFIER_SIZE, "%u", (unsigned int)ServerBootTime);

  /* LogDebug(COMPONENT_NFS_V4,
              "SETCLIENTID reply :ClientId=%llx Verifier=%s",  
              res_SETCLIENTID4.SETCLIENTID4res_u.resok4.clientid ,
              res_SETCLIENTID4.SETCLIENTID4res_u.resok4.setclientid_confirm ) ; */

  res_SETCLIENTID4.status = NFS4_OK;
  return res_SETCLIENTID4.status;
}                               /* nfs4_op_setclientid */

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
void nfs4_op_setclientid_Free(SETCLIENTID4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_setclientid_Free */
