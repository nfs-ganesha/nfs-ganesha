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

hash_table_t *ht_9p_owner;

int display_9p_owner(struct display_buffer * dspbuf,
                     state_owner_t         * pkey)
{
  int b_left;

  if(pkey == NULL)
    return display_cat(dspbuf, "<NULL>");

  b_left = display_printf(dspbuf, "STATE_LOCK_OWNER_9P %p", pkey);

  if(b_left <= 0)
    return b_left;

  b_left = display_sockaddr(dspbuf,
                            (sockaddr_t *)&(pkey->so_owner.so_9p_owner.client_addr),
                            TRUE);

  if(b_left <= 0)
    return b_left;

  return display_printf(dspbuf, " proc_id=%u refcount=%d",
                        pkey->so_owner.so_9p_owner.proc_id,
                        atomic_fetch_int32_t(&pkey->so_refcount));
}

int display_9p_owner_key(struct display_buffer * dspbuf,
                         hash_buffer_t         * pbuff)
{
  return display_9p_owner(dspbuf, pbuff->pdata);
}

int display_9p_owner_val(struct display_buffer * dspbuf,
                         hash_buffer_t         * pbuff)
{
  return display_9p_owner(dspbuf, pbuff->pdata);
}

int compare_9p_owner(state_owner_t *powner1,
                     state_owner_t *powner2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char                  str1[LOG_BUFF_LEN / 2];
      char                  str2[LOG_BUFF_LEN / 2];
      struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
      struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

      (void) display_9p_owner(&dspbuf1, powner1);
      (void) display_9p_owner(&dspbuf2, powner2);

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

state_owner_t *get_9p_owner( struct sockaddr_storage * pclient_addr,
                             uint32_t    proc_id)
{
  state_owner_t key;

  memset(&key, 0, sizeof(key));

  key.so_type                             = STATE_LOCK_OWNER_9P;
  key.so_refcount                         = 1;
  key.so_owner.so_9p_owner.proc_id        = proc_id;

  memcpy(&key.so_owner.so_9p_owner.client_addr,
         pclient_addr,
         sizeof(*pclient_addr)); 

  return get_state_owner(CARE_ALWAYS, &key, NULL, NULL);
}
