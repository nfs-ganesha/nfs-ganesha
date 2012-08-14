/**
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software F oundation; either
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
 *
 * nfs4_client_id.c : The management of the client id cache.
 *
 * $Header$
 *
 * $Log$
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <assert.h>
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "nfs4.h"
#include "sal_data.h"
#include "sal_functions.h"
#include "cache_inode_lru.h"
#include "abstract_atomic.h"

/* Hashtables used to cache NFSv4 clientids */
hash_table_t    * ht_client_record;
hash_table_t    * ht_confirmed_client_id;
hash_table_t    * ht_unconfirmed_client_id;
pthread_mutex_t   clientid_mutex = PTHREAD_MUTEX_INITIALIZER;
uint32_t          clientid_counter;
uint64_t          clientid_verifier;
pool_t          * client_id_pool;
pool_t          * client_record_pool;

const char * clientid_confirm_state_to_str(nfs_clientid_confirm_state_t confirmed)
{
  switch(confirmed)
    {
      case CONFIRMED_CLIENT_ID:       return "CONFIRMED";
      case UNCONFIRMED_CLIENT_ID:     return "UNCONFIRMED";
      case EXPIRED_CLIENT_ID:         return "EXPIRED";
    }
  return "UNKNOWN STATE";
}

int display_client_id_rec(nfs_client_id_t * pclientid, char * str)
{
  int    delta;
  char * tmpstr = str;

  tmpstr += sprintf(tmpstr, "%p ClientID=%"PRIx64" %s Client={",
                    pclientid,
                    pclientid->cid_clientid,
                    clientid_confirm_state_to_str(pclientid->cid_confirmed));

  if(pclientid->cid_client_record != NULL)
    tmpstr += display_client_record(pclientid->cid_client_record, tmpstr);
  else
    tmpstr += sprintf(tmpstr, "<NULL>");

  if(pclientid->cid_lease_reservations > 0)
    delta = 0;
  else
    delta = time(NULL) - pclientid->cid_last_renew;

  tmpstr += sprintf(tmpstr, "} cb_prog=%u r_addr=%s r_netid=%s t_delta=%d reservations=%d refcount=%"PRId32,
                    pclientid->cid_cb.cid_program,
                    pclientid->cid_cb.cid_client_r_addr,
                    netid_nc_table[pclientid->cid_cb.cid_addr.nc].netid,
                    delta,
                    pclientid->cid_lease_reservations,
                    atomic_fetch_int32_t(&pclientid->cid_refcount));

  return tmpstr - str;
}

int display_clientid_name(nfs_client_id_t * pclientid, char * str)
{
  if(pclientid->cid_client_record != NULL)
    return DisplayOpaqueValue(pclientid->cid_client_record->cr_client_val,
                              pclientid->cid_client_record->cr_client_val_len,
                              str);
  else
    return sprintf(str, "<NULL>");
}

static void Hash_inc_client_id_ref(hash_buffer_t *buffval)
{
  nfs_client_id_t *pclientid = buffval->pdata;

  inc_client_id_ref(pclientid);
}

void inc_client_id_ref(nfs_client_id_t *pclientid)
{
  int32_t cid_refcount = atomic_inc_int32_t(&pclientid->cid_refcount);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_id_rec(pclientid, str);
      LogFullDebug(COMPONENT_CLIENTID,
                   "Increment refcount Clientid {%s} to %"PRId32,
                   str, cid_refcount);
    }
}

void free_client_id(nfs_client_id_t *pclientid)
{
  assert(atomic_fetch_int32_t(&pclientid->cid_refcount) == 0);

  if(pclientid->cid_client_record != NULL)
    dec_client_record_ref(pclientid->cid_client_record);

  if(pthread_mutex_destroy(&pclientid->cid_mutex) != 0)
    LogDebug(COMPONENT_CLIENTID,
             "pthread_mutex_destroy returned errno %d (%s)",
             errno, strerror(errno));

  pool_free(client_id_pool, pclientid);
}

void dec_client_id_ref(nfs_client_id_t *pclientid)
{
  char           str[HASHTABLE_DISPLAY_STRLEN];
  int32_t        cid_refcount;

  if(isFullDebug(COMPONENT_CLIENTID))
    display_client_id_rec(pclientid, str);

  cid_refcount = atomic_dec_int32_t(&pclientid->cid_refcount);

  LogFullDebug(COMPONENT_CLIENTID,
               "Decrement refcount Clientid {%s} refcount to %"PRId32,
               str, cid_refcount);

  if(cid_refcount > 0)
    return;

  /* We don't need a lock to look at cid_confirmed because when refcount has
   * gone to 0, no other threads can have a pointer to the clientid record.
   */
  if(pclientid->cid_confirmed == EXPIRED_CLIENT_ID)
    {
      /* Is not in any hash table, so we can just delete it */
      LogFullDebug(COMPONENT_CLIENTID,
                   "Free Clientid refcount now=0 {%s}",
                   str);

      free_client_id(pclientid);
    }
  else
    {
      /* Clientid records should not be freed unless marked expired. */
      display_client_id_rec(pclientid, str);

      LogCrit(COMPONENT_CLIENTID,
              "Should not be here, try to remove last ref {%s}",
              str);

      assert(pclientid->cid_confirmed == EXPIRED_CLIENT_ID);
    }
}

/**
 *
 *  client_id_value_hash_func: computes the hash value for the entry in Client Id cache.
 *
 * Computes the hash value for the entry in Client Id cache. In fact,
 * it just use addresse as value (identity function) modulo the size
 * of the hash.  This function is called internal in the HasTable_*
 * function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
uint32_t client_id_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t    * buffclef)
{
  clientid4 clientid;

  memcpy(&clientid, buffclef->pdata, sizeof(clientid));

  return (uint32_t) clientid % p_hparam->index_size;
}                               /*  client_id_value_hash_func */

/**
 *
 *  client_id_rbt_hash_func: computes the rbt value for the entry in Client Id cache.
 *
 * Computes the rbt value for the entry in Client Id cache. In fact, it just use the address value
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
uint64_t client_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                 hash_buffer_t    * buffclef)
{
  clientid4 clientid;

  memcpy(&clientid, buffclef->pdata, sizeof(clientid));

  return clientid;
}                               /* client_id_rbt_hash_func */

/**
 *
 * compare_client_id: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in
 * the hashtable storing the client ids.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_client_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  clientid4 cl1 = *((clientid4 *) (buff1->pdata));
  clientid4 cl2 = *((clientid4 *) (buff2->pdata));
  return (cl1 == cl2) ? 0 : 1;
}

/**
 *
 * display_client_id_key: displays the client_id stored in the buffer.
 *
 * displays the client_id stored in the buffer. This function is to be used as 'key_to_str' field in
 * the hashtable storing the client ids.
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_client_id_key(hash_buffer_t * pbuff, char *str)
{
  clientid4 clientid;

  clientid = *((clientid4 *) (pbuff->pdata));

  return sprintf(str, "%"PRIx64, clientid);
}

int display_client_id_val(hash_buffer_t * pbuff, char *str)
{
  return display_client_id_rec(pbuff->pdata, str);
}

nfs_client_id_t * create_client_id(clientid4              clientid,
                                   nfs_client_record_t  * pclient_record,
                                   sockaddr_t           * pclient_addr,
                                   nfs_client_cred_t    * pcredential)
{
  nfs_client_id_t * pclientid = pool_alloc(client_id_pool, NULL);
  state_owner_t   * powner;

  if(pclientid == NULL)
    {
      LogCrit(COMPONENT_CLIENTID,
              "Unable to allocate memory for clientid %"PRIx64,
              clientid);
      return NULL;
    }

  if(pthread_mutex_init(&pclientid->cid_mutex, NULL) == -1)
    {
      char str_client[NFS4_OPAQUE_LIMIT * 2 + 1];

      display_clientid_name(pclientid, str_client);

      LogCrit(COMPONENT_CLIENTID,
               "Could not init mutex for clientid %"PRIx64"->%s",
               clientid, str_client);

      /* Directly free the clientid record since we failed to initialize it */
      pool_free(client_id_pool, pclientid);

      return NULL;
    }

  powner = &pclientid->cid_owner;

  if(pthread_mutex_init(&powner->so_mutex, NULL) == -1)
    {
      LogDebug(COMPONENT_CLIENTID,
               "Unable to create clientid owner for clientid %"PRIx64,
               clientid);

      /* Directly free the clientid record since we failed to initialize it */
      pool_free(client_id_pool, pclientid);

      return NULL;
    }

  if(clientid == 0)
    clientid = new_clientid();

  pclientid->cid_confirmed     = UNCONFIRMED_CLIENT_ID;
  pclientid->cid_clientid      = clientid;
  pclientid->cid_last_renew    = time(NULL);
  pclientid->cid_client_record = pclient_record;
  pclientid->cid_client_addr   = *pclient_addr;
  pclientid->cid_credential    = *pcredential;

  /* need to init the list_head */
  init_glist(&pclientid->cid_openowners);
  init_glist(&pclientid->cid_lockowners);

  /* set up the content of the clientid_owner */
  powner->so_type                              = STATE_CLIENTID_OWNER_NFSV4;
  powner->so_owner.so_nfs4_owner.so_clientid   = clientid;
  powner->so_owner.so_nfs4_owner.so_pclientid  = pclientid;
  powner->so_owner.so_nfs4_owner.so_resp.resop = NFS4_OP_ILLEGAL;
  powner->so_owner.so_nfs4_owner.so_args.argop = NFS4_OP_ILLEGAL;
  powner->so_refcount                          = 1;

  /* Init the lists for the clientid_owner */
  init_glist(&powner->so_lock_list);
  init_glist(&powner->so_owner.so_nfs4_owner.so_state_list);

  /* Get a reference to the client record */
  inc_client_record_ref(pclientid->cid_client_record);

  return pclientid;
}

/**
 *
 * nfs_client_id_insert: Inserts an entry describing a clientid4 into the cache.
 *
 * Inserts an entry describing a clientid4 into the cache.
 *
 * @param pclientid     [IN]    the client id record
 *
 * @return CLIENT_ID_SUCCESS if successfull\n.
 * @return CLIENT_ID_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return CLIENT_ID_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */
int nfs_client_id_insert(nfs_client_id_t * pclientid)
{
  hash_buffer_t             buffkey;
  hash_buffer_t             buffdata;
  int                       rc;

  /* Create key from cid_clientid */
  buffkey.pdata = &pclientid->cid_clientid;
  buffkey.len   = sizeof(pclientid->cid_clientid);

  buffdata.pdata = (caddr_t) pclientid;
  buffdata.len   = sizeof(nfs_client_id_t);

  rc = HashTable_Test_And_Set(ht_unconfirmed_client_id,
                              &buffkey,
                              &buffdata,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_CLIENTID,
               "Could not insert unconfirmed clientid %"PRIx64" error=%s",
               pclientid->cid_clientid,
               hash_table_err_to_str(rc));

      /* Free the clientid record and return */
      free_client_id(pclientid);

      return CLIENT_ID_INSERT_MALLOC_ERROR;
    }

  /* Take a reference to the unconfirmed clientid for the hash table. */
  inc_client_id_ref(pclientid);

  if(isFullDebug(COMPONENT_CLIENTID) && isFullDebug(COMPONENT_HASHTABLE))
    {
      LogFullDebug(COMPONENT_CLIENTID,
                   "-=-=-=-=-=-=-=-=-=-> ht_unconfirmed_client_id ");
      HashTable_Log(COMPONENT_CLIENTID, ht_unconfirmed_client_id);
    }

  /* Attach new clientid to client record's cr_punconfirmed_id. */
  pclientid->cid_client_record->cr_punconfirmed_id = pclientid;

  return CLIENT_ID_SUCCESS;
}                               /* nfs_client_id_insert */

/**
 *
 * remove_unconfirmed_client_id: Removes an unconfirmed client id record.
 *
 * Removes an unconfirmed client id record.
 *
 * @param pclientid     [IN]    the client id record
 *
 * @return hash table error code
 *
 */
int remove_unconfirmed_client_id(nfs_client_id_t * pclientid)
{
  int                       rc;
  hash_buffer_t             buffkey;
  hash_buffer_t             old_key;
  hash_buffer_t             old_value;

  buffkey.pdata = (caddr_t) &pclientid->cid_clientid;
  buffkey.len   = sizeof(pclientid->cid_clientid);

  rc = HashTable_Del(ht_unconfirmed_client_id,
                     &buffkey,
                     &old_key,
                     &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      LogCrit(COMPONENT_CLIENTID,
              "Could not remove unconfirmed clientid %"PRIx64" error=%s",
              pclientid->cid_clientid,
              hash_table_err_to_str(rc));
      return rc;
    }

  pclientid->cid_client_record->cr_punconfirmed_id = NULL;

  /* Set this up so this client id record will be freed. */
  pclientid->cid_confirmed = EXPIRED_CLIENT_ID;

  /* Release hash table reference to the unconfirmed record */
  dec_client_id_ref(pclientid);

  return rc;
}

/**
 *
 * nfs_client_id_confirm: Confirms a client id record.
 *
 * Confirms a client id record.
 *
 * @param pclientid     [IN]    the client id record
 *
 * @return CLIENT_ID_SUCCESS if successfull.
 * @return CLIENT_ID_INVALID_ARGUMENT if unable to find record in unconfirmed table
 * @return CLIENT_ID_INSERT_MALLOC_ERROR if unable to insert record into confirmed table
 * @return CLIENT_ID_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */
int nfs_client_id_confirm(nfs_client_id_t * pclientid,
                          log_components_t  component)
{
  int                       rc;
  hash_buffer_t             buffkey;
  hash_buffer_t             old_key;
  hash_buffer_t             old_value;

  buffkey.pdata = (caddr_t) &pclientid->cid_clientid;
  buffkey.len   = sizeof(pclientid->cid_clientid);

  /* Remove the clientid as the unconfirmed entry for the client record */
  pclientid->cid_client_record->cr_punconfirmed_id = NULL;

  rc = HashTable_Del(ht_unconfirmed_client_id,
                     &buffkey,
                     &old_key,
                     &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pclientid, str);

          LogCrit(COMPONENT_CLIENTID,
                  "Unexpected problem %s, could not remove {%s}",
                  hash_table_err_to_str(rc), str);
        }

      return CLIENT_ID_INVALID_ARGUMENT;
    }

  pclientid->cid_confirmed  = CONFIRMED_CLIENT_ID;

  rc = HashTable_Test_And_Set(ht_confirmed_client_id,
                              &old_key,
                              &old_value,
                              HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(isDebug(COMPONENT_CLIENTID))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_client_id_rec(pclientid, str);

          LogCrit(COMPONENT_CLIENTID,
                  "Unexpected problem %s, could not insert {%s}",
                  hash_table_err_to_str(rc), str);
        }

      /* Set this up so this client id record will be freed. */
      pclientid->cid_confirmed = EXPIRED_CLIENT_ID;

      /* Release hash table reference to the unconfirmed record */
      dec_client_id_ref(pclientid);

      return CLIENT_ID_INSERT_MALLOC_ERROR;
    }

  /* Add the clientid as the confirmed entry for the client record */
  pclientid->cid_client_record->cr_pconfirmed_id = pclientid;

  return CLIENT_ID_SUCCESS;
}

/**
 *
 * nfs_client_id_expire: client expires, need to take care of owners
 *
 * assumes caller holds precord->cr_mutex and holds a reference to precord also.
 *
 * @param pclientid [IN]    the client id used to expire
 *
 */
int nfs_client_id_expire(nfs_client_id_t * pclientid)
{
  struct glist_head    * glist, * glistn;
  struct glist_head    * glist2, * glistn2;
  state_status_t         pstatus;
  int                    rc;
  hash_buffer_t          buffkey;
  hash_buffer_t          old_key;
  hash_buffer_t          old_value;
  hash_table_t         * ht_expire;
  nfs_client_record_t  * precord;
  char                   str[HASHTABLE_DISPLAY_STRLEN];

  P(pclientid->cid_mutex);
  if (pclientid->cid_confirmed == EXPIRED_CLIENT_ID)
    {
      if(isFullDebug(COMPONENT_CLIENTID))
        {
          display_client_id_rec(pclientid, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Expired (skipped) {%s}", str);
        }

      V(pclientid->cid_mutex);
      return FALSE;
    }

  if(isDebug(COMPONENT_CLIENTID))
    {
      display_client_id_rec(pclientid, str);
      LogDebug(COMPONENT_CLIENTID,
               "Expiring {%s}", str);
    }

  if(pclientid->cid_confirmed == CONFIRMED_CLIENT_ID)
    ht_expire = ht_confirmed_client_id;
  else
    ht_expire = ht_unconfirmed_client_id;

  pclientid->cid_confirmed = EXPIRED_CLIENT_ID;

  /* Need to clean up the client record. */
  precord = pclientid->cid_client_record;

  V(pclientid->cid_mutex);

  /* Detach the clientid record from the client record */
  if(precord->cr_pconfirmed_id == pclientid)
    precord->cr_pconfirmed_id = NULL;

  if(precord->cr_punconfirmed_id == pclientid)
    precord->cr_punconfirmed_id = NULL;

  buffkey.pdata = (caddr_t) &pclientid->cid_clientid;
  buffkey.len   = sizeof(pclientid->cid_clientid);

  rc = HashTable_Del(ht_expire,
                     &buffkey,
                     &old_key,
                     &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      LogCrit(COMPONENT_CLIENTID,
              "Could not remove expired clientid %"PRIx64" error=%s",
              pclientid->cid_clientid,
              hash_table_err_to_str(rc));
      assert(rc == HASHTABLE_SUCCESS);
    }

  /* traverse the client's lock owners, and release all locks */
  glist_for_each_safe(glist, glistn, &pclientid->cid_lockowners)
    {
      state_owner_t * plock_owner = glist_entry(glist,
                                                state_owner_t,
                                	        so_owner.so_nfs4_owner.so_perclient);

      glist_for_each_safe(glist2, glistn2, &plock_owner->so_owner.so_nfs4_owner.so_state_list)
        {
          fsal_op_context_t        fsal_context;
          fsal_status_t            fsal_status;

          state_t* plock_state = glist_entry(glist2,
                                             state_t,
					     state_owner_list);

          /* construct the fsal context based on the export and root credential */
	  fsal_status = FSAL_GetClientContext(&fsal_context,
                                      &plock_state->state_pexport->FS_export_context,
                                      0,
                                      0,
                                      NULL,
                                      0);
          if(FSAL_IS_ERROR(fsal_status))
            {
              /* log error here , and continue? */
              LogCrit(COMPONENT_CLIENTID,
                      "FSAL_GetClientConext failed");
              continue;
            }

          state_owner_unlock_all(&fsal_context,
                                 plock_owner,
                                 plock_state,
                                 &pstatus);
        }
    }

  /* traverse the client's lock owners, and release all locks states and owners */
  glist_for_each_safe(glist, glistn, &pclientid->cid_lockowners)
    {
      state_owner_t * plock_owner = glist_entry(glist,
                                          state_owner_t,
					  so_owner.so_nfs4_owner.so_perclient);

      inc_state_owner_ref(plock_owner);

      release_lockstate(plock_owner);

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          int32_t refcount = atomic_fetch_int32_t(&plock_owner->so_refcount);

          DisplayOwner(plock_owner, str);

          if(refcount > 1)
            LogWarn(COMPONENT_CLIENTID,
                     "Expired State, Possibly extra references to {%s}", str);
          else
            LogFullDebug(COMPONENT_CLIENTID,
                         "Expired State for {%s}", str);
        }

      dec_state_owner_ref(plock_owner);
    }

  /* release the corresponding open states , close files*/
  glist_for_each_safe(glist, glistn, &pclientid->cid_openowners)
    {
      state_owner_t * popen_owner = glist_entry(glist,
                                          state_owner_t,
					  so_owner.so_nfs4_owner.so_perclient);

      inc_state_owner_ref(popen_owner);

      release_openstate(popen_owner);

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          int32_t refcount = atomic_fetch_int32_t(&popen_owner->so_refcount);

          DisplayOwner(popen_owner, str);

          if(refcount > 1)
            LogWarn(COMPONENT_CLIENTID,
                     "Expired State, Possibly extra references to {%s}", str);
          else
            LogFullDebug(COMPONENT_CLIENTID,
                         "Expired State for {%s}", str);
        }

      dec_state_owner_ref(popen_owner);
    }

  if (pclientid->cid_recov_dir != NULL)
    {
      nfs4_rm_clid(pclientid->cid_recov_dir);
      gsh_free(pclientid->cid_recov_dir);
      pclientid->cid_recov_dir = NULL;
    }

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      display_client_id_rec(pclientid, str);
      LogFullDebug(COMPONENT_CLIENTID,
                   "Expired (done) {%s}", str);
    }

  if(isDebug(COMPONENT_CLIENTID))
    {
      display_client_id_rec(pclientid, str);
      LogDebug(COMPONENT_CLIENTID,
               "About to release last reference to {%s}", str);
    }

  /* Release the hash table reference to the clientid. */
  dec_client_id_ref(pclientid);

  return TRUE;
}

int nfs_client_id_get(hash_table_t     * ht,
                      clientid4          clientid,
                      nfs_client_id_t ** p_pclientid)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int           status;

  if(p_pclientid == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) &clientid;
  buffkey.len = sizeof(clientid4);

  if(isFullDebug(COMPONENT_CLIENTID) && isDebug(COMPONENT_HASHTABLE))
    {
      LogFullDebug(COMPONENT_CLIENTID,
                   "%s KEY {%"PRIx64"}", ht->parameter.ht_name, clientid);
    }

  if(isFullDebug(COMPONENT_CLIENTID) &&
     isFullDebug(COMPONENT_HASHTABLE))
    {
      LogFullDebug(COMPONENT_CLIENTID,
                   "-=-=-=-=-=-=-=-=-=-> %s", ht->parameter.ht_name);
      HashTable_Log(COMPONENT_CLIENTID, ht);
    }

  if(HashTable_GetRef(ht,
                      &buffkey,
                      &buffval,
                      Hash_inc_client_id_ref) == HASHTABLE_SUCCESS)
    {
      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_CLIENTID,
                     "%s FOUND", ht->parameter.ht_name);
      *p_pclientid = buffval.pdata;

      status = CLIENT_ID_SUCCESS;
    }
  else
    {
      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_CLIENTID,
                     "%s NOTFOUND", ht->parameter.ht_name);
      *p_pclientid = NULL;
      status = CLIENT_ID_NOT_FOUND;
    }

  return status;
}                               /* nfs_client_id_Get_Pointer */

/**
 *
 * nfs_client_id_get_unconfirmed: Tries to get a pointer to an unconfirmed entry for client_id cache.
 *
 * Tries to get a pointer to an unconfirmed entry for client_id cache.
 *
 * @param clientid      [IN]  the client id
 * @param p_pclientid   [OUT] the found client id
 *
 * @return the result previously set if *pstatus == CLIENT_ID_SUCCESS
 *
 */
int nfs_client_id_get_unconfirmed(clientid4          clientid,
                                  nfs_client_id_t ** p_pclientid)
{
  return nfs_client_id_get(ht_unconfirmed_client_id, clientid, p_pclientid);
}

/**
 *
 * nfs_client_id_get_confirmed: Tries to get a pointer to an confirmed entry for client_id cache.
 *
 * Tries to get a pointer to an confirmed entry for client_id cache.
 *
 * @param clientid      [IN]  the client id
 * @param p_pclientid   [OUT] the found client id
 *
 * @return the result previously set if *pstatus == CLIENT_ID_SUCCESS
 *
 */
int nfs_client_id_get_confirmed(clientid4          clientid,
                                nfs_client_id_t ** p_pclientid)
{
  return nfs_client_id_get(ht_confirmed_client_id, clientid, p_pclientid);
}

/**
 *
 * nfs_Init_client_id: Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 *
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_client_id(nfs_client_id_parameter_t * param)
{
  if((ht_confirmed_client_id = HashTable_Init(&param->cid_confirmed_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Id cache");
      return -1;
    }

  if((ht_unconfirmed_client_id = HashTable_Init(&param->cid_unconfirmed_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Id cache");
      return -1;
    }

  if((ht_client_record = HashTable_Init(&param->cr_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Record cache");
      return -1;
    }

  client_id_pool = pool_init("NFS4 Client ID Pool",
                             sizeof(nfs_client_id_t),
                             pool_basic_substrate,
                             NULL,
                             NULL,
                             NULL);

  if(client_id_pool == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Id Pool");
      return -1;
    }

  client_record_pool = pool_init("NFS4 Client Record Pool",
                                 sizeof(nfs_client_record_t),
                                 pool_basic_substrate,
                                 NULL,
                                 NULL,
                                 NULL);

  if(client_record_pool == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Record Pool");
      return -1;
    }

  return CLIENT_ID_SUCCESS;
}                               /* nfs_Init_client_id */

/**
 *
 * new_clientid: Builds a new clientid4 value using counter and ServerEpoch.
 *
 * Builds a new clientid4 value using counter and ServerEpoch.
 *
 */
clientid4 new_clientid(void)
{
  clientid4 newid;
  uint64_t  epoch_low = ServerEpoch & 0xFFFFFFFF;

  P(clientid_mutex);

  newid = ++clientid_counter + (epoch_low << (clientid4) 32);

  V(clientid_mutex);

  return newid;
}

/**
 *
 * new_clientifd_verifier: Builds a new verifier4 value.
 *
 * Builds a new verifier4 value.
 *
 */
void new_clientifd_verifier(char * pverf)
{
  P(clientid_mutex);

  ++clientid_verifier;

  memcpy(pverf, &clientid_verifier, NFS4_VERIFIER_SIZE);

  V(clientid_mutex);
}

/*******************************************************************************
 *
 * Functions to handle lookup of clientid by nfs_client_id4 received from
 * client.
 *
 ******************************************************************************/

int display_client_record(nfs_client_record_t * precord, char *str)
{
  char * strtmp = str;

  strtmp += sprintf(strtmp, "%p name=", precord);

  strtmp += DisplayOpaqueValue(precord->cr_client_val,
                               precord->cr_client_val_len,
                               strtmp);

  strtmp += sprintf(strtmp, " refcount=%"PRId32,
                    atomic_fetch_int32_t(&precord->cr_refcount));

  return strtmp - str;
}

void inc_client_record_ref(nfs_client_record_t *precord)
{
  atomic_inc_int32_t(&precord->cr_refcount);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_client_record(precord, str);
      LogFullDebug(COMPONENT_CLIENTID,
                   "Increment refcount {%s}",
                   str);
    }
}

void free_client_record(nfs_client_record_t * precord)
{
  if(pthread_mutex_destroy(&precord->cr_mutex) != 0)
    LogCrit(COMPONENT_CLIENTID,
            "pthread_mutex_destroy returned errno %d(%s)",
            errno, strerror(errno));

  pool_free(client_record_pool, precord);
}

void dec_client_record_ref(nfs_client_record_t *precord)
{
  char              str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch latch;
  hash_error_t      rc;
  hash_buffer_t     buffkey;
  hash_buffer_t     old_value;
  hash_buffer_t     old_key;
  int32_t           refcount;

  if(isDebug(COMPONENT_CLIENTID))
    display_client_record(precord, str);

  refcount = atomic_dec_int32_t(&precord->cr_refcount);

  if(refcount > 0)
    {
      LogFullDebug(COMPONENT_CLIENTID,
                   "Decrement refcount refcount now=%"PRId32" {%s}",
                   refcount, str);

      return;
    }

  LogFullDebug(COMPONENT_CLIENTID,
               "Try to remove {%s}",
               str);

  buffkey.pdata = (caddr_t) precord;
  buffkey.len = sizeof(*precord);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(ht_client_record,
                          &buffkey,
                          &old_value,
                          TRUE,
                          &latch);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_client_record, &latch);

      display_client_record(precord, str);

      LogCrit(COMPONENT_CLIENTID,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  refcount = atomic_fetch_int32_t(&precord->cr_refcount);

  if(refcount > 0)
    {
      LogDebug(COMPONENT_CLIENTID,
               "Did not release refcount now=%"PRId32" {%s}",
               refcount, str);

      HashTable_ReleaseLatched(ht_client_record, &latch);

      return;
    }

  /* use the key to delete the entry */
  rc = HashTable_DeleteLatched(ht_client_record,
                               &buffkey,
                               &latch,
                               &old_key,
                               &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_client_record, &latch);

      display_client_record(precord, str);

      LogCrit(COMPONENT_CLIENTID,
              "Error %s, could not remove {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  LogFullDebug(COMPONENT_CLIENTID,
               "Free {%s}",
               str);

  free_client_record(old_value.pdata);
}

uint64_t client_record_value_hash(nfs_client_record_t * pkey)
{
  unsigned int    i;
  uint64_t        res = 0;
  unsigned char * sum = (unsigned char *) &res;

  /* Compute the sum of all the characters across the uint64_t */
  for(i = 0; i < pkey->cr_client_val_len; i++)
    sum[i % sizeof(res)] += (unsigned char)pkey->cr_client_val[i];

  return res;
}

/**
 *
 *  client_record_rbt_hash_func: computes the hash value for the entry in Client Record cache.
 *
 * Computes the hash value for the entry in Client Record cache
 * This function is called internal in the HasTable_* function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
uint32_t client_record_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t    * buffclef)
{
  uint64_t res;

  res = client_record_value_hash(buffclef->pdata) %
        p_hparam->index_size;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_CLIENTID,
                 "value = %"PRIu64, res);

  return (uint32_t) res;
}

/**
 *
 *  client_record_rbt_hash_func: computes the rbt value for the entry in Client Id cache.
 *
 * Computes the rbt value for the entry in Client Record cache.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long client_record_rbt_hash_func(hash_parameter_t * p_hparam,
                                          hash_buffer_t    * buffclef)
{
  uint64_t res;

  res = client_record_value_hash(buffclef->pdata);

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_CLIENTID,
                 "value = %"PRIu64, res);

  return res;
}

/**
 *
 * compare_client_record: compares the cr_client_val the key buffers.
 *
 * compare the cr_client_val stored in the key buffers. This function is to be used as 'compare_key' field in
 * the hashtable storing the client records.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different.
 *
 */
int compare_client_record(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  nfs_client_record_t * pkey1 = buff1->pdata;
  nfs_client_record_t * pkey2 = buff2->pdata;

  if(pkey1->cr_client_val_len != pkey2->cr_client_val_len)
    return 1;

  return memcmp(pkey1->cr_client_val,
                pkey2->cr_client_val,
                pkey1->cr_client_val_len);
}

/**
 *
 * display_client_record: displays the client_record stored in the buffer.
 *
 * displays the client_record stored in the buffer. This function is to be used as 'key_to_str' field in
 * the hashtable storing the client ids.
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_client_record_key(hash_buffer_t * pbuff, char *str)
{
  return display_client_record(pbuff->pdata, str);
}

int display_client_record_val(hash_buffer_t * pbuff, char *str)
{
  return display_client_record(pbuff->pdata, str);
}

nfs_client_record_t *get_client_record(char * value, int len)
{
  nfs_client_record_t * precord;
  hash_buffer_t         buffkey;
  hash_buffer_t         buffval;
  struct hash_latch     latch;
  hash_error_t          rc;
  char                  str[HASHTABLE_DISPLAY_STRLEN];

  precord = pool_alloc(client_record_pool, NULL);

  if(precord == NULL)
    return NULL;

  precord->cr_refcount       = 1;
  precord->cr_client_val_len = len;
  memcpy(precord->cr_client_val, value, len);
  buffkey.pdata = (caddr_t) precord;
  buffkey.len   = sizeof(*precord);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      display_client_record(precord, str);

      LogFullDebug(COMPONENT_CLIENTID,
                   "Find Client Record KEY {%s}", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  rc = HashTable_GetLatch(ht_client_record,
                          &buffkey,
                          &buffval,
                          TRUE,
                          &latch);

  if(rc == HASHTABLE_SUCCESS)
    {
      /* Discard the key we created and return the found Client Record.
       * Directly free since we didn't complete initialization.
       */
      pool_free(client_record_pool, precord);

      precord = buffval.pdata;

      inc_client_record_ref(precord);

      HashTable_ReleaseLatched(ht_client_record, &latch);

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          display_client_record(precord, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Found {%s}",
                       str);
        }

      return precord;
    }

  /* Any other result other than no such key is an error */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      /* Discard the key we created and return.
       * Directly free since we didn't complete initialization.
       */

      if(isFullDebug(COMPONENT_CLIENTID))
        {
          display_client_record(precord, str);

          LogCrit(COMPONENT_CLIENTID,
                  "Error %s, failed to find {%s}",
                  hash_table_err_to_str(rc), str);
        }

      pool_free(client_record_pool, precord);

      return NULL;
    }

  if(pthread_mutex_init(&precord->cr_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, directly free the record since we failed
       * to initialize it. Also release hash latch since we failed to add
       * record.
       */
      HashTable_ReleaseLatched(ht_client_record, &latch);
      pool_free(client_record_pool, precord);
      return NULL;
    }

  /* Use same record for record and key */
  buffval.pdata = (caddr_t) precord;
  buffval.len   = sizeof(*precord);

  if(isFullDebug(COMPONENT_CLIENTID))
    {
      display_client_record(precord, str);

      LogFullDebug(COMPONENT_CLIENTID,
                   "New {%s}", str);
    }

  rc = HashTable_SetLatched(ht_client_record,
                            &buffkey,
                            &buffval,
                            &latch,
                            HASHTABLE_SET_HOW_SET_NO_OVERWRITE,
                            NULL,
                            NULL);

  if(rc == HASHTABLE_SUCCESS)
    {
      if(isFullDebug(COMPONENT_CLIENTID))
        {
          display_client_record(precord, str);
          LogFullDebug(COMPONENT_CLIENTID,
                       "Set Client Record {%s}",
                       str);
        }

      return precord;
    }

  display_client_record(precord, str);

  LogCrit(COMPONENT_CLIENTID,
          "Error %s Failed to add {%s}",
          hash_table_err_to_str(rc), str);

  free_client_record(precord);

  return NULL;
}
