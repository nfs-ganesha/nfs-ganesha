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
 * \file  nfs4_op_setclientid_confirm.c
 * \brief Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * Routines used for managing the NFS4_OP_SETCLIENTID_CONFIRM operation.
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
#include "sal_functions.h"
#include "nfs_proto_functions.h"

/**
 *
 * @brief The NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * The NFS4_OP_SETCLIENTID_CONFIRM operation.
 *
 * @param[in]     op    nfs4_op arguments
 * @param[in,out] data  The compound request's data
 * @param[out]    resp  nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 * @see nfs4_Compound
 *
 */

int nfs4_op_setclientid_confirm(struct nfs_argop4 * op,
                                compound_data_t   * data,
                                struct nfs_resop4 * resp)
{
  nfs_client_id_t     * pconf   = NULL;
  nfs_client_id_t     * punconf = NULL;
  nfs_client_record_t * pclient_record;
  clientid4             clientid = 0;
  sockaddr_t            client_addr;
  char                  str_verifier[NFS4_VERIFIER_SIZE * 2 + 1];
  char                  str_client_addr[SOCK_NAME_MAX];
  char                  str_client[NFS4_OPAQUE_LIMIT * 2 + 1];
  int                   rc;

#define arg_SETCLIENTID_CONFIRM4 op->nfs_argop4_u.opsetclientid_confirm
#define res_SETCLIENTID_CONFIRM4 resp->nfs_resop4_u.opsetclientid_confirm

  resp->resop                     = NFS4_OP_SETCLIENTID_CONFIRM;
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
  clientid                        = arg_SETCLIENTID_CONFIRM4.clientid;

  copy_xprt_addr(&client_addr, data->reqp->rq_xprt);

  if(isDebug(COMPONENT_CLIENTID))
    {
      sprint_sockip(&client_addr, str_client_addr, sizeof(str_client_addr));

      sprint_mem(str_verifier,
                 arg_SETCLIENTID_CONFIRM4.setclientid_confirm,
                 NFS4_VERIFIER_SIZE);
    }

  LogDebug(COMPONENT_CLIENTID,
           "SETCLIENTID_CONFIRM client addr=%s clientid=%"PRIx64" setclientid_confirm=%s",
           str_client_addr,
           clientid,
           str_verifier);

  /* First try to look up unconfirmed record */
  rc = nfs_client_id_get_unconfirmed(clientid, &punconf);

  if(rc == CLIENT_ID_SUCCESS)
    {
      pclient_record = punconf->cid_client_record;

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Found %s",
                       str);
        }
    }
  else
    {
      rc = nfs_client_id_get_confirmed(clientid, &pconf);

      if(rc != CLIENT_ID_SUCCESS)
        {
          /* No record whatsoever of this clientid */
          LogDebug(COMPONENT_CLIENTID,
                   "Stale clientid = %"PRIx64,
                   clientid);
          res_SETCLIENTID_CONFIRM4.status = NFS4ERR_STALE_CLIENTID;

          return res_SETCLIENTID_CONFIRM4.status;
        }

      pclient_record = pconf->cid_client_record;

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pconf, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Found %s",
                       str);
        }
    }

  P(pclient_record->cr_mutex);

  inc_client_record_ref(pclient_record);

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

  /* At this point one and only one of pconf and punconf is non-NULL */

  if(punconf != NULL)
    {
      /* First must match principal */
      if(!nfs_compare_clientcred(&punconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&punconf->cid_client_addr, &client_addr, IGNORE_PORT))
        {
          if(isDebug(COMPONENT_CLIENTID))
            {
              char unconfirmed_addr[SOCK_NAME_MAX];

              sprint_sockip(&punconf->cid_client_addr, unconfirmed_addr, sizeof(unconfirmed_addr));

              LogDebug(COMPONENT_CLIENTID,
                       "Unconfirmed ClientId %"PRIx64"->'%s': Principals do not match... unconfirmed addr=%s Return NFS4ERR_CLID_INUSE",
                       clientid, str_client_addr, unconfirmed_addr);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4ERR_CLID_INUSE;

          dec_client_id_ref(punconf);

          goto out;
        }
      else if(punconf->cid_confirmed == CONFIRMED_CLIENT_ID &&
              memcmp(punconf->cid_verifier,
                     arg_SETCLIENTID_CONFIRM4.setclientid_confirm,
                     NFS4_VERIFIER_SIZE) == 0)
        {
          /* We must have raced with another SETCLIENTID_CONFIRM */
          if(isDebug(COMPONENT_CLIENTID))
            {
              char str[HASHTABLE_DISPLAY_STRLEN];

              display_client_id_rec(punconf, str);
              LogDebug(COMPONENT_CLIENTID,
                       "Race against confirm for %s",
                       str);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4_OK;

          dec_client_id_ref(punconf);

          goto out;
        }
      else if(punconf->cid_confirmed != UNCONFIRMED_CLIENT_ID)
        {
          /* We raced with another thread that dealt with this unconfirmed record.
           * Release our reference, and pretend we didn't find a record.
           */
          if(isDebug(COMPONENT_CLIENTID))
            {
              char str[HASHTABLE_DISPLAY_STRLEN];

              display_client_id_rec(punconf, str);

              LogDebug(COMPONENT_CLIENTID,
                       "Race against expire for %s",
                       str);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4ERR_STALE_CLIENTID;

          dec_client_id_ref(punconf);

          goto out;
        }
    }

  if(pconf != NULL)
    {
      if(isDebug(COMPONENT_CLIENTID) && pconf != NULL)
        display_clientid_name(pconf, str_client);

      /* First must match principal */
      if(!nfs_compare_clientcred(&pconf->cid_credential, &data->credential) ||
         !cmp_sockaddr(&pconf->cid_client_addr, &client_addr, IGNORE_PORT))
        {
          if(isDebug(COMPONENT_CLIENTID))
            {
              char confirmed_addr[SOCK_NAME_MAX];

              sprint_sockip(&pconf->cid_client_addr, confirmed_addr, sizeof(confirmed_addr));

              LogDebug(COMPONENT_CLIENTID,
                       "Confirmed ClientId %"PRIx64"->%s addr=%s: Principals do not match... confirmed addr=%s Return NFS4ERR_CLID_INUSE",
                       clientid, str_client, str_client_addr, confirmed_addr);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4ERR_CLID_INUSE;
        }
      else if(memcmp(pconf->cid_verifier,
                     arg_SETCLIENTID_CONFIRM4.setclientid_confirm,
                     NFS4_VERIFIER_SIZE) == 0)
        {
          /* In this case, the record was confirmed and we have received a retry */
          if(isDebug(COMPONENT_CLIENTID))
            {
              char str[HASHTABLE_DISPLAY_STRLEN];

              display_client_id_rec(pconf, str);
              LogDebug(COMPONENT_CLIENTID,
                       "Retry confirm for %s",
                       str);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4_OK;
        }
      else
        {
          /* This is a case not covered... Return NFS4ERR_CLID_INUSE */
          if(isDebug(COMPONENT_CLIENTID))
            {
              char str[HASHTABLE_DISPLAY_STRLEN];
              char str_conf_verifier[NFS4_VERIFIER_SIZE * 2 + 1];

              sprint_mem(str_conf_verifier,
                         pconf->cid_verifier,
                         NFS4_VERIFIER_SIZE);

              display_client_id_rec(pconf, str);

              LogDebug(COMPONENT_CLIENTID,
                       "Confirm verifier=%s doesn't match verifier=%s for %s",
                       str_conf_verifier, str_verifier, str);
            }

          res_SETCLIENTID_CONFIRM4.status = NFS4ERR_CLID_INUSE;
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(pconf);

      goto out;
    }

  /* We don't need to do any further principal checks, we can't have a confirmed
   * clientid record with a different principal than the unconfirmed record.
   * Also, at this point, we have a matching unconfirmed clientid
   * (punconf != NULL and pconf == NULL).
   */

  /* Make sure we have a reference to the confirmed clientid record if any */
  if(pconf == NULL)
    {
      pconf = pclient_record->cr_pconfirmed_id;

      if(isDebug(COMPONENT_CLIENTID) && pconf != NULL)
        display_clientid_name(pconf, str_client);

      /* Need a reference to the confirmed record for below */
      if(pconf != NULL)
        inc_client_id_ref(pconf);
    }

  if(pconf != NULL && pconf->cid_clientid != clientid)
    {
      /* Old confirmed record - need to expire it */
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

  if(pconf != NULL)
    {
      /* At this point we are updating the confirmed clientid.
       * Update the confirmed record from the unconfirmed record.
       */
      if(isFullDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Updating from %s",
                       str);
        }

      /* Copy callback information into confirmed clientid record */
      memcpy(pconf->cid_cb.cid_client_r_addr,
             punconf->cid_cb.cid_client_r_addr,
             sizeof(pconf->cid_cb.cid_client_r_addr));

      pconf->cid_cb.cid_addr    = punconf->cid_cb.cid_addr;
      pconf->cid_cb.cid_program = punconf->cid_cb.cid_program;

      pconf->cid_cb.cb_u.v40.cb_callback_ident =
        punconf->cid_cb.cb_u.v40.cb_callback_ident;

      nfs_rpc_destroy_chan(&pconf->cid_cb.cb_u.v40.cb_chan);

      memcpy(pconf->cid_verifier, punconf->cid_verifier, NFS4_VERIFIER_SIZE);

      /* unhash the unconfirmed clientid record */
      remove_unconfirmed_client_id(punconf);

      /* Release our reference to the unconfirmed entry */
      dec_client_id_ref(punconf);

      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pconf, str);
          LogDebug(COMPONENT_CLIENTID,
                   "Updated %s",
                   str);
        }

      /* Release our reference to the confirmed clientid. */
      dec_client_id_ref(pconf);
    }
  else
    {
      /* This is a new clientid */
      if(isFullDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);

          LogFullDebug(COMPONENT_CLIENTID,
                       "Confirming new %s",
                       str);
        }

      rc = nfs_client_id_confirm(punconf, COMPONENT_CLIENTID);

      if(rc != CLIENT_ID_SUCCESS)
        {
          if(rc == CLIENT_ID_INVALID_ARGUMENT)
            res_SETCLIENTID_CONFIRM4.status = NFS4ERR_SERVERFAULT;
          else
            res_SETCLIENTID_CONFIRM4.status = NFS4ERR_RESOURCE;

          LogEvent(COMPONENT_CLIENTID,
                   "FAILED to confirm client");


          /* Release our reference to the unconfirmed record */
          dec_client_id_ref(punconf);

          goto out;
        }

      /*
       * We have successfully added a new confirmed client id.  Now
       * add it to stable storage.
       */
      nfs4_create_clid_name(pclient_record, punconf, data->reqp);
      nfs4_add_clid(punconf);

      /* check if the client can perform reclaims */
      nfs4_chk_clid(punconf);

      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(punconf, str);

          LogDebug(COMPONENT_CLIENTID,
                   "Confirmed %s",
                   str);
        }

      /* Release our reference to the now confirmed record */
      dec_client_id_ref(punconf);
    }

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

  /* Successful exit */
  res_SETCLIENTID_CONFIRM4.status = NFS4_OK;

 out:

  V(pclient_record->cr_mutex);

  /* Release our reference to the client record and return */
  dec_client_record_ref(pclient_record);

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
