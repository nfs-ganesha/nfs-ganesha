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
 * Routines used for managing the NFS4_OP_CREATE_SESSION operation.
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
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "sal_functions.h"

/**
 *
 * @brief The NFS4_OP_CREATE_SESSION operation.
 *
 * The NFS4_OP_CREATE_SESSION operation.
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see nfs4_Compound
 *
 */

int nfs41_op_create_session(struct nfs_argop4 *op,
                            compound_data_t * data, struct nfs_resop4 *resp)
{
  nfs_client_id_t     * pconf   = NULL;
  nfs_client_id_t     * punconf = NULL;
  nfs_client_id_t     * pfound  = NULL;
  nfs_client_record_t * pclient_record;
  nfs41_session_t     * nfs41_session = NULL;
  clientid4             clientid = 0;
  char                  str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
  int                   rc;
  log_components_t      component = COMPONENT_CLIENTID;

  if(isDebug(COMPONENT_SESSIONS))
    component = COMPONENT_SESSIONS;

#define arg_CREATE_SESSION4 op->nfs_argop4_u.opcreate_session
#define res_CREATE_SESSION4 resp->nfs_resop4_u.opcreate_session
#define res_CREATE_SESSION4ok res_CREATE_SESSION4.CREATE_SESSION4res_u.csr_resok4

  resp->resop = NFS4_OP_CREATE_SESSION;
  res_CREATE_SESSION4.csr_status = NFS4_OK;
  clientid = arg_CREATE_SESSION4.csa_clientid;

  LogDebug(component,
           "CREATE_SESSION client addr=%s clientid=%"PRIx64" -------------------",
           data->pworker->hostaddr_str,
           clientid);

  /* First try to look up unconfirmed record */
  rc = nfs_client_id_get_unconfirmed(clientid, &punconf);

  if(rc == CLIENT_ID_SUCCESS)
    {
      pclient_record = punconf->cid_client_record;
      pfound         = punconf;
    }
  else
    {
      rc = nfs_client_id_get_confirmed(clientid, &pconf);
      if(rc != CLIENT_ID_SUCCESS)
        {
          /* No record whatsoever of this clientid */
          LogDebug(component,
                   "Stale clientid = %"PRIx64,
                   clientid);
          res_CREATE_SESSION4.csr_status = NFS4ERR_STALE_CLIENTID;

          return res_CREATE_SESSION4.csr_status;
        }
      pclient_record = pconf->cid_client_record;
      pfound         = pconf;
    }

  P(pclient_record->cr_mutex);

  inc_client_record_ref(pclient_record);

  if(isFullDebug(component))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_record(pclient_record, str);

      LogFullDebug(component,
                   "Client Record %s cr_pconfirmed_id=%p cr_punconfirmed_id=%p",
                   str,
                   pclient_record->cr_pconfirmed_id,
                   pclient_record->cr_punconfirmed_id);
    }

  /* At this point one and only one of pconf and punconf is non-NULL, and pfound
   * also references the single clientid record that was found.
   */

  LogDebug(component,
           "CREATE_SESSION clientid=%"PRIx64" csa_sequence=%"PRIu32
           " clientid_cs_seq=%" PRIu32" data_oppos=%d data_use_drc=%d",
           clientid,
           arg_CREATE_SESSION4.csa_sequence,
           pfound->cid_create_session_sequence,
           data->oppos,
           data->use_drc);

  if(isFullDebug(component))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_id_rec(pfound, str);
      LogFullDebug(component,
                   "Found %s",
                   str);
    }

  data->use_drc = FALSE;

  if(data->oppos == 0)
    {
      /* Special case : the request is used without use of OP_SEQUENCE */
      if((arg_CREATE_SESSION4.csa_sequence + 1 == pfound->cid_create_session_sequence)
         && (pfound->cid_create_session_slot.cache_used == TRUE))
        {
          data->use_drc = TRUE;
          data->pcached_res = &pfound->cid_create_session_slot.cached_result;

          res_CREATE_SESSION4.csr_status = NFS4_OK;

          dec_client_id_ref(pfound);

          LogDebug(component, "CREATE_SESSION replay=%p special case", data->pcached_res);

          goto out;
        }
      else if(arg_CREATE_SESSION4.csa_sequence != pfound->cid_create_session_sequence)
        {
          res_CREATE_SESSION4.csr_status = NFS4ERR_SEQ_MISORDERED;

          dec_client_id_ref(pfound);

          LogDebug(component, "CREATE_SESSION returning NFS4ERR_SEQ_MISORDERED");

          goto out;
        }

    }

  if(punconf != NULL)
    {
      /* First must match principal */
      if(!nfs_compare_clientcred(&punconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&punconf->cid_client_addr,
                       &data->pworker->hostaddr,
                       IGNORE_PORT))
        {
          if(isDebug(component))
            {
              char unconfirmed_addr[SOCK_NAME_MAX];

              sprint_sockip(&punconf->cid_client_addr, unconfirmed_addr, sizeof(unconfirmed_addr));

              LogDebug(component,
                       "Unconfirmed ClientId %"PRIx64"->'%s': Principals do not match... unconfirmed addr=%s Return NFS4ERR_CLID_INUSE",
                       clientid, data->pworker->hostaddr_str, unconfirmed_addr);
            }

          dec_client_id_ref(punconf);

          res_CREATE_SESSION4.csr_status = NFS4ERR_CLID_INUSE;

          goto out;
        }
    }

  if(pconf != NULL)
    {
      if(isDebug(component) && pconf != NULL)
        display_clientid_name(pconf, str_client);

      /* First must match principal */
      if(!nfs_compare_clientcred(&pconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&pconf->cid_client_addr,
                       &data->pworker->hostaddr,
                       IGNORE_PORT))
        {
          if(isDebug(component))
            {
              char confirmed_addr[SOCK_NAME_MAX];

              sprint_sockip(&pconf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

              LogDebug(component,
                       "Confirmed ClientId %"PRIx64"->%s addr=%s: Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
                       clientid, str_client, data->pworker->hostaddr_str,
                       confirmed_addr);
            }

          /* Release our reference to the confirmed clientid. */
          dec_client_id_ref(pconf);

          res_CREATE_SESSION4.csr_status = NFS4ERR_CLID_INUSE;

          goto out;
        }

      /* In this case, the record was confirmed proceed with CREATE_SESSION */
    }

  /* We don't need to do any further principal checks, we can't have a confirmed
   * clientid record with a different principal than the unconfirmed record.
   */

  /* At this point, we need to try and create the session before we modify the
   * confirmed and/or unconfirmed clientid records.
   */

  /** @todo: BUGAZOMEU Gerer les parametres de secu */

  /* Check flags value (test CSESS15) */
  if(arg_CREATE_SESSION4.csa_flags > CREATE_SESSION4_FLAG_CONN_RDMA)
    {
      LogDebug(component,
               "Invalid create session flags %"PRIu32,
               arg_CREATE_SESSION4.csa_flags);

      dec_client_id_ref(pfound);

      res_CREATE_SESSION4.csr_status = NFS4ERR_INVAL;

      goto out;
    }

  /* Record session related information at the right place */
  nfs41_session = pool_alloc(nfs41_session_pool, NULL);

  if(nfs41_session == NULL)
    {
      LogDebug(component,
               "Could not allocate memory for a session");

      dec_client_id_ref(pfound);

      res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;

      goto out;
    }

  nfs41_session->clientid           = clientid;
  nfs41_session->pclientid_record   = pfound;
  nfs41_session->sequence           = arg_CREATE_SESSION4.csa_sequence;
  nfs41_session->session_flags      = CREATE_SESSION4_FLAG_CONN_BACK_CHAN;
  nfs41_session->fore_channel_attrs = arg_CREATE_SESSION4.csa_fore_chan_attrs;
  nfs41_session->back_channel_attrs = arg_CREATE_SESSION4.csa_back_chan_attrs;

  /* Take reference to clientid record */
  inc_client_id_ref(pfound);

  /* Set ca_maxrequests */
  nfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS;
  nfs41_session->fore_channel_attrs.ca_maxrequests = NFS41_NB_SLOTS;

  nfs41_Build_sessionid(&clientid, nfs41_session->session_id);

  res_CREATE_SESSION4ok.csr_sequence = nfs41_session->sequence;
  res_CREATE_SESSION4ok.csr_flags    = CREATE_SESSION4_FLAG_CONN_BACK_CHAN;

  /* return the input for wanting of something better (will change in
     later versions) */
  res_CREATE_SESSION4ok.csr_fore_chan_attrs
       = nfs41_session->fore_channel_attrs;
  res_CREATE_SESSION4ok.csr_back_chan_attrs
       = nfs41_session->back_channel_attrs;

  memcpy(res_CREATE_SESSION4ok.csr_sessionid,
         nfs41_session->session_id,
         NFS4_SESSIONID_SIZE);

  /* Create Session replay cache */
  data->pcached_res = &pfound->cid_create_session_slot.cached_result;
  pfound->cid_create_session_slot.cache_used = TRUE;

  LogDebug(component, "CREATE_SESSION replay=%p", data->pcached_res);

  if(!nfs41_Session_Set(nfs41_session->session_id, nfs41_session))
    {
      LogDebug(component,
               "Could not insert session into table");

      /* Decrement our reference to the clientid record and the one for the session */
      dec_client_id_ref(pfound);
      dec_client_id_ref(pfound);

      /* Free the memory for the session */
      pool_free(nfs41_session_pool, nfs41_session);

      res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;     /* Maybe a more precise status would be better */

      goto out;
    }

  /* Make sure we have a reference to the confirmed clientid record if any */
  if(pconf == NULL)
    {
      pconf = pclient_record->cr_pconfirmed_id;

      if(isDebug(component) && pconf != NULL)
        display_clientid_name(pconf, str_client);

      /* Need a reference to the confirmed record for below */
      if(pconf != NULL)
        inc_client_id_ref(pconf);
    }

  if(pconf != NULL && pconf->cid_clientid != clientid)
    {
      /* Old confirmed record - need to expire it */
      if(isDebug(component))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pconf, str);
          LogDebug(component,
                   "Expiring %s",
                   str);
        }

      /* Expire clientid and release our reference. */
      nfs_client_id_expire(pconf);

      dec_client_id_ref(pconf);

      pconf = NULL;
    }

  if(pconf != NULL)
    {
      /* At this point we are updating the confirmed clientid.
       * Update the confirmed record from the unconfirmed record.
       */
      LogDebug(component,
               "Updating clientid %"PRIx64"->%s cb_program=%u",
               pconf->cid_clientid,
               str_client,
               arg_CREATE_SESSION4.csa_cb_program);

      pconf->cid_cb.cid_program = arg_CREATE_SESSION4.csa_cb_program;

      if(punconf != NULL)
        {

          /* unhash the unconfirmed clientid record */
          remove_unconfirmed_client_id(punconf);

          /* Release our reference to the unconfirmed entry */
          dec_client_id_ref(punconf);
        }

      if(isDebug(component))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pconf, str);
          LogDebug(component,
                   "Updated %s",
                   str);
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(pconf);
    }
  else
    {
      /* This is a new clientid */
      if(isFullDebug(component))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);

          LogFullDebug(component,
                       "Confirming new %s",
                       str);
        }

      punconf->cid_cb.cid_program = arg_CREATE_SESSION4.csa_cb_program;

      rc = nfs_client_id_confirm(punconf, component);

      if(rc != CLIENT_ID_SUCCESS)
        {
          if(rc == CLIENT_ID_INVALID_ARGUMENT)
            res_CREATE_SESSION4.csr_status = NFS4ERR_SERVERFAULT;
          else
            res_CREATE_SESSION4.csr_status = NFS4ERR_RESOURCE;

          /* Need to destroy the session */
          if(!nfs41_Session_Del(nfs41_session->session_id))
            LogDebug(component,
                     "Oops nfs41_Session_Del failed");

          /* Release our reference to the unconfirmed record */
          dec_client_id_ref(punconf);

          goto out;
        }

      pconf   = punconf;
      punconf = NULL;

      if(isDebug(component))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pconf, str);

          LogDebug(component,
                   "Confirmed %s",
                   str);
        }
    }

  pconf->cid_create_session_sequence++;

  /* Release our reference to the confirmed record */
  dec_client_id_ref(pconf);

  if(isFullDebug(component))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_record(pclient_record, str);

      LogFullDebug(component,
                   "Client Record %s cr_pconfirmed_id=%p cr_punconfirmed_id=%p",
                   str,
                   pclient_record->cr_pconfirmed_id,
                   pclient_record->cr_punconfirmed_id);
    }

  LogDebug(component,
           "CREATE_SESSION success");

  /* Successful exit */
  res_CREATE_SESSION4.csr_status = NFS4_OK;

 out:

  V(pclient_record->cr_mutex);

  /* Release our reference to the client record and return */
  dec_client_record_ref(pclient_record);

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
