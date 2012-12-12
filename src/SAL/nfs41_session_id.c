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
 * @brief The management of the session id cache.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "sal_functions.h"

pool_t *nfs41_session_pool = NULL;

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_session_id;
uint64_t global_sequence = 0;
pthread_mutex_t mutex_sequence = PTHREAD_MUTEX_INITIALIZER;

int display_session_id(struct display_buffer * dspbuf, char *session_id)
{
  int b_left;

  b_left = display_cat(dspbuf, "sessionid=");

  if(b_left <= 0)
    return b_left;

  return display_opaque_bytes(dspbuf, session_id, NFS4_SESSIONID_SIZE);
}

#define DISPLAY_SESSION_ID_SIZE (NFS4_SESSIONID_SIZE * 2 + 32)

int display_session_id_key(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff)
{
  return display_session_id(dspbuf, pbuff->pdata);
}

int display_session(struct display_buffer * dspbuf, nfs41_session_t * psession)
{
  return display_session_id(dspbuf, psession->session_id);
}

int display_session_id_val(struct display_buffer * dspbuf,
                           hash_buffer_t         * pbuff)
{
  return display_session(dspbuf, pbuff->pdata);
}

int compare_session_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return memcmp(buff1->pdata, buff2->pdata, NFS4_SESSIONID_SIZE);
}

uint32_t session_id_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef)
{
  /* Only need to hash the global counter portion since it is unique */
  uint64_t sum;

  memcpy(&sum, ((char *)buffclef->pdata) + sizeof(clientid4), sizeof(sum));

  if(isFullDebug(COMPONENT_SESSIONS) && isDebug(COMPONENT_HASHTABLE))
    {
      char                  str[DISPLAY_SESSION_ID_SIZE];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_session_id_key(&dspbuf, buffclef);

      LogFullDebug(COMPONENT_SESSIONS,
                   "value hash: %s=%"PRIu32,
                   str,
                   (uint32_t)(sum % p_hparam->index_size));
    }

  return (uint32_t)(sum % p_hparam->index_size);
}                               /*  client_id_reverse_value_hash_func */

uint64_t session_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef)
{
  /* Only need to hash the global counter portion since it is unique */
  uint64_t i1 = 0;

  memcpy(&i1, ((char *)buffclef->pdata) + sizeof(clientid4), sizeof(i1));

  if(isFullDebug(COMPONENT_SESSIONS) && isDebug(COMPONENT_HASHTABLE))
    {
      char                  str[DISPLAY_SESSION_ID_SIZE];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_session_id_key(&dspbuf, buffclef);

      LogFullDebug(COMPONENT_SESSIONS,
                   "rbt hash: %s=%"PRIu64,
                   str,
                   i1);
    }

  return i1;
}                               /* session_id_rbt_hash_func */

/**
 *
 * nfs41_Init_session_id: Init the hashtable for Session Id cache.
 *
 * Perform all the required initialization for hashtable Session Id cache
 *
 * @param param [IN] parameter used to init the session id cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs41_Init_session_id(nfs_session_id_parameter_t param)
{
  if((ht_session_id = HashTable_Init(&param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_SESSIONS,
              "NFS SESSION_ID: Cannot init Session Id cache");
      return -1;
    }

  return 0;
}                               /* nfs_Init_sesion_id */

/**
 *
 * nfs41_Build_sessionid
 *
 * This routine fills in the pcontext field in the compound data.
 * pentry is supposed to be locked when this function is called.
 *
 * @param pclientid     [IN]    pointer to the related clientid
 * @param sessionid    [OUT]   the sessionid
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
void nfs41_Build_sessionid(clientid4 * pclientid, char * sessionid)
{
  uint64_t seq;

  P(mutex_sequence);
  seq = ++global_sequence;
  V(mutex_sequence);

  memset(sessionid, 0, NFS4_SESSIONID_SIZE);
  memcpy(sessionid, pclientid, sizeof(clientid4));
  memcpy(sessionid + sizeof(clientid4), &seq, sizeof(seq));
}                               /* nfs41_Build_sessionid */

/**
 *
 * nfs41_Session_Set
 *
 * This routine sets a session into the sessions's hashtable.
 *
 * @param psession [IN] pointer to the sessionid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Set(char sessionid[NFS4_SESSIONID_SIZE],
                      nfs41_session_t * psession_data)
{
  hash_buffer_t         buffkey;
  hash_buffer_t         buffval;
  char                  str[DISPLAY_SESSION_ID_SIZE];
  struct display_buffer dspbuf = {sizeof(str), str, str};

  if(isFullDebug(COMPONENT_SESSIONS))
    {
      (void) display_session_id(&dspbuf, sessionid);
      LogFullDebug(COMPONENT_SESSIONS,
                   "Set %s", str);
    }

  if((buffkey.pdata = gsh_malloc(NFS4_SESSIONID_SIZE)) == NULL)
    return 0;
  memcpy(buffkey.pdata, sessionid, NFS4_SESSIONID_SIZE);
  buffkey.len = NFS4_SESSIONID_SIZE;

  buffval.pdata = (caddr_t) psession_data;
  buffval.len = sizeof(nfs41_session_t);

  if(HashTable_Test_And_Set
     (ht_session_id, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs41_Session_Set */

/**
 *
 * nfs41_Session_Get_Pointer
 *
 * This routine gets a pointer to a session from the sessions's hashtable.
 *
 * @param psession       [IN] pointer to the sessionid to be checked.
 * @param ppsession_data [OUT] pointer's session found
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Get_Pointer(char sessionid[NFS4_SESSIONID_SIZE],
                              nfs41_session_t * *psession_data)
{
  hash_buffer_t         buffkey;
  hash_buffer_t         buffval;
  char                  str[DISPLAY_SESSION_ID_SIZE];
  struct display_buffer dspbuf = {sizeof(str), str, str};

  if(isFullDebug(COMPONENT_SESSIONS))
    {
      (void) display_session_id(&dspbuf, sessionid);
      LogFullDebug(COMPONENT_SESSIONS,
                   "Get %s", str);
    }

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if(HashTable_Get(ht_session_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_SESSIONS,
                   "%s Not Found", str);
      return 0;
    }

  *psession_data = (nfs41_session_t *) buffval.pdata;

  LogFullDebug(COMPONENT_SESSIONS,
               "%s Found", str);

  return 1;
}                               /* nfs41_Session_Get_Pointer */

/**
 *
 * nfs41_Session_Del
 *
 * This routine removes a session from the sessions's hashtable.
 *
 * @param sessionid [IN] sessionid, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs41_Session_Del(char sessionid[NFS4_SESSIONID_SIZE])
{
  hash_buffer_t         buffkey;
  hash_buffer_t         old_key;
  hash_buffer_t         old_value;
  char                  str[DISPLAY_SESSION_ID_SIZE];
  struct display_buffer dspbuf = {sizeof(str), str, str};

  if(isFullDebug(COMPONENT_SESSIONS))
    {
      (void) display_session_id(&dspbuf, sessionid);
      LogFullDebug(COMPONENT_SESSIONS,
                   "Delete %s", str);
    }

  buffkey.pdata = (caddr_t) sessionid;
  buffkey.len = NFS4_SESSIONID_SIZE;

  if(HashTable_Del(ht_session_id, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      nfs41_session_t * psession = (nfs41_session_t *) old_value.pdata;

      /* free the key that was stored in hash table */
      gsh_free(old_key.pdata);

      /* Decrement our reference to the clientid record */
      dec_client_id_ref(psession->pclientid_record);

      /* Free the memory for the session */
      pool_free(nfs41_session_pool, psession);

      return 1;
    }
  else
    return 0;
}                               /* nfs41_Session_Del */

/**
 *
 *  nfs41_Session_PrintAll
 *
 * This routine displays the content of the hashtable used to store the sessions.
 *
 * @return nothing (void function)
 */

void nfs41_Session_PrintAll(void)
{
  HashTable_Log(COMPONENT_SESSIONS, ht_session_id);
}                               /* nfs41_Session_PrintAll */
