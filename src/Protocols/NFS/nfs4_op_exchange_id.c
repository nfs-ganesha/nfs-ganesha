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
 * @file    nfs4_op_exchange_id.c
 * @brief   Routines used for managing the NFS4_OP_EXCHANGE_ID operation.
 *
 * Routines used for managing the EXCHANGE_ID operation.
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
 * @brief The NFS4_OP_EXCHANGE_ID operation.
 *
 * This function implements the NFS4_OP_EXCHANGE_ID operation in
 * nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, p. 364
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_exchange_id(struct nfs_argop4 *op,
                        compound_data_t *data,
                        struct nfs_resop4 *resp)
{
  char                  str_verifier[NFS4_VERIFIER_SIZE * 2 + 1];
  char                  str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
  char                  str_client_addr[SOCK_NAME_MAX];
  nfs_client_record_t * client_record;
  nfs_client_id_t     * conf;
  nfs_client_id_t     * unconf;
  sockaddr_t            client_addr;
  int                   rc;
  int                   len;
  char                * temp;
  bool_t                update;
  const char          * update_str;
  log_components_t      component = COMPONENT_CLIENTID;

  if(isDebug(COMPONENT_SESSIONS))
    component = COMPONENT_SESSIONS;

#define arg_EXCHANGE_ID4    op->nfs_argop4_u.opexchange_id
#define res_EXCHANGE_ID4    resp->nfs_resop4_u.opexchange_id
#define res_EXCHANGE_ID4_ok resp->nfs_resop4_u.opexchange_id.EXCHANGE_ID4res_u.eir_resok4

  resp->resop = NFS4_OP_EXCHANGE_ID;
  if (data->minorversion == 0)
    {
      return (res_EXCHANGE_ID4.eir_status = NFS4ERR_INVAL);
    }

  copy_xprt_addr(&client_addr, data->reqp->rq_xprt);

  update = (arg_EXCHANGE_ID4.eia_flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) != 0;

  if(isDebug(component))
    {
      sprint_sockip(&client_addr, str_client_addr, sizeof(str_client_addr));

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
           str_client_addr, str_client, str_verifier, update_str);

  /* Do we already have one or more records for client id (x)? */
  client_record = get_client_record(arg_EXCHANGE_ID4.eia_clientowner
                                    .co_ownerid.co_ownerid_val,
                                    arg_EXCHANGE_ID4.eia_clientowner
                                    .co_ownerid.co_ownerid_len);
  if(client_record == NULL)
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

  P(client_record->cr_mutex);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_record(client_record, str);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Client Record %s cr_pconfirmed_id=%p "
                   "cr_punconfirmed_id=%p",
                   str,
                   client_record->cr_pconfirmed_id,
                   client_record->cr_punconfirmed_id);
    }

  conf = client_record->cr_pconfirmed_id;

  if(conf != NULL)
    {
      /* Need a reference to the confirmed record for below */
      inc_client_id_ref(conf);
    }

  if(conf != NULL && !update)
    {
      /* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A not set */
      if(!nfs_compare_clientcred(&conf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&conf->cid_client_addr, &client_addr, IGNORE_PORT))
        {
          /** @todo FSF: should also check if there is no state */
          P(conf->cid_mutex);

          if(valid_lease(conf))
            {
              V(conf->cid_mutex);

              /* CASE 3, client collisions, old clientid is expired */
              if(isDebug(COMPONENT_CLIENTID))
                {
                  char str[HASHTABLE_DISPLAY_STRLEN];

                  display_client_id_rec(conf, str);
                  LogDebug(COMPONENT_CLIENTID,
                           "Expiring %s",
                           str);
                }
              /* Expire clientid and release our reference. */
              nfs_client_id_expire(conf);

              dec_client_id_ref(conf);

              conf = NULL;
            }
          else
            {
              V(conf->cid_mutex);

              /* CASE 3, client collisions, old clientid is not expired */
              if(isDebug(component))
                {
                  char confirmed_addr[SOCK_NAME_MAX];

                  sprint_sockip(&conf->cid_client_addr,
                                confirmed_addr,
                                sizeof(confirmed_addr));

                  LogDebug(component,
                           "Confirmed ClientId %"PRIx64"->'%s': "
                           "Principals do not match... confirmed addr=%s "
                           "Return NFS4ERR_CLID_INUSE",
                           conf->cid_clientid, str_client, confirmed_addr);
                }

              res_EXCHANGE_ID4.eir_status = NFS4ERR_CLID_INUSE;

              /* Release our reference to the confirmed clientid. */
              dec_client_id_ref(conf);

              goto out;
            }
        }
      else if(memcmp(arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                     conf->cid_incoming_verifier,
                     NFS4_VERIFIER_SIZE) == 0)
        {
          /* CASE 2, Non-Update on Existing Client ID */
          /* Return what was last returned without changing any refcounts */
          LogDebug(COMPONENT_CLIENTID,
                   "Non-update of confirmed ClientId %"PRIx64"->%s",
                   conf->cid_clientid, str_client);

          unconf = conf;

          goto return_ok;
        }
      else
        {
          /* CASE 5, client restart */
          /** @todo FSF: expire old clientid? */
          LogDebug(component,
                   "Restarted ClientId %"PRIx64"->%s",
                   conf->cid_clientid, str_client);
          
        }
    }
  else if(conf != NULL)
    {
      /* EXCHGID4_FLAG_UPD_CONFIRMED_REC_A set */
      if(memcmp(arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
                conf->cid_incoming_verifier,
                NFS4_VERIFIER_SIZE) == 0)
        {
          if(!nfs_compare_clientcred(&conf->cid_credential,
                                     &data->credential) ||
             !cmp_sockaddr(&conf->cid_client_addr, &client_addr, IGNORE_PORT))
            {
              /* CASE 9, Update but wrong principal */
              if(isDebug(component))
                {
                  char confirmed_addr[SOCK_NAME_MAX];

                  sprint_sockip(&conf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

                  LogDebug(component,
                           "Confirmed ClientId %"PRIx64"->'%s': "
                           "Principals do not match... confirmed addr=%s "
                           "Return NFS4ERR_PERM",
                           conf->cid_clientid, str_client, confirmed_addr);
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
                         conf->cid_incoming_verifier,
                         NFS4_VERIFIER_SIZE);

              LogDebug(component,
                       "Confirmed clientid %"PRIx64"->'%s': Verifiers do not match... confirmed verifier=%s",
                       conf->cid_clientid, str_client, str_old_verifier);
            }

          res_EXCHANGE_ID4.eir_status = NFS4ERR_NOT_SAME;
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(conf);

      goto out;
    }
  else if(conf == NULL && update)
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

  unconf = client_record->cr_punconfirmed_id;

  if(unconf != NULL)
    {
      /* CASE 4, replacement of unconfirmed record */

      /* Delete the unconfirmed clientid record */
      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(unconf, str);

          LogDebug(COMPONENT_CLIENTID,
                   "Replacing %s",
                   str);
        }

      /* unhash the clientid record */
      remove_unconfirmed_client_id(unconf);
    }

  /* Now we can proceed to build the new unconfirmed record. We have determined
   * the clientid and setclientid_confirm values above.
   */

  unconf = create_client_id(0,
                            client_record,
                            &client_addr,
                            &data->credential);

  if(unconf == NULL)
    {
      /* Error already logged, return */
      res_EXCHANGE_ID4.eir_status = NFS4ERR_RESOURCE;

      goto out;
    }

  memcpy(unconf->cid_incoming_verifier,
         arg_EXCHANGE_ID4.eia_clientowner.co_verifier,
         NFS4_VERIFIER_SIZE);

  if(gethostname(unconf->cid_server_owner,
                 sizeof(unconf->cid_server_owner)) == -1)
    {
      /* Free the clientid record and return */
      free_client_id(unconf);

      res_EXCHANGE_ID4.eir_status = NFS4ERR_SERVERFAULT;

      goto out;
    }

  snprintf(unconf->cid_server_scope,
           sizeof(unconf->cid_server_scope),
           "%s_NFS-Ganesha",
           unconf->cid_server_owner);

  rc = nfs_client_id_insert(unconf);

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
  res_EXCHANGE_ID4_ok.eir_clientid   = unconf->cid_clientid;
  res_EXCHANGE_ID4_ok.eir_sequenceid = unconf->cid_create_session_sequence;
  /**
   * @todo ACE: Revisit this, if no exports support pNFS, don't
   * advertise pNFS.
   */
  res_EXCHANGE_ID4_ok.eir_flags = EXCHGID4_FLAG_USE_PNFS_MDS |
                                  EXCHGID4_FLAG_USE_PNFS_DS |
                                  EXCHGID4_FLAG_SUPP_MOVED_REFER;

  res_EXCHANGE_ID4_ok.eir_state_protect.spr_how = SP4_NONE;

  len  = strlen(unconf->cid_server_owner);
  temp = gsh_malloc(len);
  if(temp == NULL)
    {
      LogDebug(component,
               "Could not allocate memory for so_major_id in response");
      /** @todo FSF: not the best way to handle this but keeps from crashing */
      len = 0;
    }
  else
    memcpy(temp, unconf->cid_server_owner, len);

  res_EXCHANGE_ID4_ok.eir_server_owner.so_major_id.so_major_id_len = len;
  res_EXCHANGE_ID4_ok.eir_server_owner.so_major_id.so_major_id_val = temp;
  res_EXCHANGE_ID4_ok.eir_server_owner.so_minor_id = 0;

  len  = strlen(unconf->cid_server_scope);
  temp = gsh_malloc(len);
  if(temp == NULL)
    {
      LogDebug(component,
               "Could not allocate memory for eir_server_scope in response");
      /** @todo FSF: not the best way to handle this but keeps from crashing */
      len = 0;
    }
  else
    memcpy(temp, unconf->cid_server_scope, len);

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

      display_client_id_rec(unconf, str);

      LogDebug(COMPONENT_CLIENTID,
               "EXCHANGE_ID reply Verifier=%s %s",
               str_verifier, str);
    }

  res_EXCHANGE_ID4.eir_status = NFS4_OK;

 out:

  V(client_record->cr_mutex);

  /* Release our reference to the client record */
  dec_client_record_ref(client_record);

  return res_EXCHANGE_ID4.eir_status;
}                               /* nfs41_op_exchange_id */

/**
 * @brief free memory alocated for nfs4_op_exchange_id result
 *
 * This function frees memory alocated for nfs4_op_exchange_id result.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 */
void nfs4_op_exchange_id_Free(EXCHANGE_ID4res *resp)
{
  if(resp->eir_status == NFS4_OK)
    {
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope
         .eir_server_scope_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_scope
                 .eir_server_scope_val);
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner.so_major_id
         .so_major_id_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_owner
                 .so_major_id.so_major_id_val);
      if(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id
         .eir_server_impl_id_val != NULL)
        gsh_free(resp->EXCHANGE_ID4res_u.eir_resok4.eir_server_impl_id
                 .eir_server_impl_id_val);
    }
  return;
}                               /* nfs41_op_exchange_id_Free */
