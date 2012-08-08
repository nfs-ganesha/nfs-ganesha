/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs41_op_exchange_id.c
 * \brief   Routines used for managing the NFS4_OP_EXCHANGE_ID operation.
 *
 * Routines used for managing the EXCHANGE_ID operation.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <pthread.h>
#include "log.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"

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
  char                  str_verifier[NFS4_VERIFIER_SIZE * 2 + 1];
  char                  str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
  nfs_client_record_t * pclient_record;
  nfs_client_id_t     * pconf;
  nfs_client_id_t     * punconf;
  int                   rc;
  int                   len;
  char                * temp;
  bool_t                update;
  const char          * update_str;
  log_components_t      component = COMPONENT_CLIENTID;

#if 0 /** @todo: plante le client sous windows. Ai-je réellement besoin de cela ???? */
  /* Check flags value (test EID4) */
  if(arg_EXCHANGE_ID4.eia_flags & all_eia_flags != arg_EXCHANGE_ID4.eia_flags)
    {
      res_EXCHANGE_ID4.eir_status = NFS4ERR_INVAL;
      return res_EXCHANGE_ID4.eir_status;
    }
#endif

  if(isDebug(COMPONENT_SESSIONS))
    component = COMPONENT_SESSIONS;

#define arg_EXCHANGE_ID4    op->nfs_argop4_u.opexchange_id
#define res_EXCHANGE_ID4    resp->nfs_resop4_u.opexchange_id
#define res_EXCHANGE_ID4_ok resp->nfs_resop4_u.opexchange_id.EXCHANGE_ID4res_u.eir_resok4

  resp->resop = NFS4_OP_EXCHANGE_ID;

  update = (arg_EXCHANGE_ID4.eia_flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) != 0;

  if(isDebug(component))
    {
      DisplayOpaqueValue(arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val,
                         arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len,
                         str_client);

      sprint_mem(str_verifier,
                 arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                 NFS4_VERIFIER_SIZE);

      update_str = update ? "UPDATE" : "NO UPDATE";
    }

  LogDebug(component,
           "EXCHANGE_ID Client addr=%s id=%s verf=%s %s --------------------",
           data->pworker->hostaddr_str,
           str_client, str_verifier, update_str);

  /* Do we already have one or more records for client id (x)? */
  pclient_record = get_client_record(arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_val,
                                     arg_EXCHANGE_ID4.eia_clientowner.co_ownerid.co_ownerid_len);
  if(pclient_record == NULL)
    {
      /* Some major failure */
      LogCrit(component,
              "EXCHANGE_ID failed");
      res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;
      return res_EXCHANGE_ID4.eir_status;
    }

  /*
   * The following checks are based on RFC5661
   *
   * This attempts to implement the logic described in 18.35.4. IMPLEMENTATION
   */

  P(pclient_record->cr_mutex);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_record(pclient_record, str);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Client Record %s cr_pconfirmed_id=%p cr_punconfirmed_id=%p",
                   str,
                   pclient_record->cr_pconfirmed_id,
                   pclient_record->cr_punconfirmed_id);
    }

  pconf = pclient_record->cr_pconfirmed_id;

  if(pconf != NULL)
    {
      /* Need a reference to the confirmed record for below */
      inc_client_id_ref(pconf);
    }

  if(pconf != NULL && !update)
    {
      /* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A not set */
      /** @todo FSF: old code ifdefed out nfs_compare_clientcred with _NFSV4_COMPARE_CRED_IN_EXCHANGE_ID */
      if(!nfs_compare_clientcred(&pconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&pconf->cid_client_addr,
                       &data->pworker->hostaddr,
                       IGNORE_PORT))
        {
          /** @todo FSF: should also check if there is no state */
          P(pconf->cid_mutex);

          if(valid_lease(pconf))
            {
              V(pconf->cid_mutex);

              /* CASE 3, client collisions, old clientid is expired */
              if(isDebug(COMPONENT_CLIENTID))
                {
                  char str[HASHTABLE_DISPLAY_STRLEN];

                  display_client_id_rec(pconf, str);
                  LogDebug(COMPONENT_CLIENTID,
                           "Expiring %s",
                           str);
                }
              /* Expire clientid and release our reference. */
              nfs_client_id_expire(pconf);

              dec_client_id_ref(pconf);

              pconf = NULL;
            }
          else
            {
              V(pconf->cid_mutex);

              /* CASE 3, client collisions, old clientid is not expired */
              if(isDebug(component))
                {
                  char confirmed_addr[SOCK_NAME_MAX];

                  sprint_sockip(&pconf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

                  LogDebug(component,
                           "Confirmed ClientId %"PRIx64"->'%s': Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
                           pconf->cid_clientid, str_client, confirmed_addr);
                }

              res_EXCHANGE_ID4.eir_status = NFS4ERR_CLID_INUSE;

              /* Release our reference to the confirmed clientid. */
              dec_client_id_ref(pconf);

              goto out;
            }
        }
      else if(memcmp(arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                     pconf->cid_incoming_verifier,
                     NFS4_VERIFIER_SIZE) == 0)
        {
          /* CASE 2, Non-Update on Existing Client ID */
          /* Return what was last returned without changing any refcounts */
          LogDebug(COMPONENT_CLIENTID,
                   "Non-update of confirmed ClientId %"PRIx64"->%s",
                   pconf->cid_clientid, str_client);

          punconf = pconf;

          goto return_ok;
        }
      else
        {
          /* CASE 5, client restart */
          /** @todo FSF: expire old clientid? */
          LogDebug(component,
                   "Restarted ClientId %"PRIx64"->%s",
                   pconf->cid_clientid, str_client);
          
        }
    }
  else if(pconf != NULL)
    {
      /* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A set */
      if(memcmp(arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                pconf->cid_incoming_verifier,
                NFS4_VERIFIER_SIZE) == 0)
        {
          /** @todo FSF: old code ifdefed out nfs_compare_clientcred with _NFSV4_COMPARE_CRED_IN_EXCHANGE_ID */
          if(!nfs_compare_clientcred(&pconf->cid_credential, &data->credential) ||
             !cmp_sockaddr(&pconf->cid_client_addr,
                           &data->pworker->hostaddr,
                           IGNORE_PORT))
            {
              /* CASE 9, Update but wrong principal */
              if(isDebug(component))
                {
                  char confirmed_addr[SOCK_NAME_MAX];

                  sprint_sockip(&pconf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

                  LogDebug(component,
                           "Confirmed ClientId %"PRIx64"->'%s': Principals do not match... confirmed addr=%s Return NFS4ERR_PERM",
                           pconf->cid_clientid, str_client, confirmed_addr);
                }

              res_EXCHANGE_ID4.eir_status = NFS4ERR_PERM;
            }
          else
            {
              /* CASE 6, Update */
              /** @todo: this is not implemented, the things it updates aren't even tracked */
              LogMajor(component,
                       "EXCHANGE_ID Update not supported");
              res_EXCHANGE_ID4.eir_status = NFS4ERR_NOTSUPP;
            }
        }
      else
        {
          /* CASE 8, Update but wrong verifier */
          if(isDebug(component))
            {
              char str_old_verifier[NFS4_VERIFIER_SIZE * 2 + 1];

              sprint_mem(str_old_verifier,
                         pconf->cid_incoming_verifier,
                         NFS4_VERIFIER_SIZE);

              LogDebug(component,
                       "Confirmed clientid %"PRIx64"->'%s': Verifiers do not match... confirmed verifier=%s",
                       pconf->cid_clientid, str_client, str_old_verifier);
            }

          res_EXCHANGE_ID4.eir_status = NFS4ERR_NOT_SAME;
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(pconf);

      goto out;
    }
  else if(pconf == NULL && update)
    {
      LogDebug(component,
               "No confirmed clientid to update for %s",
               str_client);

      res_EXCHANGE_ID4.eir_status = NFS4ERR_NOENT;

      goto out;
    }

  /* At this point, no matter what the case was above, we should remove any
   * pre-existing unconfirmed record.
   */

  punconf = pclient_record->cr_punconfirmed_id;

  if(punconf != NULL)
    {
      /* CASE 4, replacement of unconfirmed record */

      /* Delete the unconfirmed clientid record */
      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);

          LogDebug(COMPONENT_CLIENTID,
                   "Replacing %s",
                   str);
        }

      /* unhash the clientid record */
      remove_unconfirmed_client_id(punconf);
    }

  /* Now we can proceed to build the new unconfirmed record. We have determined
   * the clientid and setclientid_confirm values above.
   */

  punconf = create_client_id(0,
                             pclient_record,
                             &data->pworker->hostaddr,
                             &data->credential);

  if(punconf == NULL)
    {
      /* Error already logged, return */
      res_EXCHANGE_ID4.eir_status = NFS4ERR_RESOURCE;

      goto out;
    }

  memcpy(punconf->cid_incoming_verifier,
         arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
         NFS4_VERIFIER_SIZE);

  if(gethostname(punconf->cid_server_owner,
                 sizeof(punconf->cid_server_owner)) == -1)
    {
      /* Free the clientid record and return */
      free_client_id(punconf);

      res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;

      goto out;
    }

  snprintf(punconf->cid_server_scope,
           sizeof(punconf->cid_server_scope),
           "%s_NFS-Ganesha",
           punconf->cid_server_owner);

  rc = nfs_client_id_insert(punconf);

  if(rc != CLIENT_ID_SUCCESS)
    {
      /* Record is already freed, return. */
      if(rc == CLIENT_ID_INSERT_MALLOC_ERROR)
        res_EXCHANGE_ID4.eir_status = NFS4ERR_RESOURCE;
      else
        res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;

      goto out;
    }

 return_ok:

  /* Build the reply */
  res_EXCHANGE_ID4_ok.eir_clientid   = punconf->cid_clientid;
  res_EXCHANGE_ID4_ok.eir_sequenceid = punconf->cid_create_session_sequence;
#if defined(_USE_FSALMDS) && defined(_USE_FSALDS)
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_PNFS_MDS |
                                  EXCHGID4_FLAG_USE_PNFS_DS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;
#elif defined(_USE_FSALMDS)
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_PNFS_MDS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;
#elif defined(_USE_FSALDS)
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_PNFS_DS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;
#elif defined(_USE_DS)
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_PNFS_MDS |
                                  EXCHGID4_FLAG_USE_PNFS_DS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;
#else
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_NON_PNFS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;
#endif

  res_EXCHANGE_ID4_ok.eir_state_protect.spr_how = SP4_NONE;

  len  = strlen(punconf->cid_server_owner);
  temp = gsh_malloc(len);
  if(temp == NULL)
    {
      LogDebug(component,
               "Could not allocate memory for so_major_id in response");
      /** @todo FSF: not the best way to handle this but keeps from crashing */
      len = 0;
    }
  else
    memcpy(temp, punconf->cid_server_owner, len);

  res_EXCHANGE_ID4_ok.eir_server_owner.so_major_id.so_major_id_len = len;
  res_EXCHANGE_ID4_ok.eir_server_owner.so_major_id.so_major_id_val = temp;
  res_EXCHANGE_ID4_ok.eir_server_owner.so_minor_id = 0;

  len  = strlen(punconf->cid_server_scope);
  temp = gsh_malloc(len);
  if(temp == NULL)
    {
      LogDebug(component,
               "Could not allocate memory for eir_server_scope in response");
      /** @todo FSF: not the best way to handle this but keeps from crashing */
      len = 0;
    }
  else
    memcpy(temp, punconf->cid_server_scope, len);

  res_EXCHANGE_ID4_ok.eir_server_scope.eir_server_scope_len = len;
  res_EXCHANGE_ID4_ok.eir_server_scope.eir_server_scope_val = temp;

  res_EXCHANGE_ID4_ok.eir_server_impl_id.eir_server_impl_id_len = 0;
  res_EXCHANGE_ID4_ok.eir_server_impl_id.eir_server_impl_id_val = NULL;

  if(isDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      sprint_mem(str_verifier,
                 arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                 NFS4_VERIFIER_SIZE);

      display_client_id_rec(punconf, str);

      LogDebug(COMPONENT_CLIENTID,
               "EXCHANGE_ID reply Verifier=%s %s",
               str_verifier, str);
    }

  res_EXCHANGE_ID4.eir_status = NFS4_OK;

 out:

  V(pclient_record->cr_mutex);

  /* Release our reference to the client record */
  dec_client_record_ref(pclient_record);

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
  if(resp->eir_status == NFS4_OK)
    {
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope.eir_server_scope_val);
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id.so_major_id_val);
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.eir_server_impl_id_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id.eir_server_impl_id_val);
    }
  return;
}                               /* nfs41_op_exchange_id_Free */
