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

hash_table_t *ht_nlm_owner;

int display_nlm_owner(cache_inode_nlm_owner_t *pkey, char *str)
{
  unsigned int i = 0;
  char *strtmp = str;

  strtmp += sprintf(strtmp, "caller_name=");
  strncpy(strtmp, pkey->clo_nlm_caller_name, pkey->clo_nlm_caller_name_len);
  strtmp += pkey->clo_nlm_caller_name_len;

  strtmp += sprintf(strtmp, " oh=(%u|", pkey->clo_nlm_oh_len);

  for(i = 0; i < pkey->clo_nlm_oh_len; i++)
    {
      sprintf(strtmp, "%02x", (unsigned char)pkey->clo_nlm_oh[i]);
      strtmp += 2;
    }

  strtmp += sprintf(strtmp, ") svid=%d", pkey->clo_nlm_svid);
  return strlen(str);
}

int display_nlm_owner_key(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((cache_inode_nlm_owner_t *)pbuff->pdata, str);
}

int display_nlm_owner_val(hash_buffer_t * pbuff, char *str)
{
  return display_nlm_owner((cache_inode_nlm_owner_t *)pbuff->pdata, str);
}

int compare_nlm_owner(cache_inode_nlm_owner_t *pkey1,
                      cache_inode_nlm_owner_t *pkey2)
{
  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_nlm_owner(pkey1, str1);
      display_nlm_owner(pkey2, str2);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
                   "compare_nlm_owners => {%s}|{%s}", str1, str2);
    }

  if(pkey1 == NULL || pkey2 == NULL)
    return 1;

  if(pkey1->clo_nlm_svid != pkey2->clo_nlm_svid)
    return 1;

  if(pkey1->clo_nlm_oh_len != pkey2->clo_nlm_oh_len)
    return 1;

  if(memcmp(pkey1->clo_nlm_oh,
            pkey2->clo_nlm_oh,
            pkey1->clo_nlm_oh_len) != 0)
    return 1;

  if(pkey1->clo_nlm_caller_name_len != pkey2->clo_nlm_caller_name_len)
    return 1;

  return memcmp(pkey1->clo_nlm_caller_name,
                pkey2->clo_nlm_caller_name,
                pkey1->clo_nlm_caller_name_len);
}

int compare_nlm_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_nlm_owner((cache_inode_nlm_owner_t *)buff1->pdata,
                           (cache_inode_nlm_owner_t *)buff2->pdata);

}                               /* compare_nlm_owner */

unsigned long nlm_owner_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_inode_nlm_owner_t *pkey = (cache_inode_nlm_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_nlm_oh_len; i++)
    sum += (unsigned char)pkey->clo_nlm_oh[i];

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->clo_nlm_caller_name[i];

  res = (unsigned long) (pkey->clo_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->clo_nlm_oh_len;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* nlm_clo_nlm_ohue_hash_func */

unsigned long nlm_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  cache_inode_nlm_owner_t *pkey = (cache_inode_nlm_owner_t *)buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_nlm_oh_len; i++)
    sum += (unsigned char)pkey->clo_nlm_oh[i];

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->clo_nlm_caller_name_len; i++)
    sum +=(unsigned char) pkey->clo_nlm_caller_name[i];

  res = (unsigned long) (pkey->clo_nlm_svid) +
        (unsigned long) sum +
        (unsigned long) pkey->clo_nlm_oh_len;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "---> rbt_hash_func = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

/**
 *
 * nfs4_Init_nlm_owner: Init the hashtable for NLM Owner cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs4_Init_nlm_owner(nlm_owner_parameter_t param)
{

  if((ht_nlm_owner = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_OPEN_OWNER_HASH,
              "NFS STATE_ID: Cannot init NLM Owner cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_nlm_owner */

/**
 * nlm_owner_Set
 * 
 *
 * This routine sets a open owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Set(cache_inode_nlm_owner_t * pkey,
                  cache_inode_nlm_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(cache_inode_nlm_owner_t);

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
                   "nlm_owner_Set => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(cache_inode_nlm_owner_t);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(cache_inode_nlm_owner_t);

  if(HashTable_Test_And_Set
     (ht_nlm_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nlm_owner_Set */

/**
 *
 * nlm_owner_Get
 *
 * This routine gets open owner from the openowner's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Get(cache_inode_nlm_owner_t * pkey,
                  cache_inode_nlm_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(cache_inode_nlm_owner_t);

  if(HashTable_Get(ht_nlm_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    return 0;

  memcpy((char *)powner, buffval.pdata, sizeof(cache_inode_nlm_owner_t));

  return 1;
}                               /* nlm_owner_Get */

/**
 *
 * nlm_owner_Get_Pointer
 *
 * This routine gets a pointer to an open owner from the open owners's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Get_Pointer(cache_inode_nlm_owner_t * pkey,
                          cache_inode_nlm_owner_t * *powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(cache_inode_nlm_owner_t);

      display_nlm_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
                   "nlm_owner_Get_Pointer => KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(cache_inode_nlm_owner_t);

  if(HashTable_Get(ht_nlm_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
                   "nlm_owner_Get_Pointer => NOTFOUND");
      return 0;
    }

  *powner = (cache_inode_nlm_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH,
               "nlm_owner_Get_Pointer => FOUND");

  return 1;
}                               /* nlm_owner_Get_Pointer */

/**
 * 
 * nlm_owner_Update
 *
 * This routine updates a open owner from the open owners's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [IN] new state
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nlm_owner_Update(cache_inode_nlm_owner_t * pkey,
                     cache_inode_nlm_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(cache_inode_nlm_owner_t);

  if(HashTable_Get(ht_nlm_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    return 0;

  memcpy(buffval.pdata, powner, sizeof(cache_inode_nlm_owner_t));

  return 1;
}                               /* nlm_owner_Update */

/**
 *
 * nlm_owner_Del
 *
 * This routine removes a open owner from the nlm_owner's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nlm_owner_Del(cache_inode_nlm_owner_t * pkey)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(cache_inode_nlm_owner_t);

  if(HashTable_Del(ht_nlm_owner, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nlm_owner_Del */

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
  HashTable_Log(COMPONENT_OPEN_OWNER_HASH, ht_nlm_owner);
}                               /* nlm_owner_PrintAll */

int convert_nlm_owner(const char              * caller_name,
                      netobj                  * oh,
                      uint32_t                  svid,
                      cache_inode_nlm_owner_t * pnlm_owner)
{
  if(caller_name == NULL || oh == NULL || pnlm_owner == NULL)
    return 0;

  memset(pnlm_owner, 0, sizeof(*pnlm_owner));
  pnlm_owner->clo_nlm_caller_name_len = strlen(caller_name);

  if(pnlm_owner->clo_nlm_caller_name_len > LM_MAXSTRLEN ||
     oh->n_len > LM_MAXSTRLEN)
    return 0;

  pnlm_owner->clo_nlm_svid = svid;
  pnlm_owner->clo_nlm_oh_len = oh->n_len;
  memcpy(pnlm_owner->clo_nlm_oh,
         oh->n_bytes,
         oh->n_len);
  memcpy(pnlm_owner->clo_nlm_caller_name,
         caller_name,
         pnlm_owner->clo_nlm_caller_name_len);

  return 1;
}                               /* nfs_convert_nlm_owner */
