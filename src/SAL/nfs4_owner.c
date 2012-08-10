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
 */

/**
 * @brief The management of the NFS4 Owner cache.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "sal_functions.h"

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_nfs4_owner;

int display_nfs4_owner_key(hash_buffer_t * pbuff, char *str)
{
  return display_nfs4_owner((state_owner_t *) (pbuff->pdata), str);
}

int display_nfs4_owner(state_owner_t *powner, char *str)
{
  char         * strtmp = str;

  strtmp += sprintf(strtmp, "%s %p:",
                    state_owner_type_to_str(powner->so_type),
                    powner);

  strtmp += sprintf(strtmp, " clientid={");
  strtmp += display_client_id_rec(powner->so_owner.so_nfs4_owner.so_pclientid, strtmp);
  strtmp += sprintf(strtmp, "} owner=");

  strtmp += DisplayOpaqueValue(powner->so_owner_val,
                               powner->so_owner_len,
                               strtmp);

  strtmp += sprintf(strtmp, " confirmed=%u seqid=%u",
                    powner->so_owner.so_nfs4_owner.so_confirmed,
                    powner->so_owner.so_nfs4_owner.so_seqid);

  if(powner->so_owner.so_nfs4_owner.so_related_owner != NULL)
    {
      strtmp += sprintf(strtmp, " related_owner={");
      strtmp += display_nfs4_owner(powner->so_owner.so_nfs4_owner.so_related_owner, strtmp);
      strtmp += sprintf(strtmp, "}");
    }

  strtmp += sprintf(strtmp, " refcount=%d",
                    atomic_fetch_int32_t(&powner->so_refcount));

  return strtmp - str;
}

int display_nfs4_owner_val(hash_buffer_t * pbuff, char *str)
{
  return display_nfs4_owner((state_owner_t *) (pbuff->pdata), str);
}

int compare_nfs4_owner(state_owner_t * powner1,
                       state_owner_t * powner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner(powner1, str1);
      display_nfs4_owner(powner2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(powner1 == NULL || powner2 == NULL)
    return 1;

  if(powner1 == powner2)
    return 0;

  if(powner1->so_type == STATE_LOCK_OWNER_NFSV4 &&
     powner2->so_type == STATE_OPEN_OWNER_NFSV4)
    return compare_nfs4_owner(powner1->so_owner.so_nfs4_owner.so_related_owner,
                              powner2);

  if(powner2->so_type == STATE_LOCK_OWNER_NFSV4 &&
     powner1->so_type == STATE_OPEN_OWNER_NFSV4)
    return compare_nfs4_owner(powner2->so_owner.so_nfs4_owner.so_related_owner,
                              powner1);

  if(powner1->so_type != powner2->so_type)
    return 1;

  if(powner1->so_owner.so_nfs4_owner.so_clientid !=
     powner2->so_owner.so_nfs4_owner.so_clientid)
    return 1;

  if(powner1->so_owner_len !=
     powner2->so_owner_len)
    return 1;

  return memcmp(powner1->so_owner_val,
                powner2->so_owner_val,
                powner1->so_owner_len);
}

int compare_nfs4_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  state_owner_t *pkey1 = buff1->pdata;
  state_owner_t *pkey2 = buff2->pdata;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(pkey1, str1);
      DisplayOwner(pkey2, str2);
      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_STATE,
                     "{%s} vs {%s}", str1, str2);
    }

  if(pkey1 == NULL || pkey2 == NULL)
    return 1;

  if(pkey1->so_type != pkey2->so_type)
    return 1;

  return compare_nfs4_owner(pkey1, pkey2);
}                               /* compare_nfs4_owner */

uint32_t nfs4_owner_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  uint32_t res = 0;

  state_owner_t * pkey = buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    {
      c = ((char *)pkey->so_owner_val)[i];
      sum += c;
    }

  res = ((uint32_t) pkey->so_owner.so_nfs4_owner.so_clientid +
         (uint32_t) sum + pkey->so_owner_len +
         (uint32_t) pkey->so_type) % (uint32_t) p_hparam->index_size;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %"PRIu32, res);

  return res;

}                               /* nfs4_owner_value_hash_func */

uint64_t nfs4_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef)
{
  state_owner_t * pkey = buffclef->pdata;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  uint64_t res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    {
      c = ((char *)pkey->so_owner_val)[i];
      sum += c;
    }

  res = (uint64_t) pkey->so_owner.so_nfs4_owner.so_clientid +
        (uint64_t) sum + pkey->so_owner_len +
        (uint64_t) pkey->so_type;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %"PRIu64, res);

  return res;
}                               /* state_id_rbt_hash_func */

void free_nfs4_owner(state_owner_t * powner)
{
  if(powner->so_owner.so_nfs4_owner.so_related_owner != NULL)
    dec_state_owner_ref(powner->so_owner.so_nfs4_owner.so_related_owner);

  /* Release the saved response. */
  nfs4_Compound_FreeOne(&powner->so_owner.so_nfs4_owner.so_resp);

  /* Remove the owner from the owners per clientid list. */
  P(powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);

  glist_del(&powner->so_owner.so_nfs4_owner.so_perclient);

  V(powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);

  dec_client_id_ref(powner->so_owner.so_nfs4_owner.so_pclientid);
}

/**
 *
 * nfs4_Init_nfs4_owner: Init the hashtable for NFS Open Owner cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 *
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int Init_nfs4_owner(nfs4_owner_parameter_t param)
{

  if((ht_nfs4_owner = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NFS Open Owner cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_nfs4_owner */

static void init_nfs4_owner(state_owner_t * powner)
{
  init_glist(&powner->so_owner.so_nfs4_owner.so_state_list);

  /* Increment refcount on related owner */
  if(powner->so_owner.so_nfs4_owner.so_related_owner != NULL)
    inc_state_owner_ref(powner->so_owner.so_nfs4_owner.so_related_owner);

  /* Increment reference count for clientid record */
  inc_client_id_ref(powner->so_owner.so_nfs4_owner.so_pclientid);

  P(powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);

  if (powner->so_type == STATE_OPEN_OWNER_NFSV4)
    {
      /* If open owner, add to clientid lock owner list */
      glist_add_tail(&powner->so_owner.so_nfs4_owner.so_pclientid->cid_openowners,
                     &powner->so_owner.so_nfs4_owner.so_perclient);
    }
  else if(powner->so_type == STATE_LOCK_OWNER_NFSV4)
    {
      /* If lock owner, add to clientid open owner list */
      glist_add_tail(&powner->so_owner.so_nfs4_owner.so_pclientid->cid_lockowners,
                     &powner->so_owner.so_nfs4_owner.so_perclient);
    }

  V(powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);
}

/**
 *
 *  nfs4_owner_PrintAll
 *
 * This routine displays the content of the hashtable used to store the open owners. 
 *
 * @return nothing (void function)
 */
void nfs4_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_nfs4_owner);
}                               /* nfs4_owner_PrintAll */

state_owner_t *create_nfs4_owner(state_nfs4_owner_name_t * pname,
                                 nfs_client_id_t         * pclientid,
                                 state_owner_type_t        type,
                                 state_owner_t           * related_owner,
                                 unsigned int              init_seqid,
                                 bool_t                  * pisnew,
                                 care_t                    care)
{
  state_owner_t   key;
  state_owner_t * powner;
  bool_t          isnew;

  /* set up the content of the open_owner */
  memset(&key, 0, sizeof(key));

  key.so_type                                 = type;
  key.so_owner.so_nfs4_owner.so_seqid         = init_seqid;
  key.so_owner.so_nfs4_owner.so_related_owner = related_owner;
  key.so_owner.so_nfs4_owner.so_clientid      = pclientid->cid_clientid;
  key.so_owner.so_nfs4_owner.so_pclientid     = pclientid;
  key.so_owner_len                            = pname->son_owner_len;
  key.so_owner_val                            = pname->son_owner_val;
  key.so_owner.so_nfs4_owner.so_resp.resop    = NFS4_OP_ILLEGAL;
  key.so_owner.so_nfs4_owner.so_args.argop    = NFS4_OP_ILLEGAL;
  key.so_refcount                             = 1;
#if 0
  /* WAITING FOR COMMUNITY FIX */
  /* setting lock owner confirmed */
  if ( type == STATE_LOCK_OWNER_NFSV4)
    key.so_owner.so_nfs4_owner.so_confirmed   = 1;
#endif

  powner = get_state_owner(care, &key, init_nfs4_owner, &isnew);

  if(powner != NULL && related_owner != NULL)
    {
      P(powner->so_mutex);
      /* Related owner already exists. */
      if(powner->so_owner.so_nfs4_owner.so_related_owner == NULL)
        {
          /* Attach related owner to owner now that we know it. */
          inc_state_owner_ref(related_owner);
          powner->so_owner.so_nfs4_owner.so_related_owner = related_owner;
        }
      else if(powner->so_owner.so_nfs4_owner.so_related_owner != related_owner)
        {
          char str1[HASHTABLE_DISPLAY_STRLEN];
          char str2[HASHTABLE_DISPLAY_STRLEN];

          DisplayOwner(related_owner, str1);
          DisplayOwner(powner, str1);

          LogCrit(COMPONENT_NFS_V4_LOCK,
                  "Related {%s} doesn't match for {%s}",
                  str1, str2);
          V(powner->so_mutex);

          /* Release the reference to the owner. */
          dec_state_owner_ref(powner);
 
          return NULL;
        }
      V(powner->so_mutex);
    }

  if(!isnew && powner != NULL && pisnew != NULL)
    {
      char str[HASHTABLE_DISPLAY_STRLEN];
      DisplayOwner(powner, str);
      LogDebug(COMPONENT_STATE,
               "Previously known owner {%s} is being reused",
               str);
    }

  if(pisnew != NULL)
    *pisnew = isnew;

  return powner;
}

void Process_nfs4_conflict(LOCK4denied          * denied,    /* NFS v4 LOck4denied structure to fill in */
                           state_owner_t        * holder,    /* owner that holds conflicting lock */
                           fsal_lock_param_t    * conflict)  /* description of conflicting lock */
{
  /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
  denied->offset = conflict->lock_start;
  denied->length = conflict->lock_length;

  if(conflict->lock_type == FSAL_LOCK_R)
    denied->locktype = READ_LT;
  else
    denied->locktype = WRITE_LT;

  if(holder != NULL && holder->so_owner_len != 0)
    denied->owner.owner.owner_val = gsh_malloc(holder->so_owner_len);
  else
    denied->owner.owner.owner_val = NULL;

  LogFullDebug(COMPONENT_STATE,
               "denied->owner.owner.owner_val = %p",
               denied->owner.owner.owner_val);

  if(denied->owner.owner.owner_val != NULL)
    {
      denied->owner.owner.owner_len = holder->so_owner_len;

      memcpy(denied->owner.owner.owner_val,
             holder->so_owner_val,
             holder->so_owner_len);
    }
  else
    {
      denied->owner.owner.owner_len = unknown_owner.so_owner_len;
      denied->owner.owner.owner_val = unknown_owner.so_owner_val;
    }

  if(holder != NULL && holder->so_type == STATE_LOCK_OWNER_NFSV4)
    denied->owner.clientid = holder->so_owner.so_nfs4_owner.so_clientid;
  else
    denied->owner.clientid = 0;

  /* Release any lock owner reference passed back from SAL */
  if(holder != NULL)
    dec_state_owner_ref(holder);
}

void Release_nfs4_denied(LOCK4denied * denied)
{
  if(denied->owner.owner.owner_val != unknown_owner.so_owner_val &&
     denied->owner.owner.owner_val != NULL)
    gsh_free(denied->owner.owner.owner_val);
}

void Copy_nfs4_denied(LOCK4denied * denied_dst, LOCK4denied * denied_src)
{
  memcpy(denied_dst, denied_src, sizeof(*denied_dst));

  if(denied_src->owner.owner.owner_val != unknown_owner.so_owner_val &&
     denied_src->owner.owner.owner_val != NULL)
    {
      denied_dst->owner.owner.owner_val
        = gsh_malloc(denied_src->owner.owner.owner_len);
      LogFullDebug(COMPONENT_STATE,
                   "denied_dst->owner.owner.owner_val = %p",
                   denied_dst->owner.owner.owner_val);
    }

  if(denied_dst->owner.owner.owner_val == NULL)
    {
      denied_dst->owner.owner.owner_len = unknown_owner.so_owner_len;
      denied_dst->owner.owner.owner_val = unknown_owner.so_owner_val;
    }
}

void Copy_nfs4_state_req(state_owner_t   * powner,
                         seqid4            seqid,
                         nfs_argop4      * args,
                         compound_data_t * data,
                         nfs_resop4      * resp,
                         const char      * tag)
{
  /* Simplify use of this function when we may not be keeping any data for the
   * state owner
   */
  if(powner == NULL)
    return;

  LogFullDebug(COMPONENT_STATE,
               "%s: saving response %p so_seqid %u new seqid %u",
               tag, powner, powner->so_owner.so_nfs4_owner.so_seqid, seqid);

  /* Free previous response */
  nfs4_Compound_FreeOne(&powner->so_owner.so_nfs4_owner.so_resp);

  /* Copy new response */
  nfs4_Compound_CopyResOne(&powner->so_owner.so_nfs4_owner.so_resp, resp);

  /* Deep copy OPEN args? */
  if(powner->so_owner.so_nfs4_owner.so_args.argop == NFS4_OP_OPEN)
    {
    }

  /* Copy bnew args */
  memcpy(&powner->so_owner.so_nfs4_owner.so_args,
         args,
         sizeof(powner->so_owner.so_nfs4_owner.so_args));

  /* Copy new file */
  powner->so_owner.so_nfs4_owner.so_last_pentry = data->current_entry;

  /* Deep copy OPEN args? */
  if(args->argop == NFS4_OP_OPEN)
    {
    }

  /* Store new seqid */
  powner->so_owner.so_nfs4_owner.so_seqid = seqid;
}

/**
 *
 * Check_nfs4_seqid: Check NFS4 request for valid seqid for replay, next request, or BAD_SEQID.
 *
 * Returns TRUE if the request is the next seqid.
 * If the request is a replay, copies the saved response and returns FALSE.
 * Otherwise, sets status to NFS4ERR_BAD_SEQID and returns FALSE.
 *
 * In either case, on a FALSE return, the caller should send the resulting response back to the client.
 *
 */
bool_t Check_nfs4_seqid(state_owner_t   * powner,
                        seqid4            seqid,
                        nfs_argop4      * args,
                        compound_data_t * data,
                        nfs_resop4      * resp,
                        const char      * tag)
{
  seqid4 next;

  /* Check if any owner to verify seqid against */
  if(powner == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
               "%s: Unknown owner doesn't have saved seqid, req seqid %u",
               tag, seqid);
      return TRUE;
    }

  /* If this is a new state owner, client may start with any seqid */
  if(powner->so_owner.so_nfs4_owner.so_last_pentry == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
               "%s: New owner %p doesn't have saved seqid, req seqid %u",
               tag, powner, seqid);
      return TRUE;
    }

  /* Check for valid next seqid */
  next = powner->so_owner.so_nfs4_owner.so_seqid + 1;

  LogFullDebug(COMPONENT_STATE,
               "%s: Check powner %p so_seqid %u next %u req seqid %u",
               tag, powner, powner->so_owner.so_nfs4_owner.so_seqid, next, seqid);

  if(seqid == next)
    return TRUE;

  /* All NFS4 responses have the status in the same place, so use any to set NFS4ERR_BAD_SEQID */
  resp->nfs_resop4_u.oplock.status = NFS4ERR_BAD_SEQID;

  /* Now check for valid replay */
  if(powner->so_owner.so_nfs4_owner.so_seqid != seqid)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid %u in request (not replay), expected seqid %u, returning NFS4ERR_BAD_SEQID",
               tag, seqid, powner->so_owner.so_nfs4_owner.so_seqid);
      return FALSE;
    }

  if(args->argop != powner->so_owner.so_nfs4_owner.so_args.argop)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid in request %u (not replay - not same op), returning NFS4ERR_BAD_SEQID",
               tag, seqid);
      return FALSE;
    }

  if(powner->so_owner.so_nfs4_owner.so_last_pentry != data->current_entry)
    {
      LogDebug(COMPONENT_STATE,
               "%s: Invalid seqid in request %u (not replay - wrong file), returning NFS4ERR_BAD_SEQID",
               tag, seqid);
      return FALSE;
    }

  // TODO FSF: add more checks here...

  LogDebug(COMPONENT_STATE,
           "%s: Copying saved response for seqid %u",
           tag, seqid);

  /* Copy the saved response and tell caller to use it */
  nfs4_Compound_CopyResOne(resp, &powner->so_owner.so_nfs4_owner.so_resp);

  return FALSE;
}

/** @todo FSF: I'm not really sure about the refcounting here... */
state_status_t get_clientid_owner(clientid4 clientid,
                                  state_owner_t **clientid_owner)
{
  /* Pointer to client record, retrieved by ID and in which the
     client state owner is stored */
  nfs_client_id_t *pclientid = NULL;
  /* Return code for error checking */
  int rc = 0;

  if ((rc = nfs_client_id_get_confirmed(clientid, &pclientid))
      != CLIENT_ID_SUCCESS)
    {
      return STATE_NOT_FOUND;
    }
  else
    {
      *clientid_owner = &pclientid->cid_owner;
      dec_client_id_ref(pclientid);
      return STATE_SUCCESS;
    }
}
