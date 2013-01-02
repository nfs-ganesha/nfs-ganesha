/*
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
 *
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file 9p_owner.c
 * @brief Management of the 9P owner cache.
 */

#include "config.h"

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "log.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "sal_functions.h"

/**
 * @todo FSF: check if can optimize by using same reference as key and
 * value */

/**
 * @brief Hash table for 9p owners
 */
hash_table_t *ht_9p_owner;

/**
 * @brief Display a 9p owner
 *
 * @param[in]  key The 9P owner
 * @param[out] str Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner(state_owner_t *key, char *str)
{
  char *strtmp = str;

  if(key == NULL)
    return sprintf(str, "<NULL>");

  strtmp += sprintf(strtmp, "STATE_LOCK_OWNER_9P %p", key);
  strtmp += sprint_sockaddr( (sockaddr_t *)&(key->so_owner.so_9p_owner.client_addr),
                             strtmp, 
                             sizeof( key->so_owner.so_9p_owner.client_addr ) ) ;

  strtmp += sprintf(strtmp, " proc_id=%u", key->so_owner.so_9p_owner.proc_id);
 
  strtmp += sprintf(strtmp, " refcount=%d", key->so_refcount);

  return strtmp - str;
}

/**
 * @brief Display owner from hash key
 *
 * @param[in]  buff Buffer pointing to owner
 * @param[out] str  Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner_key(struct gsh_buffdesc *buff, char *str)
{
  return display_9p_owner(buff->addr, str);
}

/**
 * @brief Display owner from hash value
 *
 * @param[in]  buff Buffer pointing to owner
 * @param[out] str  Output buffer
 *
 * @return Length of display string.
 */

int display_9p_owner_val(struct gsh_buffdesc *buff, char *str)
{
  return display_9p_owner(buff->addr, str);
}

/**
 * @brief Compare two 9p owners
 *
 * @param[in] owner1 One owner
 * @param[in] owner2 Another owner
 *
 * @retval 1 if they differ.
 * @retval 0 if they're identical.
 */

int compare_9p_owner(state_owner_t *owner1,
                     state_owner_t *owner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str1[HASHTABLE_DISPLAY_STRLEN];
      char str2[HASHTABLE_DISPLAY_STRLEN];

      display_9p_owner(owner1, str1);
      display_9p_owner(owner2, str2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(owner1 == NULL || owner2 == NULL)
    return 1;

  if(owner1 == owner2)
    return 0;

  if(owner1->so_owner.so_9p_owner.proc_id !=
     owner2->so_owner.so_9p_owner.proc_id)
    return 1;
#if 0
  if( memcmp(&owner1->so_owner.so_9p_owner.client_addr, 
             &owner2->so_owner.so_9p_owner.client_addr,
              sizeof( struct sockaddr_storage ) ) )
    return 1;
#endif 

  if(owner1->so_owner_len !=
     owner2->so_owner_len)
    return 1;

  return memcmp(owner1->so_owner_val,
                owner2->so_owner_val,
                owner1->so_owner_len);
}

/**
 * @brief Compare two keys in the 9p owner hash table
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another key
 *
 * @retval 1 if they differ.
 * @retval 0 if they're the same.
 */

int compare_9p_owner_key(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
  return compare_9p_owner(buff1->addr,
                          buff2->addr);

}

/**
 * @brief Get the hash index from a 9p owner
 *
 * @param[in] hparam Hash parameters
 * @param[in] key The key to hash
 *
 * @return The hash index.
 */

uint32_t _9p_owner_value_hash_func(hash_parameter_t *hparam,
                                   struct gsh_buffdesc *key)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = key->addr;

  struct sockaddr_in * paddr = (struct sockaddr_in *)&pkey->so_owner.so_9p_owner.client_addr;

  /* Compute the sum of all the characters */
  for(i = 0; i < pkey->so_owner_len; i++)
    sum += (unsigned char)pkey->so_owner_val[i];

  res = (unsigned long) (pkey->so_owner.so_9p_owner.proc_id)  +
        (unsigned long) paddr->sin_addr.s_addr +
        (unsigned long) sum +
        (unsigned long) pkey->so_owner_len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
		 "value = %lu", res % hparam->index_size);

  return (unsigned long)(res % hparam->index_size);

}

/**
 * @brief Get the RBT hash from a 9p owner
 *
 * @param[in] hparam Hash parameters
 * @param[in] key The key to hash
 *
 * @return The RBT hash.
 */

uint64_t _9p_owner_rbt_hash_func(hash_parameter_t *hparam,
                                 struct gsh_buffdesc *key)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  state_owner_t *pkey = key->addr;

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
}

/**
 * @brief Init the hashtable for 9P Owner cache
 * 
 * @reval 0 if successful.
 * @retval -1 otherwise.
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
}

/**
 * @brief Set a 9P owner into the related hashtable
 *
 * @param[in] key   9P owner key
 * @param[in] owner 9P owner
 *
 * @retval 1 if ok.
 * @retval 0 otherwise.
 *
 */
int _9p_owner_Set(state_owner_t *key,
                  state_owner_t *owner)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      buffkey.addr = key;
      buffkey.len = sizeof(*key);

      display_9p_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  buffkey.addr = key;
  buffkey.len = sizeof(*key);

  buffval.addr = owner;
  buffval.len = sizeof(*owner);

  if(HashTable_Test_And_Set
     (ht_9p_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}

/**
 * @brief Remove an owner from the 9p owner hash
 *
 * @param[in] owner Owner to remove
 * @param[in] str   String inserted into logs, for debugging
 */

void remove_9p_owner(state_owner_t *owner,
                     const char *str)
{
  struct gsh_buffdesc buffkey, old_key, old_value;

  buffkey.addr = owner;
  buffkey.len = sizeof(*owner);

  switch(HashTable_DelRef(ht_9p_owner, &buffkey, &old_key, &old_value, Hash_dec_state_owner_ref))
    {
      case HASHTABLE_SUCCESS:
        LogFullDebug(COMPONENT_STATE,
                     "Free %s size %llx",
                     str, (unsigned long long) old_value.len);
        if(isFullDebug(COMPONENT_MEMLEAKS))
          {
            memset(old_key.addr, 0, old_key.len);
            memset(old_value.addr, 0, old_value.len);
          }
        gsh_free(old_key.addr);
        gsh_free(old_value.addr);
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
 * @brief Get a pointer to a 9P owner from the 9p_owner hashtable
 *
 * @param[in]  key   Owner key
 * @param[out] owner Found owner
 *
 * @retval 1 if ok.
 * @retval 0 otherwise.
 */
static int _9p_owner_Get_Pointer(state_owner_t  *key,
                                 state_owner_t **owner)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;

  *owner = NULL; // in case we dont find it, return NULL
  buffkey.addr = key;
  buffkey.len = sizeof(*key);

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

  *owner = buffval.addr;

  LogFullDebug(COMPONENT_STATE,
               "FOUND");

  return 1;
}                               /* _9p_owner_Get_Pointer */

/**
 * @brief Display the hashtable used to store 9P owners
 */

void _9p_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_STATE, ht_9p_owner);
}

/**
 * @brief Look up a 9p owner
 *
 * @param[in] client_addr 9p client address
 * @param[in] proc_id     Process ID of owning process
 *
 * @return The found owner or NULL.
 */

state_owner_t *get_9p_owner(struct sockaddr_storage *client_addr,
                            uint32_t proc_id)
{
  state_owner_t * pkey, *powner;

  pkey = gsh_malloc(sizeof(*pkey));
  if(pkey == NULL)
    return NULL;

  memset(pkey, 0, sizeof(*pkey));
  pkey->so_type                             = STATE_LOCK_OWNER_9P;
  pkey->so_refcount                         = 1;
  pkey->so_owner.so_9p_owner.proc_id        = proc_id;
  memcpy(&pkey->so_owner.so_9p_owner.client_addr, client_addr,
	 sizeof( struct sockaddr_storage ) ) ; 
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
/** @} */
