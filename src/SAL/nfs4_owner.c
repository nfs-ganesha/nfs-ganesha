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
 * nfs_owner.c : The management of the NFS4 Owner cache.
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

hash_table_t *ht_nfs4_owner;

uint32_t nfs4_owner_counter = 0;
pthread_mutex_t nfs4_owner_counter_lock = PTHREAD_MUTEX_INITIALIZER;

int display_nfs4_owner_key(hash_buffer_t * pbuff, char *str)
{
  char strtmp[NFS4_OPAQUE_LIMIT * 2 + 1];
  unsigned int i = 0;

  state_nfs4_owner_name_t *pname = (state_nfs4_owner_name_t *) pbuff->pdata;

  for(i = 0; i < pname->son_owner_len; i++)
    sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)pname->son_owner_val[i]);

  return sprintf(str,
                 "clientid=%llu owner=(%u|%s)",
                 (unsigned long long)pname->son_clientid,
                 pname->son_owner_len,
                 strtmp);
}                               /* display_state_id_val */

int display_nfs4_owner_val(hash_buffer_t * pbuff, char *str)
{
  char strtmp[NFS4_OPAQUE_LIMIT * 2 + 1];
  unsigned int i = 0;

  state_owner_t *powner = (state_owner_t *) (pbuff->pdata);

  for(i = 0; i < powner->so_owner.so_nfs4_owner.so_owner_len; i++)
    sprintf(&(strtmp[i * 2]),
            "%02x",
            (unsigned char)powner->so_owner.so_nfs4_owner.so_owner_val[i]);

  return sprintf(str, "clientid=%llu owner=(%u|%s) confirmed=%u seqid=%u",
                       (unsigned long long)powner->so_owner.so_nfs4_owner.so_clientid,
                       powner->so_owner.so_nfs4_owner.so_owner_len, strtmp,
                       powner->so_owner.so_nfs4_owner.so_confirmed,
                       powner->so_owner.so_nfs4_owner.so_seqid);
}                               /* display_state_id_val */

int compare_nfs4_owner(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nfs4_owner_key(buff1, str1);
      display_nfs4_owner_key(buff2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "compare_nfs4_owner => {%s}|{%s}", str1, str2);
    }

  state_nfs4_owner_name_t *pname1 = (state_nfs4_owner_name_t *) buff1->pdata;
  state_nfs4_owner_name_t *pname2 = (state_nfs4_owner_name_t *) buff2->pdata;

  if(pname1 == NULL || pname2 == NULL)
    return 1;

  if(pname1->son_clientid != pname2->son_clientid)
    return 1;

  if(pname1->son_owner_len != pname2->son_owner_len)
    return 1;

  return memcmp(pname1->son_owner_val, pname2->son_owner_val, pname1->son_owner_len);
}                               /* compare_nfs4_owner */

unsigned long nfs4_owner_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  state_nfs4_owner_name_t *pname = (state_nfs4_owner_name_t *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->son_owner_len; i++)
    {
      c = ((char *)pname->son_owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->son_clientid) + (unsigned long)sum + pname->son_owner_len;

  LogFullDebug(COMPONENT_STATE,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nfs4_owner_value_hash_func */

unsigned long nfs4_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  state_nfs4_owner_name_t *pname = (state_nfs4_owner_name_t *) buffclef->pdata;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->son_owner_len; i++)
    {
      c = ((char *)pname->son_owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->son_clientid) + (unsigned long)sum + pname->son_owner_len;

  LogFullDebug(COMPONENT_STATE, "---> rbt_hash_func = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

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

  if((ht_nfs4_owner = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "NFS STATE_ID: Cannot init NFS Open Owner cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_nfs4_owner */

/**
 * nfs4_owner_Set
 * 
 *
 * This routine sets a open owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_owner_Set(state_nfs4_owner_name_t * pname,
                   state_owner_t           * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(state_nfs4_owner_name_t);

      display_nfs4_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "nfs4_owner_Set => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_nfs4_owner_name_t);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(state_owner_t);

  P(nfs4_owner_counter_lock);
  nfs4_owner_counter += 1;
  powner->so_owner.so_nfs4_owner.so_counter = nfs4_owner_counter;
  V(nfs4_owner_counter_lock);

  if(HashTable_Test_And_Set
     (ht_nfs4_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs4_owner_Set */

/**
 *
 * nfs4_owner_Get_Pointer
 *
 * This routine gets a pointer to an open owner from the open owners's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_owner_Get_Pointer(state_nfs4_owner_name_t  * pname,
                           state_owner_t           ** powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(state_nfs4_owner_name_t);

      display_nfs4_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "nfs4_owner_Get_Pointer => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_nfs4_owner_name_t);

  if(HashTable_Get(ht_nfs4_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "nfs4_owner_Get_Pointer => NOTFOUND");
      return 0;
    }

  *powner = (state_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATE,
               "nfs4_owner_Get_Pointer => FOUND");

  return 1;
}                               /* nfs4_owner_Get_Pointer */

/**
 *
 * nfs4_owner_Del
 *
 * This routine removes a open owner from the nfs4_owner's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_owner_Del(state_nfs4_owner_name_t * pname)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(state_nfs4_owner_name_t);

  if(HashTable_Del(ht_nfs4_owner, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nfs4_owner_Del */

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

int convert_nfs4_owner(open_owner4             * pnfsowner,
                       state_nfs4_owner_name_t * pname_owner)
{
  if(pnfsowner == NULL || pname_owner == NULL)
    return 0;

  pname_owner->son_clientid = pnfsowner->clientid;
  pname_owner->son_owner_len = pnfsowner->owner.owner_len;
  memcpy(pname_owner->son_owner_val,
         pnfsowner->owner.owner_val,
         pnfsowner->owner.owner_len);

  return 1;
}                               /* convert_nfs4_owner */

state_owner_t *create_nfs4_owner(cache_inode_client_t    * pclient,
                                 state_nfs4_owner_name_t * pname,
                                 open_owner4             * arg_owner,
                                 state_owner_t           * related_owner,
                                 unsigned int              init_seqid)
{
  state_owner_t           * powner;
  state_nfs4_owner_name_t * powner_name;

  /* This lock owner is not known yet, allocated and set up a new one */
  GetFromPool(powner, &pclient->pool_state_owner, state_owner_t);

  if(powner == NULL)
    return NULL;

  GetFromPool(powner_name, &pclient->pool_nfs4_owner_name, state_nfs4_owner_name_t);

  if(powner_name == NULL)
    {
      ReleaseToPool(powner, &pclient->pool_state_owner);
      return NULL;
    }

  *powner_name = *pname;

  /* set up the content of the open_owner */
  powner->so_owner.so_nfs4_owner.so_confirmed     = FALSE;
  powner->so_owner.so_nfs4_owner.so_seqid         = init_seqid;
  powner->so_owner.so_nfs4_owner.so_related_owner = related_owner;
  powner->so_owner.so_nfs4_owner.so_clientid      = arg_owner->clientid;
  powner->so_owner.so_nfs4_owner.so_owner_len     = arg_owner->owner.owner_len;

  memcpy(powner->so_owner.so_nfs4_owner.so_owner_val,
         arg_owner->owner.owner_val,
         arg_owner->owner.owner_len);

  powner->so_owner.so_nfs4_owner.so_owner_val[powner->so_owner.so_nfs4_owner.so_owner_len] = '\0';

  if(pthread_mutex_init(&powner->so_mutex, NULL) == -1)
    {
      ReleaseToPool(powner, &pclient->pool_state_owner);
      ReleaseToPool(powner_name, &pclient->pool_nfs4_owner_name);
      return NULL;
    }

  if(!nfs4_owner_Set(powner_name, powner))
    {
      ReleaseToPool(powner, &pclient->pool_state_owner);
      ReleaseToPool(powner_name, &pclient->pool_nfs4_owner_name);
      return NULL;
    }

  return powner;
}
