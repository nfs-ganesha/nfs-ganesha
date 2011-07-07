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

#include <stdio.h>
#include <sys/types.h>
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nlm4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

//TODO FSF: check if can optimize by using same reference as key and value

hash_table_t *ht_nlm_owner;
hash_table_t *ht_nlm_client;

int display_nlm_client(cache_inode_nlm_client_t *pkey, char *str)
{
  char *strtmp = str;

  strtmp += sprintf(strtmp, "caller_name=");
  strncpy(strtmp, pkey->clc_nlm_caller_name, pkey->clc_nlm_caller_name_len);
  strtmp += pkey->clc_nlm_caller_name_len;
  *strtmp = '\0';

  return strlen(str);
}

int display_nlm_client_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client((cache_inode_nlm_client_t *)pbuff->pdata, str);
}

int display_nlm_client_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_client((cache_inode_nlm_client_t *)pbuff->pdata, str);
}

int compare_nlm_client(cache_inode_nlm_client_t *pclient1,
                       cache_inode_nlm_client_t *pclient2)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient1, str1);
      display_nlm_client(pclient2, str2);
      LogFullDebug(COMPONENT_NLM,
                   "compare_nlm_clients => {%s}|{%s}", str1, str2);
    }

  if(pclient1 == NULL || pclient2 == NULL)
    return 1;

  if(pclient1 == pclient2)
    return 0;

  if(pclient1->clc_nlm_caller_name_len != pclient2->clc_nlm_caller_name_len)
    return 1;

  return memcmp(pclient1->clc_nlm_caller_name,
                pclient2->clc_nlm_caller_name,
                pclient1->clc_nlm_caller_name_len);
}

int compare_nlm_client_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_client((cache_inode_nlm_client_t *)buff1->pdata,
                            (cache_inode_nlm_client_t *)buff2->pdata);

}                               /* compare_nlm_client */

unsigned long nlm_client_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_inode_nlm_client_t *pkey = (cache_inode_nlm_client_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clc_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->clc_nlm_caller_name[i];

  res = (unsigned long) sum +
        (unsigned long) pkey->clc_nlm_caller_name_len;

  LogFullDebug(COMPONENT_NLM,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nlm_clo_nlm_ohue_hash_func */

unsigned long nlm_client_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_inode_nlm_client_t *pkey = (cache_inode_nlm_client_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clc_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->clc_nlm_caller_name[i];

  res = (unsigned long) sum +
        (unsigned long) pkey->clc_nlm_caller_name_len;

  LogFullDebug(COMPONENT_NLM, "---> rbt_hash_func = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

int display_nlm_owner(cache_lock_owner_t *pkey, char *str)
{
  unsigned int i = 0;
  char *strtmp = str;

  strtmp += display_nlm_client(pkey->clo_owner.clo_nlm_owner.clo_client, str);

  strtmp += sprintf(strtmp, " oh=(%u|", pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len);

  for(i = 0; i < pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len; i++)
    {
      sprintf(strtmp, "%02x", (unsigned char)pkey->clo_owner.clo_nlm_owner.clo_nlm_oh[i]);
      strtmp += 2;
    }

  strtmp += sprintf(strtmp, ") svid=%d", pkey->clo_owner.clo_nlm_owner.clo_nlm_svid);
  return strlen(str);
}

int display_nlm_owner_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((cache_lock_owner_t *)pbuff->pdata, str);
}

int display_nlm_owner_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((cache_lock_owner_t *)pbuff->pdata, str);
}

int compare_nlm_owner(cache_lock_owner_t *powner1,
                      cache_lock_owner_t *powner2)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner1, str1);
      display_nlm_owner(powner2, str2);
      LogFullDebug(COMPONENT_NLM,
                   "compare_nlm_owners => {%s}|{%s}", str1, str2);
    }

  if(powner1 == NULL || powner2 == NULL)
    return 1;

  if(powner1 == powner2)
    return 0;

  if(compare_nlm_client(powner1->clo_owner.clo_nlm_owner.clo_client,
                        powner2->clo_owner.clo_nlm_owner.clo_client) != 0)
    return 1;

  /* Handle special owner that matches any lock owner with the same nlm client */
  if(powner1->clo_owner.clo_nlm_owner.clo_nlm_oh_len == -1 ||
     powner2->clo_owner.clo_nlm_owner.clo_nlm_oh_len == -1)
    return 0;

  if(powner1->clo_owner.clo_nlm_owner.clo_nlm_svid !=
     powner2->clo_owner.clo_nlm_owner.clo_nlm_svid)
    return 1;

  if(powner1->clo_owner.clo_nlm_owner.clo_nlm_oh_len !=
     powner2->clo_owner.clo_nlm_owner.clo_nlm_oh_len)
    return 1;

  return memcmp(powner1->clo_owner.clo_nlm_owner.clo_nlm_oh,
                powner2->clo_owner.clo_nlm_owner.clo_nlm_oh,
                powner1->clo_owner.clo_nlm_owner.clo_nlm_oh_len);
}

int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_owner((cache_lock_owner_t *)buff1->pdata,
                           (cache_lock_owner_t *)buff2->pdata);

}                               /* compare_nlm_owner */

unsigned long nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_lock_owner_t *pkey = (cache_lock_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len; i++)
    sum += (unsigned char)pkey->clo_owner.clo_nlm_owner.clo_nlm_oh[i];

  res = (unsigned long) (pkey->clo_owner.clo_nlm_owner.clo_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len;

  LogFullDebug(COMPONENT_NLM,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nlm_clo_nlm_ohue_hash_func */

unsigned long nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_lock_owner_t *pkey = (cache_lock_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len; i++)
    sum += (unsigned char)pkey->clo_owner.clo_nlm_owner.clo_nlm_oh[i];

  res = (unsigned long) (pkey->clo_owner.clo_nlm_owner.clo_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len;

  LogFullDebug(COMPONENT_NLM, "---> rbt_hash_func = %lu", res);

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
      LogCrit(COMPONENT_NLM,
              "Cannot init NLM Client cache");
      return -1;
    }

  if((ht_nlm_owner = HashTable_Init(owner_param)) == NULL)
    {
      LogCrit(COMPONENT_NLM,
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
int nlm_client_Set(cache_inode_nlm_client_t * pkey,
                   cache_inode_nlm_client_t * pclient)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(*pkey);

      display_nlm_client_key(&buffkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "nlm_client_Set => KEY {%s}", str);
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
  cache_inode_nlm_client_t *pclient = (cache_inode_nlm_client_t *)(buffval->pdata);

  P(pclient->clc_mutex);

  pclient->clc_refcount--;
  rc = pclient->clc_refcount;

  V(pclient->clc_mutex);  

  return rc;
}

static void Hash_inc_client_ref(hash_buffer_t *buffval)
{
  cache_inode_nlm_client_t *pclient = (cache_inode_nlm_client_t *)(buffval->pdata);

  P(pclient->clc_mutex);
  pclient->clc_refcount++;
  V(pclient->clc_mutex);  
}

void inc_nlm_client_ref(cache_inode_nlm_client_t *pclient)
{
  P(pclient->clc_mutex);
  pclient->clc_refcount++;
  V(pclient->clc_mutex);
}

void dec_nlm_client_ref(cache_inode_nlm_client_t *pclient)
{
  bool_t remove = FALSE;

  P(pclient->clc_mutex);
  if(pclient->clc_refcount > 1)
    pclient->clc_refcount--;
  else
    remove = TRUE;
  V(pclient->clc_mutex);
  if(remove)
    {
      hash_buffer_t buffkey, old_key, old_value;

      buffkey.pdata = (caddr_t) pclient;
      buffkey.len = sizeof(*pclient);

      switch(HashTable_DelRef(ht_nlm_client, &buffkey, &old_key, &old_value, Hash_del_nlm_client_ref))
        {
          case HASHTABLE_SUCCESS:
            Mem_Free(old_key.pdata);
            Mem_Free(old_value.pdata);
            break;

          case HASHTABLE_NOT_DELETED:
            /* ref count didn't end up at 0, don't free. */
            break;

          default:
            /* some problem occurred */
            LogFullDebug(COMPONENT_NLM,
                         "HashTable_Del failed");
            break;
        }
    }
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
int nlm_client_Get_Pointer(cache_inode_nlm_client_t * pkey,
                           cache_inode_nlm_client_t * *pclient)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client_key(&buffkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "nlm_client_Get_Pointer => KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_nlm_client, &buffkey, &buffval, Hash_inc_client_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NLM,
                   "nlm_client_Get_Pointer => NOTFOUND");
      *pclient = NULL;
      return 0;
    }

  *pclient = (cache_inode_nlm_client_t *) buffval.pdata;

  LogFullDebug(COMPONENT_NLM,
               "nlm_client_Get_Pointer => FOUND");

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
  HashTable_Log(COMPONENT_NLM, ht_nlm_client);
}                               /* nlm_client_PrintAll */

cache_inode_nlm_client_t *get_nlm_client(bool_t care, const char * caller_name)
{
  cache_inode_nlm_client_t *pkey, *pclient;

  LogFullDebug(COMPONENT_NLM,
               "get_nlm_client %s", caller_name);

  if(caller_name == NULL)
    return NULL;

  pkey = (cache_inode_nlm_client_t *)Mem_Alloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));
  pkey->clc_nlm_caller_name_len = strlen(caller_name);

  if(pkey->clc_nlm_caller_name_len > LM_MAXSTRLEN)
    return NULL;

  memcpy(pkey->clc_nlm_caller_name,
         caller_name,
         pkey->clc_nlm_caller_name_len);

  
  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "get_nlm_client pkey=%s", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  if(nlm_client_Get_Pointer(pkey, &pclient) == 1 || !care)
    {
      /* Discard the key we created and return the found NLM Client */
      Mem_Free(pkey);

      if(isFullDebug(COMPONENT_NLM))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_client(pclient, str);
          LogFullDebug(COMPONENT_NLM,
                       "get_nlm_client found pclient=%s", str);
        }

      return pclient;
    }

  pclient = (cache_inode_nlm_client_t *)Mem_Alloc(sizeof(*pkey));

  /* Copy everything over */
  *pclient = *pkey;
  init_glist(&pclient->clc_lock_list);

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_NLM,
                   "get_nlm_client new pclient=%s", str);
    }

  if(pthread_mutex_init(&pclient->clc_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the key and created owner */
      Mem_Free(pkey);
      Mem_Free(pclient);
      return NULL;
    }

  if(nlm_client_Set(pkey, pclient) == 1)
    return pclient;

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
int nlm_owner_Set(cache_lock_owner_t * pkey,
                  cache_lock_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(*pkey);

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "nlm_owner_Set => KEY {%s}", str);
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
  cache_lock_owner_t *powner = (cache_lock_owner_t *)(buffval->pdata);

  P(powner->clo_mutex);

  powner->clo_refcount--;
  rc = powner->clo_refcount;

  V(powner->clo_mutex);  

  return rc;
}

static void Hash_inc_owner_ref(hash_buffer_t *buffval)
{
  cache_lock_owner_t *powner = (cache_lock_owner_t *)(buffval->pdata);

  P(powner->clo_mutex);
  powner->clo_refcount++;
  V(powner->clo_mutex);  
}

void inc_nlm_owner_ref(cache_lock_owner_t *powner)
{
  P(powner->clo_mutex);
  powner->clo_refcount++;
  V(powner->clo_mutex);
}

void dec_nlm_owner_ref(cache_lock_owner_t *powner)
{
  bool_t remove = FALSE;

  P(powner->clo_mutex);
  if(powner->clo_refcount > 1)
    powner->clo_refcount--;
  else
    remove = TRUE;
  V(powner->clo_mutex);
  if(remove)
    {
      hash_buffer_t buffkey, old_key, old_value;

      buffkey.pdata = (caddr_t) powner;
      buffkey.len = sizeof(*powner);

      switch(HashTable_DelRef(ht_nlm_owner, &buffkey, &old_key, &old_value, Hash_del_nlm_owner_ref))
        {
          case HASHTABLE_SUCCESS:
            dec_nlm_client_ref(powner->clo_owner.clo_nlm_owner.clo_client);
            Mem_Free(old_key.pdata);
            Mem_Free(old_value.pdata);
            break;

          case HASHTABLE_NOT_DELETED:
            /* ref count didn't end up at 0, don't free. */
            break;

          default:
            /* some problem occurred */
            LogFullDebug(COMPONENT_NLM,
                         "HashTable_Del failed");
            break;
        }
    }
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
int nlm_owner_Get_Pointer(cache_lock_owner_t * pkey,
                          cache_lock_owner_t * *powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "nlm_owner_Get_Pointer => KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_nlm_owner, &buffkey, &buffval, Hash_inc_owner_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NLM,
                   "nlm_owner_Get_Pointer => NOTFOUND");
      return 0;
    }

  *powner = (cache_lock_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_NLM,
               "nlm_owner_Get_Pointer => FOUND");

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
  HashTable_Log(COMPONENT_NLM, ht_nlm_owner);
}                               /* nlm_owner_PrintAll */

cache_lock_owner_t *get_nlm_owner(bool_t                     care,
                                  cache_inode_nlm_client_t * pclient, 
                                  netobj                   * oh,
                                  uint32_t                   svid)
{
  cache_lock_owner_t *pkey, *powner;

  if(pclient == NULL || oh == NULL || oh->n_len > MAX_NETOBJ_SZ)
    return NULL;

  pkey = (cache_lock_owner_t *)Mem_Alloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));

  inc_nlm_client_ref(pclient);

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_client(pclient, str);
      LogFullDebug(COMPONENT_NLM,
                   "get_nlm_owner pclient=%s", str);
    }

  pkey->clo_type     = CACHE_LOCK_OWNER_NLM;
  pkey->clo_refcount = 1;
  pkey->clo_owner.clo_nlm_owner.clo_client     = pclient;
  pkey->clo_owner.clo_nlm_owner.clo_nlm_svid   = svid;
  pkey->clo_owner.clo_nlm_owner.clo_nlm_oh_len = oh->n_len;
  memcpy(pkey->clo_owner.clo_nlm_owner.clo_nlm_oh,
         oh->n_bytes,
         oh->n_len);

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(pkey, str);
      LogFullDebug(COMPONENT_NLM,
                   "get_nlm_owner pkey=%s", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  if(nlm_owner_Get_Pointer(pkey, &powner) == 1 || !care)
    {
      /* Discard the key we created and return the found NLM Owner */
      Mem_Free(pkey);

      if(isFullDebug(COMPONENT_NLM))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nlm_owner(powner, str);
          LogFullDebug(COMPONENT_NLM,
                       "get_nlm_owner new powner=%s", str);
        }

      return powner;
    }
    
  powner = (cache_lock_owner_t *)Mem_Alloc(sizeof(*pkey));

  /* Copy everything over */
  *powner = *pkey;
  init_glist(&powner->clo_lock_list);

  if(pthread_mutex_init(&powner->clo_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the key and created owner */
      Mem_Free(pkey);
      Mem_Free(powner);
      return NULL;
    }

  if(isFullDebug(COMPONENT_NLM))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(powner, str);
      LogFullDebug(COMPONENT_NLM,
                   "get_nlm_owner new powner=%s", str);
    }

  /* Ref count the client as being used by this owner */
  inc_nlm_client_ref(pclient);
  if(nlm_owner_Set(pkey, powner) == 1)
    return powner;

  Mem_Free(pkey);
  Mem_Free(powner);
  return NULL;
}

void make_nlm_special_owner(cache_inode_nlm_client_t * pclient,
                            cache_lock_owner_t       * pnlm_owner)
{
  if(pnlm_owner == NULL)
    return;

  memset(pnlm_owner, 0, sizeof(*pnlm_owner));

  inc_nlm_client_ref(pclient);

  pnlm_owner->clo_type     = CACHE_LOCK_OWNER_NLM;
  pnlm_owner->clo_refcount = 1;
  pnlm_owner->clo_owner.clo_nlm_owner.clo_client     = pclient;
  pnlm_owner->clo_owner.clo_nlm_owner.clo_nlm_oh_len = -1;
}