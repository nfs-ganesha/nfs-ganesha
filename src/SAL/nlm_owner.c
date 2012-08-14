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
#include "sal_functions.h"
#include "nsm.h"

hash_table_t *ht_nsm_client;
hash_table_t *ht_nlm_client;
hash_table_t *ht_nlm_owner;

/*******************************************************************************
 *
 * NSM Client Routines
 *
 ******************************************************************************/

int display_nsm_client(state_nsm_client_t *pkey, char *str)
{
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "NSM Client <NULL>");

  strtmp += sprintf(strtmp, "NSM Client %p: ", pkey);

  if(nfs_param.core_param.nsm_use_caller_name)
    strtmp += sprintf(strtmp, "caller_name=");
  else
    strtmp += sprintf(strtmp, "addr=");

  memcpy(strtmp, pkey->ssc_nlm_caller_name, pkey->ssc_nlm_caller_name_len);
  strtmp += pkey->ssc_nlm_caller_name_len;

  strtmp += sprintf(strtmp,
                    " %s refcount=%d",
                    atomic_fetch_int32_t(&pkey->ssc_monitored) ?
                      "monitored" : "unmonitored",
                    atomic_fetch_int32_t(&pkey->ssc_refcount));

  return strtmp - str;
}

int display_nsm_client_key(hash_buffer_t * pbuff, char *str)
{
  return display_nsm_client((state_nsm_client_t *)pbuff->pdata, str);
}

int display_nsm_client_val(hash_buffer_t * pbuff, char *str)
{
  return display_nsm_client((state_nsm_client_t *)pbuff->pdata, str);
}

int compare_nsm_client(state_nsm_client_t *pclient1,
                       state_nsm_client_t *pclient2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nsm_client(pclient1, str1);
      display_nsm_client(pclient2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(pclient1 == NULL || pclient2 == NULL)
    return 1;

  if(pclient1 == pclient2)
    return 0;

  if(!nfs_param.core_param.nsm_use_caller_name)
    {
      if(cmp_sockaddr(&pclient1->ssc_client_addr, &pclient2->ssc_client_addr, IGNORE_PORT) == 0)
        return 1;
      return 0;
    }

  if(pclient1->ssc_nlm_caller_name_len != pclient2->ssc_nlm_caller_name_len)
    return 1;

  return memcmp(pclient1->ssc_nlm_caller_name,
                pclient2->ssc_nlm_caller_name,
                pclient1->ssc_nlm_caller_name_len);
}

int compare_nsm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nsm_client((state_nsm_client_t *)buff1->pdata,
                            (state_nsm_client_t *)buff2->pdata);

}                               /* compare_nsm_client */

uint32_t nsm_client_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned long        res;
  state_nsm_client_t * pkey = (state_nsm_client_t *)buffclef->pdata;

  if(nfs_param.core_param.nsm_use_caller_name)
    {
      unsigned int sum = 0;
      unsigned int i;

      /* Compute the sum of all the characters */
      for(i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
        sum +=(unsigned char) pkey->ssc_nlm_caller_name[i];

      res = (unsigned long) sum +
            (unsigned long) pkey->ssc_nlm_caller_name_len;
    }
  else
    {
      res = hash_sockaddr(&pkey->ssc_client_addr, IGNORE_PORT) % p_hparam->index_size;
    }

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);
}                               /* nsm_client_value_hash_func */

uint64_t nsm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned long        res;
  state_nsm_client_t * pkey = (state_nsm_client_t *)buffclef->pdata;

  if(nfs_param.core_param.nsm_use_caller_name)
    {
      unsigned int sum = 0;
      unsigned int i;

      /* Compute the sum of all the characters */
      for(i = 0; i < pkey->ssc_nlm_caller_name_len; i++)
        sum +=(unsigned char) pkey->ssc_nlm_caller_name[i];

      res = (unsigned long) sum +
            (unsigned long) pkey->ssc_nlm_caller_name_len;
    }
  else
    {
      res = hash_sockaddr(&pkey->ssc_client_addr, IGNORE_PORT);
    }

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}                               /* nsm_client_rbt_hash_func */

/*******************************************************************************
 *
 * NLM Client Routines
 *
 ******************************************************************************/

int display_nlm_client(state_nlm_client_t *pkey, char *str)
{
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "NLM Client <NULL>");

  strtmp += sprintf(strtmp, "NLM Client %p: {", pkey);
  strtmp += display_nsm_client(pkey->slc_nsm_client, strtmp);
  strtmp += sprintf(strtmp, "} caller_name=");
  memcpy(strtmp, pkey->slc_nlm_caller_name, pkey->slc_nlm_caller_name_len);
  strtmp += pkey->slc_nlm_caller_name_len;
  strtmp += sprintf(strtmp, " type=%s",
                    xprt_type_to_str(pkey->slc_client_type));
  strtmp += sprintf(strtmp, " refcount=%d",
                    atomic_fetch_int32_t(&pkey->slc_refcount));

  return strtmp - str;
}

int display_nlm_client_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client(pbuff->pdata, str);
}

int display_nlm_client_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client(pbuff->pdata, str);
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

  if(compare_nsm_client(pclient1->slc_nsm_client, pclient2->slc_nsm_client) != 0)
    return 1;

  if(pclient1->slc_client_type != pclient2->slc_client_type)
    return 1;

  if(pclient1->slc_nlm_caller_name_len != pclient2->slc_nlm_caller_name_len)
    return 1;

  return memcmp(pclient1->slc_nlm_caller_name,
                pclient2->slc_nlm_caller_name,
                pclient1->slc_nlm_caller_name_len);
}

int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_client(buff1->pdata, buff2->pdata);

}                               /* compare_nlm_client */

uint32_t nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t    * buffclef)
{
  uint32_t sum = 0;
  unsigned int i;
  unsigned long res;
  state_nlm_client_t *pkey = buffclef->pdata;

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

uint64_t nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_nlm_client_t *pkey = buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->slc_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->slc_nlm_caller_name[i];

  res = (unsigned long) sum +
        (unsigned long) pkey->slc_nlm_caller_name_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}                               /* nlm_client_rbt_hash_func */
 
/*******************************************************************************
 *
 * NLM Owner Routines
 *
 ******************************************************************************/

int display_nlm_owner(state_owner_t *pkey, char *str)
{
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "STATE_LOCK_OWNER_NLM <NULL>");

  strtmp += sprintf(strtmp, "STATE_LOCK_OWNER_NLM %p: {", pkey);

  strtmp += display_nlm_client(pkey->so_owner.so_nlm_owner.so_client, strtmp);

  strtmp += sprintf(strtmp, "} oh=");

  strtmp += DisplayOpaqueValue(pkey->so_owner_val, pkey->so_owner_len, strtmp);

  strtmp += sprintf(strtmp, " svid=%d",
                    pkey->so_owner.so_nlm_owner.so_nlm_svid);
  strtmp += sprintf(strtmp, " refcount=%d",
                    atomic_fetch_int32_t(&pkey->so_refcount));

  return strtmp - str;
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

uint32_t nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
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

uint64_t nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
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
int Init_nlm_hash(void)
{

  if((ht_nsm_client =
      HashTable_Init(&nfs_param.nsm_client_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NSM Client cache");
      return -1;
    }

  if((ht_nlm_client
      = HashTable_Init(&nfs_param.nlm_client_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NLM Client cache");
      return -1;
    }

  if((ht_nlm_owner = HashTable_Init(&nfs_param.nlm_owner_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NLM Owner cache");
      return -1;
    }

  return 0;
}                               /* Init_nlm_hash */

/*******************************************************************************
 *
 * NSM Client Routines
 *
 ******************************************************************************/

void inc_nsm_client_ref(state_nsm_client_t *pclient)
{
  atomic_inc_int32_t(&pclient->ssc_refcount);
}

void free_nsm_client(state_nsm_client_t *pclient)
{
  if(pclient->ssc_nlm_caller_name != NULL)
    gsh_free(pclient->ssc_nlm_caller_name);

  pthread_mutex_destroy(&pclient->ssc_mutex);

  gsh_free(pclient);
}

void dec_nsm_client_ref(state_nsm_client_t *pclient)
{
  char              str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch latch;
  hash_error_t      rc;
  hash_buffer_t     buffkey;
  hash_buffer_t     old_value;
  hash_buffer_t     old_key;
  int32_t           refcount;

  if(isDebug(COMPONENT_STATE))
    display_nsm_client(pclient, str);

  refcount = atomic_dec_int32_t(&pclient->ssc_refcount);

  if(refcount > 0)
    {
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount now=%"PRId32" {%s}",
                   refcount, str);

      return;
    }

  LogFullDebug(COMPONENT_STATE,
               "Try to remove {%s}",
               str);

  buffkey.pdata = pclient;
  buffkey.len   = sizeof(*pclient);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(ht_nsm_client,
                          &buffkey,
                          &old_value,
                          TRUE,
                          &latch);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_nsm_client, &latch);

      display_nsm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  refcount = atomic_fetch_int32_t(&pclient->ssc_refcount);

  if(refcount > 0)
    {
      LogDebug(COMPONENT_STATE,
               "Did not release refcount now=%"PRId32" {%s}",
               refcount, str);

      HashTable_ReleaseLatched(ht_nsm_client, &latch);

      return;
    }

  /* use the key to delete the entry */
  rc = HashTable_DeleteLatched(ht_nsm_client,
                               &buffkey,
                               &latch,
                               &old_key,
                               &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_nsm_client, &latch);

      display_nsm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not remove {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  LogFullDebug(COMPONENT_STATE,
               "Free {%s}",
               str);

  nsm_unmonitor(old_value.pdata);
  free_nsm_client(old_value.pdata);
}

state_nsm_client_t *get_nsm_client(care_t    care,
                                   SVCXPRT * xprt,
                                   char    * caller_name)
{
  state_nsm_client_t   key;
  state_nsm_client_t * pclient;
  char                 sock_name[SOCK_NAME_MAX];
  char                 str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch    latch;
  hash_error_t         rc;
  hash_buffer_t        buffkey;
  hash_buffer_t        buffval;

  if(caller_name == NULL)
    return NULL;

  memset(&key, 0, sizeof(key));

  if(nfs_param.core_param.nsm_use_caller_name)
    {
      key.ssc_nlm_caller_name_len = strlen(caller_name);

      if(key.ssc_nlm_caller_name_len > LM_MAXSTRLEN)
        {
          return NULL;
        }

      key.ssc_nlm_caller_name = caller_name;
    }
  else if(xprt == NULL)
    {
      int rc = ipstring_to_sockaddr(caller_name, &key.ssc_client_addr);
      if(rc != 0)
        {
          LogEvent(COMPONENT_STATE,
                   "Error %s, converting caller_name %s to an ipaddress",
                   gai_strerror(rc), caller_name);

          return NULL;
        }

      key.ssc_nlm_caller_name_len = strlen(caller_name);

      if(key.ssc_nlm_caller_name_len > LM_MAXSTRLEN)
        {
          return NULL;
        }

      key.ssc_nlm_caller_name = caller_name;
    }
  else
    {
      key.ssc_nlm_caller_name = sock_name;

      if(copy_xprt_addr(&key.ssc_client_addr, xprt) == 0)
        {
          LogCrit(COMPONENT_STATE,
                  "Error converting caller_name %s to an ipaddress",
                  caller_name);

          return NULL;
        }

      if(sprint_sockip(&key.ssc_client_addr,
                       key.ssc_nlm_caller_name,
                       SOCK_NAME_MAX) == 0)
        {
          LogCrit(COMPONENT_STATE,
                  "Error converting caller_name %s to an ipaddress",
                  caller_name);

          return NULL;
        }

      key.ssc_nlm_caller_name_len = strlen(key.ssc_nlm_caller_name);
    }

  
  if(isFullDebug(COMPONENT_STATE))
    {
      display_nsm_client(&key, str);
      LogFullDebug(COMPONENT_STATE,
                   "Find {%s}", str);
    }

  buffkey.pdata = &key;
  buffkey.len   = sizeof(key);

  rc = HashTable_GetLatch(ht_nsm_client,
                          &buffkey,
                          &buffval,
                          TRUE,
                          &latch);

  /* If we found it, return it */
  if(rc == HASHTABLE_SUCCESS)
    {
      pclient = buffval.pdata;

      /* Return the found NSM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          display_nsm_client(pclient, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found {%s}",
                       str);
        }

      /* Increment refcount under hash latch.
       * This prevents dec ref from removing this entry from hash if a race
       * occurs.
       */
      inc_nsm_client_ref(pclient);

      HashTable_ReleaseLatched(ht_nsm_client, &latch);

      if(care == CARE_MONITOR && !nsm_monitor(pclient))
          {
            dec_nsm_client_ref(pclient);
            pclient = NULL;
          }

      return pclient;
    }

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      display_nsm_client(&key, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return NULL;
    }

  /* Not found, but we don't care, return NULL */
  if(care == CARE_NOT)
    {
      /* Return the found NSM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          display_nsm_client(&key, str);
          LogFullDebug(COMPONENT_STATE,
                       "Ignoring {%s}",
                       str);
        }

      HashTable_ReleaseLatched(ht_nsm_client, &latch);

      return NULL;
    }

  pclient = gsh_malloc(sizeof(*pclient));

  if(pclient == NULL)
    {
      display_nsm_client(&key, str);
      LogCrit(COMPONENT_STATE,
              "No memory for {%s}",
              str);

      return NULL;
    }

  /* Copy everything over */
  memcpy(pclient, &key, sizeof(key));

  if(pthread_mutex_init(&pclient->ssc_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the created client */
      display_nsm_client(&key, str);
      LogCrit(COMPONENT_STATE,
              "Could not init mutex for {%s}",
              str);

      gsh_free(pclient);
      return NULL;
    }

  pclient->ssc_nlm_caller_name = gsh_strdup(key.ssc_nlm_caller_name);

  if(pclient->ssc_nlm_caller_name == NULL)
    {
      /* Discard the created client */
      free_nsm_client(pclient);
      return NULL;
    }

  init_glist(&pclient->ssc_lock_list);
  init_glist(&pclient->ssc_share_list);
  pclient->ssc_refcount = 1;

  if(isFullDebug(COMPONENT_STATE))
    {
      display_nsm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "New {%s}", str);
    }

  buffkey.pdata = pclient;
  buffkey.len   = sizeof(*pclient);
  buffval.pdata = pclient;
  buffval.len   = sizeof(*pclient);

  rc = HashTable_SetLatched(ht_nsm_client,
                            &buffval,
                            &buffval,
                            &latch,
                            FALSE,
                            NULL,
                            NULL);

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_SUCCESS)
    {
      display_nsm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, inserting {%s}",
              hash_table_err_to_str(rc), str);

      free_nsm_client(pclient);

      return NULL;
    }

  if(care != CARE_MONITOR || nsm_monitor(pclient))
    return pclient;

  /* Failed to monitor, release client reference
   * and almost certainly remove it from the hash table.
   */
  dec_nsm_client_ref(pclient);

  return NULL;
}

/*******************************************************************************
 *
 * NLM Client Routines
 *
 ******************************************************************************/

void free_nlm_client(state_nlm_client_t *pclient)
{
  if(pclient->slc_nsm_client != NULL)
    dec_nsm_client_ref(pclient->slc_nsm_client);

  if(pclient->slc_nlm_caller_name != NULL)
    gsh_free(pclient->slc_nlm_caller_name);

  gsh_free(pclient);
}

void inc_nlm_client_ref(state_nlm_client_t *pclient)
{
  atomic_inc_int32_t(&pclient->slc_refcount);
}

void dec_nlm_client_ref(state_nlm_client_t *pclient)
{
  char              str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch latch;
  hash_error_t      rc;
  hash_buffer_t     buffkey;
  hash_buffer_t     old_value;
  hash_buffer_t     old_key;
  int32_t           refcount;

  if(isDebug(COMPONENT_STATE))
    display_nlm_client(pclient, str);

  refcount = atomic_dec_int32_t(&pclient->slc_refcount);

  if(refcount > 0)
    {
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount now=%"PRId32" {%s}",
                   refcount, str);

      return;
    }

  LogFullDebug(COMPONENT_STATE,
               "Try to remove {%s}",
               str);

  buffkey.pdata = pclient;
  buffkey.len   = sizeof(*pclient);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(ht_nlm_client,
                          &buffkey,
                          &old_value,
                          TRUE,
                          &latch);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_nlm_client, &latch);

      display_nlm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  refcount = atomic_fetch_int32_t(&pclient->slc_refcount);

  if(refcount > 0)
    {
      LogDebug(COMPONENT_STATE,
               "Did not release refcount now=%"PRId32" {%s}",
               refcount, str);

      HashTable_ReleaseLatched(ht_nlm_client, &latch);

      return;
    }

  /* use the key to delete the entry */
  rc = HashTable_DeleteLatched(ht_nlm_client,
                               &buffkey,
                               &latch,
                               &old_key,
                               &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_nlm_client, &latch);

      display_nlm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not remove {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  LogFullDebug(COMPONENT_STATE,
               "Free {%s}",
               str);

  free_nlm_client(old_value.pdata);
}

state_nlm_client_t *get_nlm_client(care_t               care,
                                   SVCXPRT            * xprt,
                                   state_nsm_client_t * pnsm_client,
                                   char               * caller_name)
{
  state_nlm_client_t   key;
  state_nlm_client_t * pclient;
  char                 str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch    latch;
  hash_error_t         rc;
  hash_buffer_t        buffkey;
  hash_buffer_t        buffval;

  if(caller_name == NULL)
    return NULL;

  memset(&key, 0, sizeof(key));

  key.slc_nsm_client          = pnsm_client;
  key.slc_nlm_caller_name_len = strlen(caller_name);
  key.slc_client_type         = svc_get_xprt_type(xprt);

  if(key.slc_nlm_caller_name_len > LM_MAXSTRLEN)
    return NULL;

  key.slc_nlm_caller_name = caller_name;
  
  if(isFullDebug(COMPONENT_STATE))
    {
      display_nlm_client(&key, str);
      LogFullDebug(COMPONENT_STATE,
                   "Find {%s}", str);
    }

  buffkey.pdata = &key;
  buffkey.len   = sizeof(key);

  rc = HashTable_GetLatch(ht_nlm_client,
                          &buffkey,
                          &buffval,
                          TRUE,
                          &latch);

  /* If we found it, return it */
  if(rc == HASHTABLE_SUCCESS)
    {
      pclient = buffval.pdata;

      /* Return the found NLM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          display_nlm_client(pclient, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found {%s}",
                       str);
        }

      /* Increment refcount under hash latch.
       * This prevents dec ref from removing this entry from hash if a race
       * occurs.
       */
      inc_nlm_client_ref(pclient);

      HashTable_ReleaseLatched(ht_nlm_client, &latch);

      if(care == CARE_MONITOR && !nsm_monitor(pnsm_client))
          {
            dec_nlm_client_ref(pclient);
            pclient = NULL;
          }

      return pclient;
    }

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      display_nlm_client(&key, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return NULL;
    }

  /* Not found, but we don't care, return NULL */
  if(care == CARE_NOT)
    {
      /* Return the found NLM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          display_nlm_client(&key, str);
          LogFullDebug(COMPONENT_STATE,
                       "Ignoring {%s}",
                       str);
        }

      HashTable_ReleaseLatched(ht_nlm_client, &latch);

      return NULL;
    }

  pclient = gsh_malloc(sizeof(*pclient));

  if(pclient == NULL)
    {
      display_nlm_client(&key, str);
      LogCrit(COMPONENT_STATE,
              "No memory for {%s}",
              str);

      return NULL;
    }

  /* Copy everything over */
  memcpy(pclient, &key, sizeof(key));

  pclient->slc_nlm_caller_name = gsh_strdup(key.slc_nlm_caller_name);

  /* Take a reference to the NSM Client */
  inc_nsm_client_ref(pnsm_client);

  if(pclient->slc_nlm_caller_name == NULL)
    {
      /* Discard the created client */
      free_nlm_client(pclient);
      return NULL;
    }

  pclient->slc_refcount = 1;

  if(isFullDebug(COMPONENT_STATE))
    {
      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_STATE,
                   "New {%s}", str);
    }

  buffkey.pdata = pclient;
  buffkey.len   = sizeof(*pclient);
  buffval.pdata = pclient;
  buffval.len   = sizeof(*pclient);

  rc = HashTable_SetLatched(ht_nlm_client,
                            &buffval,
                            &buffval,
                            &latch,
                            FALSE,
                            NULL,
                            NULL);

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_SUCCESS)
    {
      display_nlm_client(pclient, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, inserting {%s}",
              hash_table_err_to_str(rc), str);

      free_nlm_client(pclient);

      return NULL;
    }

  if(care != CARE_MONITOR || nsm_monitor(pnsm_client))
    return pclient;

  /* Failed to monitor, release client reference
   * and almost certainly remove it from the hash table.
   */
  dec_nlm_client_ref(pclient);

  return NULL;
}

/*******************************************************************************
 *
 * NLM Owner Routines
 *
 ******************************************************************************/

void free_nlm_owner(state_owner_t *powner)
{
  if(powner->so_owner.so_nlm_owner.so_client != NULL)
    dec_nlm_client_ref(powner->so_owner.so_nlm_owner.so_client);
}

static void init_nlm_owner(state_owner_t * powner)
{
  inc_nlm_client_ref(powner->so_owner.so_nlm_owner.so_client);

  init_glist(&powner->so_owner.so_nlm_owner.so_nlm_shares);
}

state_owner_t *get_nlm_owner(care_t               care,
                             state_nlm_client_t * pclient, 
                             netobj             * oh,
                             uint32_t             svid)
{
  state_owner_t key;

  if(pclient == NULL || oh == NULL || oh->n_len > MAX_NETOBJ_SZ)
    return NULL;

  memset(&key, 0, sizeof(key));

  key.so_type                             = STATE_LOCK_OWNER_NLM;
  key.so_owner.so_nlm_owner.so_client     = pclient;
  key.so_owner.so_nlm_owner.so_nlm_svid   = svid;
  key.so_owner_len                        = oh->n_len;
  key.so_owner_val                        = oh->n_bytes;

  return get_state_owner(care, &key, init_nlm_owner, NULL);
}
