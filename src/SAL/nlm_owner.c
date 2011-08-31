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
 * nlm_owner.c : The management of the NLM owner cache.
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

#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "log_macros.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nsm.h"

//TODO FSF: check if can optimize by using same reference as key and value

hash_table_t *ht_nlm_owner;
hash_table_t *ht_nlm_client;

int display_nlm_client(state_nlm_client_t *pkey, char *str)
{
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "<NULL>");

  strtmp += sprintf(strtmp, "caller_name=");
  strncpy(strtmp, pkey->slc_nlm_caller_name, pkey->slc_nlm_caller_name_len);
  strtmp += pkey->slc_nlm_caller_name_len;
  *strtmp = '\0';

  return strlen(str);
}

int display_nlm_client_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client((state_nlm_client_t *)pbuff->pdata, str);
}

int display_nlm_client_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client((state_nlm_client_t *)pbuff->pdata, str);
}

int compare_nlm_client(state_nlm_client_t *pclient1,
                       state_nlm_client_t *pclient2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient1, str1);
      display_nlm_client(pclient2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(pclient1 == NULL || pclient2 == NULL)
    return 1;

  if(pclient1 == pclient2)
    return 0;

  if(pclient1->slc_nlm_caller_name_len != pclient2->slc_nlm_caller_name_len)
    return 1;

  return memcmp(pclient1->slc_nlm_caller_name,
                pclient2->slc_nlm_caller_name,
                pclient1->slc_nlm_caller_name_len);
}

int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_client((state_nlm_client_t *)buff1->pdata,
                            (state_nlm_client_t *)buff2->pdata);

}                               /* compare_nlm_client */

unsigned long nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_nlm_client_t *pkey = (state_nlm_client_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->slc_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->slc_nlm_caller_name[i];

  res = (unsigned long) sum +
        (unsigned long) pkey->slc_nlm_caller_name_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nlm_client_value_hash_func */

unsigned long nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_nlm_client_t *pkey = (state_nlm_client_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->slc_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->slc_nlm_caller_name[i];

  res = (unsigned long) sum +
        (unsigned long) pkey->slc_nlm_caller_name_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

int display_nlm_owner(state_owner_t *pkey, char *str)
{
  unsigned int i = 0;
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "<NULL>");

  strtmp += display_nlm_client(pkey->so_owner.so_nlm_owner.so_client, str);

  strtmp += sprintf(strtmp, " oh=(%u|", pkey->so_owner_len);

  for(i = 0; i < pkey->so_owner_len; i++)
    if(!isprint(pkey->so_owner_val[i]))
      break;

  if(i == pkey->so_owner_len)
    {
      memcpy(strtmp, pkey->so_owner_val, pkey->so_owner_len);
      strtmp[pkey->so_owner_len] = '\0';
      strtmp += pkey->so_owner_len;
    }
  else for(i = 0; i < pkey->so_owner_len; i++)
    {
      sprintf(strtmp, "%02x", (unsigned char)pkey->so_owner_val[i]);
      strtmp += 2;
    }

  strtmp += sprintf(strtmp, ") svid=%d", pkey->so_owner.so_nlm_owner.so_nlm_svid);
  return strlen(str);
}

int display_nlm_owner_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((state_owner_t *)pbuff->pdata, str);
}

int display_nlm_owner_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((state_owner_t *)pbuff->pdata, str);
}

int compare_nlm_owner(state_owner_t *powner1,
                      state_owner_t *powner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner1, str1);
      display_nlm_owner(powner2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(powner1 == NULL || powner2 == NULL)
    return 1;

  if(powner1 == powner2)
    return 0;

  if(compare_nlm_client(powner1->so_owner.so_nlm_owner.so_client,
                        powner2->so_owner.so_nlm_owner.so_client) != 0)
    return 1;

  /* Handle special owner that matches any lock owner with the same nlm client */
  if(powner1->so_owner_len == -1 ||
     powner2->so_owner_len == -1)
    return 0;

  if(powner1->so_owner.so_nlm_owner.so_nlm_svid !=
     powner2->so_owner.so_nlm_owner.so_nlm_svid)
    return 1;

  if(powner1->so_owner_len !=
     powner2->so_owner_len)
    return 1;

  return memcmp(powner1->so_owner_val,
                powner2->so_owner_val,
                powner1->so_owner_len);
}

int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_owner((state_owner_t *)buff1->pdata,
                           (state_owner_t *)buff2->pdata);

}                               /* compare_nlm_owner */

unsigned long nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = (state_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    sum += (unsigned char)pkey->so_owner_val[i];

  res = (unsigned long) (pkey->so_owner.so_nlm_owner.so_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->so_owner_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nlm_so_nlm_ohue_hash_func */

unsigned long nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = (state_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    sum += (unsigned char)pkey->so_owner_val[i];

  res = (unsigned long) (pkey->so_owner.so_nlm_owner.so_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->so_owner_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

/**
 *
 * Init_nlm_hash: Init the hashtable for NLM Owner cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int Init_nlm_hash(hash_parameter_t client_param, hash_parameter_t owner_param)
{

  if((ht_nlm_client = HashTable_Init(client_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NLM Client cache");
      return -1;
    }

  if((ht_nlm_owner = HashTable_Init(owner_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NLM Owner cache");
      return -1;
    }

  return 0;
}                               /* Init_nlm_hash */

/**
 * nlm_client_Set
 * 
 *
 * This routine sets a NLM client into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_client_Set(state_nlm_client_t * pkey,
                   state_nlm_client_t * pclient)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(*pkey);

      display_nlm_client_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  buffval.pdata = (caddr_t) pclient;
  buffval.len = sizeof(*pclient);

  if(HashTable_Test_And_Set
     (ht_nlm_client, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nlm_client_Set */

static int Hash_del_nlm_client_ref(hash_buffer_t *buffval)
{
  int rc;
  state_nlm_client_t *pclient = (state_nlm_client_t *)(buffval->pdata);

  P(pclient->slc_mutex);

  pclient->slc_refcount--;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount NLM Client pclient=%p {%s}, refcount = %d",
                   pclient, str, pclient->slc_refcount);
    }

  rc = pclient->slc_refcount;

  V(pclient->slc_mutex);  

  return rc;
}

static void Hash_inc_client_ref(hash_buffer_t *buffval)
{
  state_nlm_client_t *pclient = (state_nlm_client_t *)(buffval->pdata);

  P(pclient->slc_mutex);
  pclient->slc_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount NLM Client pclient=%p {%s}, refcount = %d",
                   pclient, str, pclient->slc_refcount);
    }

  V(pclient->slc_mutex);  
}

void inc_nlm_client_ref_locked(state_nlm_client_t *pclient)
{
  pclient->slc_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount NLM Client pclient=%p {%s}, refcount = %d",
                   pclient, str, pclient->slc_refcount);
    }

  V(pclient->slc_mutex);
}

void inc_nlm_client_ref(state_nlm_client_t *pclient)
{
  P(pclient->slc_mutex);

  inc_nlm_client_ref_locked(pclient);
}

void dec_nlm_client_ref_locked(state_nlm_client_t *pclient)
{
  bool_t remove = FALSE;
  char   str[HASHTABLE_DISPLAY_STRLEN];

  if(isFullDebug(COMPONENT_STATE))
    display_nlm_client(pclient, str);

  if(pclient->slc_refcount > 1)
    {
      pclient->slc_refcount--;

      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount NLM Client pclient=%p {%s}, refcount = %d",
                   pclient, str, pclient->slc_refcount);
    }
  else
    remove = TRUE;

  V(pclient->slc_mutex);

  if(remove)
    {
      hash_buffer_t buffkey, old_key, old_value;

      buffkey.pdata = (caddr_t) pclient;
      buffkey.len = sizeof(*pclient);

      switch(HashTable_DelRef(ht_nlm_client, &buffkey, &old_key, &old_value, Hash_del_nlm_client_ref))
        {
          case HASHTABLE_SUCCESS:
            LogFullDebug(COMPONENT_STATE,
                         "Free NLM Client pclient=%p {%s}, refcount = %d",
                         pclient, str, pclient->slc_refcount);
            nsm_unmonitor((state_nlm_client_t *) old_value.pdata);
            Mem_Free(old_key.pdata);
            Mem_Free(old_value.pdata);
            break;

          case HASHTABLE_NOT_DELETED:
            /* ref count didn't end up at 0, don't free. */
            LogDebug(COMPONENT_STATE,
                     "HashTable_DelRef didn't reduce refcount to 0 for NLM Client pclient=%p {%s}",
                     pclient, str);
            break;

          default:
            /* some problem occurred */
            LogDebug(COMPONENT_STATE,
                     "HashTable_DelRef failed for NLM Client pclient=%p {%s}",
                     pclient, str);
            break;
        }
    }
}

void dec_nlm_client_ref(state_nlm_client_t *pclient)
{
  P(pclient->slc_mutex);

  dec_nlm_client_ref_locked(pclient);
}

/**
 *
 * nlm_client_Get_Pointer
 *
 * This routine gets a pointer to an NLM client from the nlm_client's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_client_Get_Pointer(state_nlm_client_t * pkey,
                           state_nlm_client_t * *pclient)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_nlm_client, &buffkey, &buffval, Hash_inc_client_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "NOTFOUND");
      *pclient = NULL;
      return 0;
    }

  *pclient = (state_nlm_client_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATE,
               "FOUND");

  return 1;
}                               /* nlm_client_Get_Pointer */

/**
 * 
 *  nlm_client_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the NLM clients. 
 * 
 * @return nothing (void function)
 */

void nlm_client_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_nlm_client);
}                               /* nlm_client_PrintAll */

state_nlm_client_t *get_nlm_client(care_t care, const char * caller_name)
{
  state_nlm_client_t *pkey, *pclient;

  if(caller_name == NULL)
    return NULL;

  pkey = (state_nlm_client_t *)Mem_Alloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));
  pkey->slc_refcount            = 1;
  pkey->slc_nlm_caller_name_len = strlen(caller_name);

  if(pkey->slc_nlm_caller_name_len > LM_MAXSTRLEN)
    return NULL;

  memcpy(pkey->slc_nlm_caller_name,
         caller_name,
         pkey->slc_nlm_caller_name_len);
  pkey->slc_nlm_caller_name[pkey->slc_nlm_caller_name_len] = '\0';
  
  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "Find NLM Client pkey {%s}", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  if(nlm_client_Get_Pointer(pkey, &pclient) == 1 || care == CARE_NOT)
    {
      /* Discard the key we created and return the found NLM Client */
      Mem_Free(pkey);

      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_client(pclient, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found NLM Client pclient=%p {%s}, refcount = %d",
                       pclient, str, pclient != NULL ? pclient->slc_refcount : 0);
        }

      if(care == CARE_MONITOR)
        if(!nsm_monitor(pclient))
          {
            dec_nlm_client_ref(pclient);
            return NULL;
          }

      return pclient;
    }

  pclient = (state_nlm_client_t *)Mem_Alloc(sizeof(*pkey));
  if(pclient == NULL)
    {
      Mem_Free(pkey);
      return NULL;
    }

  /* Copy everything over */
  *pclient = *pkey;
  init_glist(&pclient->slc_lock_list);

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "New pclient=%p {%s}", pclient, str);
    }

  if(pthread_mutex_init(&pclient->slc_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the key and created owner */
      Mem_Free(pkey);
      Mem_Free(pclient);
      return NULL;
    }

  if(nlm_client_Set(pkey, pclient) == 1)
    {
      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_client(pclient, str);
          LogFullDebug(COMPONENT_STATE,
                       "Set NLM Client pclient=%p {%s}, refcount = %d",
                       pclient, str, pclient->slc_refcount);
        }

      if(care != CARE_MONITOR || nsm_monitor(pclient))
        return pclient;

      dec_nlm_client_ref(pclient);
      return NULL;
    }

  Mem_Free(pkey);
  Mem_Free(pclient);
  return NULL;
}

/**
 * nlm_owner_Set
 * 
 *
 * This routine sets a NLM owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Set(state_owner_t * pkey,
                  state_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(*pkey);

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(*powner);

  if(HashTable_Test_And_Set
     (ht_nlm_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nlm_owner_Set */

static int Hash_del_nlm_owner_ref(hash_buffer_t *buffval)
{
  int rc;
  state_owner_t *powner = (state_owner_t *)(buffval->pdata);

  P(powner->so_mutex);

  powner->so_refcount--;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount for NLM Owner powner=%p {%s}, refcount = %d",
                   powner, str, powner->so_refcount);
    }

  rc = powner->so_refcount;

  V(powner->so_mutex);  

  return rc;
}

static void Hash_inc_owner_ref(hash_buffer_t *buffval)
{
  state_owner_t *powner = (state_owner_t *)(buffval->pdata);

  P(powner->so_mutex);
  powner->so_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount for NLM Owner powner=%p {%s}, refcount = %d",
                   powner, str, powner->so_refcount);
    }

  V(powner->so_mutex);  
}

void inc_nlm_owner_ref_locked(state_owner_t *powner)
{
  powner->so_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount for NLM Owner powner=%p {%s}, refcount = %d",
                   powner, str, powner->so_refcount);
    }

  V(powner->so_mutex);
}

void inc_nlm_owner_ref(state_owner_t *powner)
{
  P(powner->so_mutex);

  inc_nlm_owner_ref_locked(powner);
}

void dec_nlm_owner_ref_locked(state_owner_t *powner)
{
  bool_t remove = FALSE;
  char   str[HASHTABLE_DISPLAY_STRLEN];

  if(isFullDebug(COMPONENT_STATE))
    display_nlm_owner(powner, str);

  if(powner->so_refcount > 1)
    {
      powner->so_refcount--;

      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount for NLM Owner powner=%p {%s}, refcount = %d",
                   powner, str, powner->so_refcount);
    }
  else
    remove = TRUE;

  V(powner->so_mutex);

  if(remove)
    {
      hash_buffer_t buffkey, old_key, old_value;

      buffkey.pdata = (caddr_t) powner;
      buffkey.len = sizeof(*powner);

      switch(HashTable_DelRef(ht_nlm_owner, &buffkey, &old_key, &old_value, Hash_del_nlm_owner_ref))
        {
          case HASHTABLE_SUCCESS:
            LogFullDebug(COMPONENT_STATE,
                         "Free NLM Owner powner=%p {%s}, refcount = %d",
                         powner, str, powner->so_refcount);
            dec_nlm_client_ref(powner->so_owner.so_nlm_owner.so_client);
            Mem_Free(old_key.pdata);
            Mem_Free(old_value.pdata);
            break;

          case HASHTABLE_NOT_DELETED:
            /* ref count didn't end up at 0, don't free. */
            LogDebug(COMPONENT_STATE,
                     "HashTable_DelRef didn't reduce refcount to 0 for powner=%p {%s}, refcount = %d",
                      powner, str, powner->so_refcount);
            break;

          default:
            /* some problem occurred */
            LogDebug(COMPONENT_STATE,
                     "HashTable_DelRef failed for powner=%p {%s}",
                      powner, str);
            break;
        }
    }
}

void dec_nlm_owner_ref(state_owner_t *powner)
{
  P(powner->so_mutex);

  dec_nlm_owner_ref_locked(powner);
}

/**
 *
 * nlm_owner_Get_Pointer
 *
 * This routine gets a pointer to an NLM owner from the nlm_owner's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Get_Pointer(state_owner_t  * pkey,
                          state_owner_t ** powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_nlm_owner, &buffkey, &buffval, Hash_inc_owner_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "NOTFOUND");
      return 0;
    }

  *powner = (state_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATE,
               "FOUND");

  return 1;
}                               /* nlm_owner_Get_Pointer */

/**
 * 
 *  nlm_owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the NLM owners. 
 * 
 * @return nothing (void function)
 */

void nlm_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_nlm_owner);
}                               /* nlm_owner_PrintAll */

state_owner_t *get_nlm_owner(care_t               care,
                             state_nlm_client_t * pclient, 
                             netobj             * oh,
                             uint32_t             svid)
{
  state_owner_t * pkey, *powner;

  if(pclient == NULL || oh == NULL || oh->n_len > MAX_NETOBJ_SZ)
    return NULL;

  pkey = (state_owner_t *)Mem_Alloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));
  pkey->so_type                             = STATE_LOCK_OWNER_NLM;
  pkey->so_refcount                         = 1;
  pkey->so_owner.so_nlm_owner.so_client     = pclient;
  pkey->so_owner.so_nlm_owner.so_nlm_svid   = svid;
  pkey->so_owner_len                        = oh->n_len;
  memcpy(pkey->so_owner_val, oh->n_bytes, oh->n_len);

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(pkey, str);

      LogFullDebug(COMPONENT_STATE,
                   "Find NLM Owner pkey {%s}", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  if(nlm_owner_Get_Pointer(pkey, &powner) == 1 || care == CARE_NOT)
    {
      /* Discard the key we created and return the found NLM Owner */
      Mem_Free(pkey);

      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_owner(powner, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found NLM Owner powner=%p {%s}, refcount = %d",
                       powner, str, powner != NULL ? powner->so_refcount : 0);
        }

      return powner;
    }
    
  powner = (state_owner_t *)Mem_Alloc(sizeof(*pkey));
  if(powner == NULL)
    {
      Mem_Free(pkey);
      return NULL;
    }

  /* Copy everything over */
  *powner = *pkey;
  init_glist(&powner->so_lock_list);

  if(pthread_mutex_init(&powner->so_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the key and created owner */
      Mem_Free(pkey);
      Mem_Free(powner);
      return NULL;
    }

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "New powner=%p {%s}", powner, str);
    }

  /* Ref count the client as being used by this owner */
  inc_nlm_client_ref(pclient);
  if(nlm_owner_Set(pkey, powner) == 1)
    {
      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_owner(powner, str);
          LogFullDebug(COMPONENT_STATE,
                       "Set NLM Owner powner=%p {%s}, refcount = %d",
                       powner, str, powner != NULL ? powner->so_refcount : 0);
        }

      return powner;
    }

  dec_nlm_client_ref(pclient);
  Mem_Free(pkey);
  Mem_Free(powner);
  return NULL;
}

void make_nlm_special_owner(state_nlm_client_t * pclient,
                            state_owner_t      * pnlm_owner)
{
  if(pnlm_owner == NULL)
    return;

  memset(pnlm_owner, 0, sizeof(*pnlm_owner));

  inc_nlm_client_ref(pclient);

  pnlm_owner->so_type     = STATE_LOCK_OWNER_NLM;
  pnlm_owner->so_refcount = 1;
  pnlm_owner->so_owner.so_nlm_owner.so_client     = pclient;
  pnlm_owner->so_owner_len                        = -1;
}