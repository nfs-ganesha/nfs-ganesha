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
 *
 * nfs_open_owner.c : The management of the open owner cache.
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

#include <pthread.h>
#include "log_macros.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs4.h"
#include "sal_functions.h"

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_open_owner;

uint32_t open_owner_counter = 0;
pthread_mutex_t open_owner_counter_lock = PTHREAD_MUTEX_INITIALIZER;

int display_open_owner_key(hash_buffer_t * pbuff, char *str)
{
  char strtmp[NFS4_OPAQUE_LIMIT * 2 + 1];
  unsigned int i = 0;

  state_open_owner_name_t *pname = (state_open_owner_name_t *) pbuff->pdata;

  for(i = 0; i < pname->owner_len; i++)
    sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)pname->owner_val[i]);

  return sprintf(str, "clientid=%llu owner=(%u|%s)",
                 (unsigned long long)pname->clientid, pname->owner_len, strtmp);
}                               /* display_state_id_val */

int display_open_owner_val(hash_buffer_t * pbuff, char *str)
{
  char strtmp[NFS4_OPAQUE_LIMIT * 2 + 1];
  unsigned int i = 0;

  state_open_owner_t *powner = (state_open_owner_t *) (pbuff->pdata);

  for(i = 0; i < powner->owner_len; i++)
    sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)powner->owner_val[i]);

  return sprintf(str, "clientid=%llu owner=(%u|%s) confirmed=%u seqid=%u",
                       (unsigned long long)powner->clientid, powner->owner_len, strtmp,
                       powner->confirmed, powner->seqid);
}                               /* display_state_id_val */

int compare_open_owner(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_open_owner_key(buff1, str1);
      display_open_owner_key(buff2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "compare_open_owner => {%s}|{%s}", str1, str2);
    }

  state_open_owner_name_t *pname1 = (state_open_owner_name_t *) buff1->pdata;
  state_open_owner_name_t *pname2 = (state_open_owner_name_t *) buff2->pdata;

  if(pname1 == NULL || pname2 == NULL)
    return 1;

  if(pname1->clientid != pname2->clientid)
    return 1;

  if(pname1->owner_len != pname2->owner_len)
    return 1;

  return memcmp((char *)pname1->owner_val, (char *)pname2->owner_val, pname1->owner_len);
}                               /* compare_open_owner */

unsigned long open_owner_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  state_open_owner_name_t *pname = (state_open_owner_name_t *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

  LogFullDebug(COMPONENT_STATE,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* open_owner_value_hash_func */

unsigned long open_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  state_open_owner_name_t *pname = (state_open_owner_name_t *) buffclef->pdata;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

  LogFullDebug(COMPONENT_STATE, "---> rbt_hash_func = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

/**
 *
 * nfs4_Init_open_owner: Init the hashtable for NFS Open Owner cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs4_Init_open_owner(nfs_open_owner_parameter_t param)
{

  if((ht_open_owner = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "NFS STATE_ID: Cannot init NFS Open Owner cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_open_owner */

/**
 * nfs_open_owner_Set
 * 
 *
 * This routine sets a open owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Set(state_open_owner_name_t * pname,
                       state_open_owner_t      * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(state_open_owner_name_t);

      display_open_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "nfs_open_owner_Set => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_open_owner_name_t);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(state_open_owner_t);

  P(open_owner_counter_lock);
  open_owner_counter += 1;
  powner->counter = open_owner_counter;
  V(open_owner_counter_lock);

  if(HashTable_Test_And_Set
     (ht_open_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs_open_owner_Set */

/**
 *
 * nfs_open_owner_Get_Pointer
 *
 * This routine gets a pointer to an open owner from the open owners's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Get_Pointer(state_open_owner_name_t  * pname,
                               state_open_owner_t      ** powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(state_open_owner_name_t);

      display_open_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "nfs_open_owner_Get_Pointer => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "nfs_open_owner_Get_Pointer => NOTFOUND");
      return 0;
    }

  *powner = (state_open_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATE,
               "nfs_open_owner_Get_Pointer => FOUND");

  return 1;
}                               /* nfs_open_owner_Get_Pointer */

/**
 *
 * nfs_open_owner_Del
 *
 * This routine removes a open owner from the open_owner's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Del(state_open_owner_name_t * pname)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_open_owner_name_t);

  if(HashTable_Del(ht_open_owner, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nfs_open_owner_Del */

/**
 * 
 *  nfs_open_owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the open owners. 
 * 
 * @return nothing (void function)
 */

void nfs_open_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_open_owner);
}                               /* nfs_open_owner_PrintAll */

int nfs_convert_open_owner(open_owner4             * pnfsowner,
                           state_open_owner_name_t * pname_owner)
{
  if(pnfsowner == NULL || pname_owner == NULL)
    return 0;

  pname_owner->clientid = pnfsowner->clientid;
  pname_owner->owner_len = pnfsowner->owner.owner_len;
  memcpy((char *)pname_owner->owner_val, (char *)pnfsowner->owner.owner_val,
         pnfsowner->owner.owner_len);

  return 1;
}                               /* nfs_convert_open_owner */
