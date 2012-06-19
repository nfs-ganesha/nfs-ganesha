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
 * 9p_owner.c : The management of the 9P owner cache.
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
#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "sal_functions.h"

//TODO FSF: check if can optimize by using same reference as key and value

hash_table_t *ht_9p_owner;

int display_9p_owner(state_owner_t *pkey, char *str)
{
  char *strtmp = str;

  if(pkey == NULL)
    return sprintf(str, "<NULL>");

  strtmp += sprintf(strtmp, "STATE_LOCK_OWNER_9P %p", pkey);
  strtmp += sprint_sockaddr( (sockaddr_t *)&(pkey->so_owner.so_9p_owner.client_addr),
                             strtmp, 
                             sizeof( pkey->so_owner.so_9p_owner.client_addr ) ) ;

  strtmp += sprintf(strtmp, " proc_id=%u", pkey->so_owner.so_9p_owner.proc_id);
 
  strtmp += sprintf(strtmp, " refcount=%d", pkey->so_refcount);

  return strtmp - str;
}

int display_9p_owner_key(hash_buffer_t * pbuff, char *str)
{
  return display_9p_owner((state_owner_t *)pbuff->pdata, str);
}

int display_9p_owner_val(hash_buffer_t * pbuff, char *str)
{
  return display_9p_owner((state_owner_t *)pbuff->pdata, str);
}

int compare_9p_owner(state_owner_t *powner1,
                     state_owner_t *powner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_9p_owner(powner1, str1);
      display_9p_owner(powner2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(powner1 == NULL || powner2 == NULL)
    return 1;

  if(powner1 == powner2)
    return 0;

  if(powner1->so_owner.so_9p_owner.proc_id !=
     powner2->so_owner.so_9p_owner.proc_id)
    return 1;
#if 0
  if( memcmp( (char *)&powner1->so_owner.so_9p_owner.client_addr, 
              (char *)&powner2->so_owner.so_9p_owner.client_addr,
              sizeof( struct sockaddr_storage ) ) )
    return 1;
#endif 

  if(powner1->so_owner_len !=
     powner2->so_owner_len)
    return 1;

  return memcmp(powner1->so_owner_val,
                powner2->so_owner_val,
                powner1->so_owner_len);
}

int compare_9p_owner_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return compare_9p_owner((state_owner_t *)buff1->pdata,
                          (state_owner_t *)buff2->pdata);

}                               /* compare_9p_owner */

uint32_t _9p_owner_value_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = (state_owner_t *)buffclef->pdata;

  struct sockaddr_in * paddr = (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr ;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    sum += (unsigned char)pkey->so_owner_val[i];

  res = (unsigned long) (pkey->so_owner.so_9p_owner.proc_id)  +
        (unsigned long) paddr->sin_addr.s_addr +
        (unsigned long) sum +
        (unsigned long) pkey->so_owner_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               

uint64_t _9p_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                 hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = (state_owner_t *)buffclef->pdata;

  struct sockaddr_in * paddr = (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr ;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    sum += (unsigned char)pkey->so_owner_val[i];

  res = (unsigned long) (pkey->so_owner.so_9p_owner.proc_id)  +
        (unsigned long) paddr->sin_addr.s_addr +
        (unsigned long) sum +
        (unsigned long) pkey->so_owner_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}                               /* state_id_rbt_hash_func */

/**
 *
 * Init_9p_hash: Init the hashtable for 9P Owner cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int Init_9p_hash(void)
{
  if((ht_9p_owner = HashTable_Init(&nfs_param._9p_owner_hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init 9P Owner cache");
      return -1;
    }

  return 0;
}                               /* Init_9p_hash */

/**
 * _9p_owner_Set
 * 
 *
 * This routine sets a 9P owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int _9p_owner_Set(state_owner_t * pkey,
                  state_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.pdata = (caddr_t) pkey;
      buffkey.len = sizeof(*pkey);

      display_9p_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(*powner);

  if(HashTable_Test_And_Set
     (ht_9p_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* _9p_owner_Set */

void remove_9p_owner( state_owner_t        * powner,
                      const char           * str)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) powner;
  buffkey.len = sizeof(*powner);

  switch(HashTable_DelRef(ht_9p_owner, &buffkey, &old_key, &old_value, Hash_dec_state_owner_ref))
    {
      case HASHTABLE_SUCCESS:
        LogFullDebug(COMPONENT_STATE,
                     "Free %s size %llx",
                     str, (unsigned long long) old_value.len);
        if(isFullDebug(COMPONENT_MEMLEAKS))
          {
            memset(old_key.pdata, 0, old_key.len);
            memset(old_value.pdata, 0, old_value.len);
          }
        gsh_free(old_key.pdata);
        gsh_free(old_value.pdata);
        break;

      case HASHTABLE_NOT_DELETED:
        /* ref count didn't end up at 0, don't free. */
        LogDebug(COMPONENT_STATE,
                 "HashTable_DelRef didn't reduce refcount to 0 for %s",
                  str);
        break;

      default:
        /* some problem occurred */
        LogDebug(COMPONENT_STATE,
                 "HashTable_DelRef failed for %s",
                  str);
        break;
    }
}

/**
 *
 * _9p_owner_Get_Pointer
 *
 * This routine gets a pointer to an 9P owner from the 9p_owner's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
static int _9p_owner_Get_Pointer(state_owner_t  * pkey,
                                 state_owner_t ** powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  *powner = NULL; // in case we dont find it, return NULL
  buffkey.pdata = (caddr_t) pkey;
  buffkey.len = sizeof(*pkey);

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_9p_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  if(HashTable_GetRef(ht_9p_owner,
                      &buffkey,
                      &buffval,
                      Hash_inc_state_owner_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "NOTFOUND");
      return 0;
    }

  *powner = (state_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATE,
               "FOUND");

  return 1;
}                               /* _9p_owner_Get_Pointer */

/**
 * 
 *  _9p_owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the 9P owners. 
 * 
 * @return nothing (void function)
 */

void _9p_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_9p_owner);
}                               /* _9p_owner_PrintAll */

state_owner_t *get_9p_owner( struct sockaddr_storage * pclient_addr,
                             uint32_t    proc_id)
{
  state_owner_t * pkey, *powner;

  pkey = (state_owner_t *)gsh_malloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));
  pkey->so_type                             = STATE_LOCK_OWNER_9P;
  pkey->so_refcount                         = 1;
  pkey->so_owner.so_9p_owner.proc_id        = proc_id;
  memcpy( (char *)&pkey->so_owner.so_9p_owner.client_addr, (char *)pclient_addr, sizeof( struct sockaddr_storage ) ) ; 
  pkey->so_owner_len                        = 0 ;
  memset( pkey->so_owner_val, 0, NFS4_OPAQUE_LIMIT) ;
  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_9p_owner(pkey, str);

      LogFullDebug(COMPONENT_STATE,
                   "Find 9P Owner KEY {%s}", str);
    }

  /* If we found it, return it, if we don't care, return NULL */
  if(_9p_owner_Get_Pointer(pkey, &powner) == 1 )
    {
      /* Discard the key we created and return the found 9P Owner */
      gsh_free(pkey);

      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_9p_owner(powner, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found {%s}",
                       str);
        }

      return powner;
    }
    
  powner = (state_owner_t *)gsh_malloc(sizeof(*pkey));
  if(powner == NULL)
    {
      gsh_free(pkey);
      return NULL;
    }

  /* Copy everything over */
  *powner = *pkey;
  init_glist(&powner->so_lock_list);

  if(pthread_mutex_init(&powner->so_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the key and created owner */
      gsh_free(pkey);
      gsh_free(powner);
      return NULL;
    }

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      display_9p_owner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "New {%s}", str);
    }

  /* Ref count the client as being used by this owner */
  if(_9p_owner_Set(pkey, powner) == 1)
    {
      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_9p_owner(powner, str);
          LogFullDebug(COMPONENT_STATE,
                       "Set 9P Owner {%s}",
                       str);
        }

      return powner;
    }

  gsh_free(pkey);
  gsh_free(powner);
  return NULL;
}
