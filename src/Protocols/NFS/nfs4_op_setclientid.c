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
 * \file    nfs4_op_setclientid.c
 * \brief   Routines used for managing the NFS4_OP_SETCLIENTID operation.
 *
 * Routines used for managing the NFS4_OP_SETCLIENTID operation.
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
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_core.h"

/**
 *
 * @brief The NFS4_OP_SETCLIENTID operation.
 *
 * Gets the currentFH for the current compound requests.
 *
 * @param[in]     op   nfs4_op arguments
 * @param[in,out] data Compound request's data
 * @param[out]    resp nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_setclientid(struct nfs_argop4 * op,
                        compound_data_t   * data,
                        struct nfs_resop4 * resp)
{
  char                  str_verifier[NFS4_VERIFIER_SIZE * 2 + 1];
  char                  str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
  nfs_client_record_t * pclient_record;
  nfs_client_id_t     * pconf;
  nfs_client_id_t     * punconf;
  clientid4             clientid;
  verifier4             verifier;
  int                   rc;

#define arg_SETCLIENTID4       op->nfs_argop4_u.opsetclientid
#define res_SETCLIENTID4       resp->nfs_resop4_u.opsetclientid
#define res_SETCLIENTID4_INUSE resp->nfs_resop4_u.opsetclientid.SETCLIENTID4res_u.client_using

  resp->resop = NFS4_OP_SETCLIENTID;

  if(isDebug(COMPONENT_CLIENTID))
    {
      DisplayOpaqueValue(arg_SETCLIENTID4.client.id.id_val,
                         arg_SETCLIENTID4.client.id.id_len,
                         str_client);

      sprint_mem(str_verifier,
                 arg_SETCLIENTID4.client.verifier,
                 NFS4_VERIFIER_SIZE);
    }

  LogDebug(COMPONENT_CLIENTID,
           "SETCLIENTID Client addr=%s id=%s verf=%s callback={program=%u r_addr=%s r_netid=%s} ident=%u",
           data->pworker->hostaddr_str, str_client, str_verifier,
           arg_SETCLIENTID4.callback.cb_program,
           arg_SETCLIENTID4.callback.cb_location.r_addr,
           arg_SETCLIENTID4.callback.cb_location.r_netid,
           arg_SETCLIENTID4.callback_ident);

  /* Do we already have one or more records for client id (x)? */
  pclient_record = get_client_record(arg_SETCLIENTID4.client.id.id_val,
                                     arg_SETCLIENTID4.client.id.id_len);
  if(pclient_record == NULL)
    {
      /* Some major failure */
      LogCrit(COMPONENT_CLIENTID,
              "SETCLIENTID failed to create client record");

      res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;

      return res_SETCLIENTID4.status;
    }

  /*
   * The following checks are based on RFC3530bis draft 16
   *
   * This attempts to implement the logic described in 15.35.5. IMPLEMENTATION
   * Consider the major bullets as CASE 1, CASE 2, CASE 3, CASE 4, and CASE 5.
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

      if(!nfs_compare_clientcred(&pconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&pconf->cid_client_addr,
                       &data->pworker->hostaddr,
                       IGNORE_PORT))
        {
          /* CASE 1:
           *
           * Confirmed record exists and not the same principal
           */
          if(isDebug(COMPONENT_CLIENTID))
            {
              char confirmed_addr[SOCK_NAME_MAX];

              sprint_sockip(&pconf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

              LogDebug(COMPONENT_CLIENTID,
                       "Confirmed ClientId %"PRIx64"->'%s': Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
                       pconf->cid_clientid, str_client, confirmed_addr);
            }

          res_SETCLIENTID4.status = NFS4ERR_CLID_INUSE;
          res_SETCLIENTID4_INUSE.r_netid
               = (char *) netid_nc_table[pconf->cid_cb.cid_addr.nc].netid;
          res_SETCLIENTID4_INUSE.r_addr
               = gsh_strdup(pconf->cid_cb.cid_client_r_addr);

          /* Release our reference to the confirmed clientid. */
          dec_client_id_ref(pconf);

          goto out;
        }

      /* Check if confirmed record is for (v, x, c, l, s) */
      if(memcmp(arg_SETCLIENTID4.client.verifier,
                pconf->cid_incoming_verifier,
                NFS4_VERIFIER_SIZE) == 0)
        {
          /* CASE 2:
           *
           * A confirmed record exists for this long form client id and
           * verifier.
           *
           * Consider this to be a possible update of the call-back information.
           *
           * Remove any pre-existing unconfirmed record for (v, x, c).
           *
           * Return the same short form client id (c), but a new
           * setclientid_confirm verifier (t).
           */
           LogFullDebug(COMPONENT_CLIENTID,
                        "Update ClientId %"PRIx64"->%s",
                        pconf->cid_clientid, str_client);

           clientid = pconf->cid_clientid;

           new_clientifd_verifier(verifier);
        }
      else
        {
          /* Must be CASE 3 or CASE 4
           *
           * Confirmed record is for (u, x, c, l, s).
           *
           * These are actually the same, doesn't really matter if an
           * unconfirmed record exists or not. Any existing unconfirmed record
           * will be removed and a new unconfirmed record added.
           *
           * Return a new short form clientid (d) and a new setclientid_confirm
           * verifier (t). (Note the spec calls the values e and r for CASE 4).
           */
          LogFullDebug(COMPONENT_CLIENTID,
                       "Replace ClientId %"PRIx64"->%s",
                       pconf->cid_clientid, str_client);

          clientid = new_clientid();

          new_clientifd_verifier(verifier);
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(pconf);
    }
  else
    {
      /* CASE 5:
       *
       *
       * Remove any existing unconfirmed record.
       *
       * Return a new short form clientid (d) and a new setclientid_confirm
       * verifier (t).
       */
      LogFullDebug(COMPONENT_CLIENTID,
                   "New client");

      clientid = new_clientid();

      new_clientifd_verifier(verifier);
    }

  /* At this point, no matter what the case was above, we should remove any
   * pre-existing unconfirmed record.
   */

  punconf = pclient_record->cr_punconfirmed_id;

  if(punconf != NULL)
    {
      /* Delete the unconfirmed clientid record. Because we have the cr_mutex,
       * we have won any race to deal with this clientid record (whether we
       * raced with a SETCLIENTID_CONFIRM or the reaper thread (if either of
       * those operations had won the race, cr_punconfirmed_id would have been
       * NULL).
       */
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

      punconf = NULL;
    }

  /* Now we can proceed to build the new unconfirmed record. We have determined
   * the clientid and setclientid_confirm values above.
   */

  punconf = create_client_id(clientid,
                             pclient_record,
                             &data->pworker->hostaddr,
                             &data->credential);

  if(punconf == NULL)
    {
      /* Error already logged, return */
      res_SETCLIENTID4.status = NFS4ERR_RESOURCE;

      goto out;
    }

  strncpy(punconf->cid_cb.cid_client_r_addr,
          arg_SETCLIENTID4.callback.cb_location.r_addr,
          SOCK_NAME_MAX);

  nfs_set_client_location(punconf,
                          &arg_SETCLIENTID4.callback.cb_location);
  
  memcpy(punconf->cid_incoming_verifier,
         arg_SETCLIENTID4.client.verifier,
         NFS4_VERIFIER_SIZE);
  memcpy(punconf->cid_verifier, verifier, sizeof(NFS4_write_verifier));

  punconf->cid_cb.cid_program = arg_SETCLIENTID4.callback.cb_program;
  punconf->cid_cb.cb_u.v40.cb_callback_ident = arg_SETCLIENTID4.callback_ident;

  rc = nfs_client_id_insert(punconf);

  if(rc != CLIENT_ID_SUCCESS)
    {
      /* Record is already freed, return. */
      if(rc == CLIENT_ID_INSERT_MALLOC_ERROR)
        res_SETCLIENTID4.status = NFS4ERR_RESOURCE;
      else
        res_SETCLIENTID4.status = NFS4ERR_SERVERFAULT;

      goto out;
    }

  if(isDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      sprint_mem(str_verifier,
                 verifier,
                 NFS4_VERIFIER_SIZE);

      display_client_id_rec(punconf, str);

      LogDebug(COMPONENT_CLIENTID,
               "SETCLIENTID reply Verifier=%s %s",
               str_verifier, str);
    }

  res_SETCLIENTID4.status = NFS4_OK;

  res_SETCLIENTID4.SETCLIENTID4res_u.resok4.clientid = clientid;

  memcpy(res_SETCLIENTID4.SETCLIENTID4res_u.resok4.setclientid_confirm,
         &verifier,
         NFS4_VERIFIER_SIZE);

 out:

  V(pclient_record->cr_mutex);

  /* Release our reference to the client record */
  dec_client_record_ref(pclient_record);

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
  if(resp->status == NFS4ERR_CLID_INUSE)
    {
      if(resp->SETCLIENTID4res_u.client_using.r_addr != NULL)
        gsh_free(resp->SETCLIENTID4res_u.client_using.r_addr);
    }
  return;
}                               /* nfs4_op_setclientid_Free */
