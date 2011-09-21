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
 * nfs_client_id.c : The management of the client id cache.
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

#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "nfs4.h"

#ifdef _APPLE
#define strnlen( s, l ) strlen( s )
#else
size_t strnlen(const char *s, size_t maxlen);
#endif

/* Hashtable used to cache the hostname, accessed by their IP addess */
hash_table_t *ht_client_id;
hash_table_t *ht_client_id_reverse;

/**
 *
 *  client_id_rbt_hash_func: computes the hash value for the entry in Client Id cache.
 * 
 * Computes the hash value for the entry in Client Id cache. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long client_id_value_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  clientid4 clientid;

  clientid = *((clientid4 *) (buffclef->pdata));

  /* Sum upper and lower 32bits fields to build the key */
  hash_func = ((unsigned long)(clientid & 0x00000000FFFFFFFFLL) +
               (unsigned long)(clientid >> 32));

  return hash_func % p_hparam->index_size;
}                               /*  client_id_value_hash_func */

/**
 *
 *  client_id_reverse_hash_func: computes the hash value for the entry in Client Id cache.
 *
 * Computes the hash value for the entry in Client Id cache. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam     [IN] hash table parameter.
 * @param buffcleff  [IN] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long client_id_value_hash_func_reverse(hash_parameter_t * p_hparam,
                                                hash_buffer_t * buffclef)
{
  unsigned long sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for(i = 0, c = ((char *)buffclef->pdata)[0]; ((char *)buffclef->pdata)[i] != '\0';
      c = ((char *)buffclef->pdata)[++i], sum += c) ;

  return (unsigned long)(sum % p_hparam->index_size);
}                               /*  client_id_reverse_value_hash_func */

unsigned int client_id_value_both_reverse( hash_parameter_t * p_hparam,
				           hash_buffer_t    * buffclef, 
				           uint32_t * phashval, uint32_t * prbtval )
{
   uint32_t h1 = 0 ;
   uint32_t h2 = 0 ;

   Lookup3_hash_buff_dual( (char *)(buffclef->pdata), strnlen( (char *)(buffclef->pdata), MAXNAMLEN ), 
                           &h1, &h2  );

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ; 

   /* Success */
   return 1 ;
} /* client_id_value_both_reverse */

/**
 *
 *  client_id_rbt_hash_func: computes the rbt value for the entry in Client Id cache.
 * 
 * Computes the rbt value for the entry in Client Id cache. In fact, it just use the address value
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long client_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                      hash_buffer_t * buffclef)
{
  unsigned long hash_func;
  clientid4 clientid;

  clientid = *((clientid4 *) (buffclef->pdata));

  /* Xor upper and lower 32bits fields to build the key */
  hash_func = ((unsigned long)(clientid & 0x00000000FFFFFFFFLL) ^
               (unsigned long)(clientid >> 32));

  return hash_func;
}                               /* client_id_rbt_hash_func */

unsigned long client_id_rbt_hash_func_reverse(hash_parameter_t * p_hparam,
                                              hash_buffer_t * buffclef)
{
  clientid4 result;

  if(nfs_client_id_compute((char *)buffclef->pdata, &result) != CLIENT_ID_SUCCESS)
    return 0;

  return ((unsigned long)(result & 0x00000000FFFFFFFFLL));
}                               /* ip_name_rbt_hash_func */

/**
 *
 * compare_client_id: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in 
 * the hashtable storing the client ids. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_client_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  clientid4 cl1 = *((clientid4 *) (buff1->pdata));
  clientid4 cl2 = *((clientid4 *) (buff2->pdata));
  return (cl1 == cl2) ? 0 : 1;
}                               /* compare_xid */

int compare_client_id_reverse(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  char *cl1 = (char *)(buff1->pdata);
  char *cl2 = (char *)(buff2->pdata);
  return strncmp(cl1, cl2, MAXNAMLEN);
}                               /* compare_xid */

/**
 *
 * display_client_id: displays the client_id stored in the buffer.
 *
 * displays the client_id stored in the buffer. This function is to be used as 'key_to_str' field in 
 * the hashtable storing the client ids. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_client_id(hash_buffer_t * pbuff, char *str)
{
  clientid4 clientid;

  clientid = *((clientid4 *) (pbuff->pdata));

  return sprintf(str, "%llu", (unsigned long long)clientid);
}                               /* display_client_id */

int display_client_id_reverse(hash_buffer_t * pbuff, char *str)
{
  return sprintf(str, "%s", (char *)(pbuff->pdata));
}                               /* display_client_id_reverse */

int display_client_id_val(hash_buffer_t * pbuff, char *str)
{
  nfs_client_id_t *precord;

  precord = (nfs_client_id_t *) (pbuff->pdata);

  return sprintf(str, "#%s#=>%llu cb_prog=%u r_addr=%s r_netid=%s",
                 precord->client_name,
                 (unsigned long long)precord->clientid,
                 precord->cb_program, precord->client_r_addr, precord->client_r_netid);
}                               /* display_client_id_val */

/**
 *
 * nfs_client_id_add: adds an entry in the duplicate requests cache.
 *
 * Adds an entry in the duplicate requests cache.
 *
 * @param clientid           [IN]    the client id used as key
 * @param client_record      [IN]    the candidate record for the client
 * @param clientid_pool      [INOUT] values pool for hash table
 *
 * @return CLIENT_ID_SUCCESS if successfull\n.
 * @return CLIENT_ID_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return CLIENT_ID_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */

int nfs_client_id_add(clientid4 clientid,
                      nfs_client_id_t client_record, struct prealloc_pool *clientid_pool)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  hash_buffer_t buffkey_reverse;
  hash_buffer_t buffdata_reverse;
  nfs_client_id_t *pnfs_client_id = NULL;
  clientid4 *pclientid = NULL;

  /* Entry to be cached */
  GetFromPool(pnfs_client_id, clientid_pool, nfs_client_id_t);

  if(pnfs_client_id == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  if((pclientid = (clientid4 *) Mem_Alloc(sizeof(clientid4))) == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  if((buffkey_reverse.pdata = (caddr_t) Mem_Alloc(MAXNAMLEN)) == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  *pclientid = clientid;
  buffkey.pdata = (caddr_t) pclientid;
  buffkey.len = sizeof(clientid);

  *pnfs_client_id = client_record;
  buffdata.pdata = (caddr_t) pnfs_client_id;
  buffdata.len = sizeof(nfs_client_id_t);

  if(HashTable_Test_And_Set
     (ht_client_id, &buffkey, &buffdata,
      HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  /* Keep information in reverse hash table */
  strncpy((char *)(buffkey_reverse.pdata), client_record.client_name, MAXNAMLEN);
  buffkey_reverse.len = MAXNAMLEN;

  buffdata_reverse.pdata = (caddr_t) pnfs_client_id;
  buffdata_reverse.len = sizeof(nfs_client_id_t);

  if(HashTable_Test_And_Set
     (ht_client_id_reverse, &buffkey_reverse, &buffdata_reverse,
      HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  if(isFullDebug(COMPONENT_CLIENT_ID_COMPUTE))
    {
      LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                   "-=-=-=-=-=-=-=-=-=-> ht_client_id ");
      HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE, ht_client_id);
      LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                   "-=-=-=-=-=-=-=-=-=-> ht_client_id_reverse ");
      HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE,ht_client_id_reverse);
    }

  return CLIENT_ID_SUCCESS;
}                               /* nfs_client_id_add */

/**
 *
 * nfs_client_id_sets: sets an entry that already exists.
 *
 * Sets an entry that already exists
 *
 * @param clientid           [IN]    the client id used as key
 * @param client_record      [IN]    the candidate record for the client
 * @param clientid_pool      [INOUT] values pool for hash table
 *
 * @return CLIENT_ID_SUCCESS if successfull\n.
 * @return CLIENT_ID_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return CLIENT_ID_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */

int nfs_client_id_set(clientid4 clientid,
                      nfs_client_id_t client_record, struct prealloc_pool *clientid_pool)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  hash_buffer_t buffkey_reverse;
  hash_buffer_t buffdata_reverse;
  nfs_client_id_t *pnfs_client_id = NULL;
  clientid4 *pclientid = NULL;

  /* Entry to be cached */
  GetFromPool(pnfs_client_id, clientid_pool, nfs_client_id_t);

  if(pnfs_client_id == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  if((pclientid = (clientid4 *) Mem_Alloc(sizeof(clientid4))) == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  if((buffkey_reverse.pdata = (caddr_t) Mem_Alloc(MAXNAMLEN)) == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  *pclientid = clientid;
  buffkey.pdata = (caddr_t) pclientid;
  buffkey.len = sizeof(clientid);

  *pnfs_client_id = client_record;
  buffdata.pdata = (caddr_t) pnfs_client_id;
  buffdata.len = sizeof(nfs_client_id_t);

  if(HashTable_Test_And_Set
     (ht_client_id, &buffkey, &buffdata,
      HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  /* Reverse hashtable */
  strncpy((char *)(buffkey_reverse.pdata), client_record.client_name, MAXNAMLEN);
  buffkey_reverse.len = MAXNAMLEN;

  buffdata_reverse.pdata = (caddr_t) pnfs_client_id;
  buffdata_reverse.len = sizeof(nfs_client_id_t);

  if(HashTable_Test_And_Set
     (ht_client_id_reverse, &buffkey_reverse, &buffdata_reverse,
      HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  return CLIENT_ID_SUCCESS;
}                               /* nfs_client_id_set */

/**
 *
 * nfs_client_id_get: Tries to get an entry for client_id cache.
 *
 * Tries to get an entry for client_id cache.
 * 
 * @param clientid      [IN]  the client id
 * @param resclientid   [OUT] the found client id
 *
 * @return the result previously set if *pstatus == CLIENT_ID_SUCCESS
 *
 */
int nfs_client_id_get(clientid4 clientid, nfs_client_id_t * client_id_res)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  nfs_client_id_t *pnfs_client_id = NULL;
  clientid4 *pclientid = &clientid;

  if(client_id_res == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) pclientid;
  buffkey.len = sizeof(clientid4);

  if(HashTable_Get(ht_client_id, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      pnfs_client_id = (nfs_client_id_t *) buffval.pdata;

      *client_id_res = *pnfs_client_id;
      status = CLIENT_ID_SUCCESS;
      if(isFullDebug(COMPONENT_CLIENT_ID_COMPUTE))
        {
          LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                       "-=-=-=-=-=-=-=-=-=-> ht_client_id ");
          HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE,ht_client_id);
          LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                       "-=-=-=-=-=-=-=-=-=-> ht_client_id_reverse ");
          HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE,ht_client_id_reverse);
        }
    }
  else
    {
      status = CLIENT_ID_NOT_FOUND;
    }

  return status;
}                               /* nfs_client_id_get */

int nfs_client_id_Get_Pointer(clientid4 clientid, nfs_client_id_t ** ppclient_id_res)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  clientid4 *pclientid = &clientid;

  if(ppclient_id_res == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) pclientid;
  buffkey.len = sizeof(clientid4);

  if(HashTable_Get(ht_client_id, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      *ppclient_id_res = (nfs_client_id_t *) buffval.pdata;

      status = CLIENT_ID_SUCCESS;
      if(isFullDebug(COMPONENT_CLIENT_ID_COMPUTE))
        {
          LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                       "-=-=-=-=-=-=-=-=-=-> ht_client_id ");
          HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE,ht_client_id);
          LogFullDebug(COMPONENT_CLIENT_ID_COMPUTE,
                       "-=-=-=-=-=-=-=-=-=-> ht_client_id_reverse ");
          HashTable_Log(COMPONENT_CLIENT_ID_COMPUTE,ht_client_id_reverse);
        }
    }
  else
    {
      status = CLIENT_ID_NOT_FOUND;
    }

  return status;
}                               /* nfs_client_id_Get_Pointer */

int nfs_client_id_get_reverse(char *key, nfs_client_id_t * client_id_res)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  nfs_client_id_t *pnfs_client_id = NULL;

  if(client_id_res == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  buffkey.pdata = (caddr_t) key;
  buffkey.len = MAXNAMLEN;

  if(HashTable_Get(ht_client_id_reverse, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      pnfs_client_id = (nfs_client_id_t *) buffval.pdata;

      *client_id_res = *pnfs_client_id;
      status = CLIENT_ID_SUCCESS;
    }
  else
    {
      status = CLIENT_ID_NOT_FOUND;
    }

  return status;
}                               /* nfs_client_id_get_reverse */

/**
 *
 * nfs_client_id_remove: Tries to remove an entry for client_id cache
 *
 * Tries to remove an entry for client_id cache.
 * 
 * @param clientid           [IN]    the clientid to be used as key
 * @param clientid_pool      [INOUT] values pool for hash table
 *
 * @return the result previously set if *pstatus == CLIENT_ID_SUCCESS
 *
 */
int nfs_client_id_remove(clientid4 clientid, struct prealloc_pool *clientid_pool)
{
  hash_buffer_t buffkey, old_key, old_key_reverse, old_value;
  nfs_client_id_t *pnfs_client_id = NULL;
  clientid4 *pclientid = NULL;

  if((pclientid = (clientid4 *) Mem_Alloc(sizeof(clientid4))) == NULL)
    return CLIENT_ID_INSERT_MALLOC_ERROR;

  *pclientid = clientid;
  buffkey.pdata = (caddr_t) pclientid;
  buffkey.len = 0;

  /* Remove entry */

  if(HashTable_Del(ht_client_id, &buffkey, &old_key, &old_value) != HASHTABLE_SUCCESS)
    {
      Mem_Free(pclientid);
      return CLIENT_ID_NOT_FOUND;
    }

  /* Remove reverse entry */
  pnfs_client_id = (nfs_client_id_t *) old_value.pdata;

  buffkey.pdata = pnfs_client_id->client_name;
  buffkey.len = MAXNAMLEN;

  if(HashTable_Del(ht_client_id_reverse, &buffkey, &old_key_reverse, &old_value) !=
     HASHTABLE_SUCCESS)
    {
      ReleaseToPool(pnfs_client_id, clientid_pool);
      Mem_Free(old_key.pdata);
      Mem_Free(pclientid);
      return CLIENT_ID_NOT_FOUND;
    }

  ReleaseToPool(pnfs_client_id, clientid_pool);
  Mem_Free(old_key_reverse.pdata);
  Mem_Free(old_key.pdata);
  Mem_Free(pclientid);

  return CLIENT_ID_SUCCESS;

}                               /* nfs_client_id_remove */

/**
 *
 * nfs_Init_client_id: Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_client_id(nfs_client_id_parameter_t param)
{
  if((ht_client_id = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Id cache");
      return -1;
    }

  return CLIENT_ID_SUCCESS;
}                               /* nfs_Init_client_id */

/**
 *
 * nfs_Init_client_id_reverse: Init the reverse hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable Client Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_client_id_reverse(nfs_client_id_parameter_t param)
{
  if((ht_client_id_reverse = HashTable_Init(param.hash_param_reverse)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS CLIENT_ID: Cannot init Client Id cache");
      return -1;
    }

  return CLIENT_ID_SUCCESS;
}                               /* nfs_Init_client_id */

int nfs_client_id_basic_compute(char *name, clientid4 * pclientid)
{
  char *str = NULL;
  char stock[MAXNAMLEN];
  unsigned int i = 0;
  unsigned int sum = 0;

  if(name == NULL || pclientid == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  strncpy(stock, name, MAXNAMLEN);

  for(str = stock, i = 0; *str != '\0'; str++, i++)
    sum += (unsigned int)(str[i] * 10 ^ i);

  *pclientid = (clientid4) sum;

  return CLIENT_ID_SUCCESS;
}                               /* nfs_client_id_basic_compute */

/** 
 * 
 * nfs_client_id_compute: computes the client id, based on the string.
 * 
 * Computes the client id, based on the string.
 *
 */

int nfs_client_id_compute(char *name, clientid4 * pclientid)
{
  char padded_name[CLIENT_ID_MAX_LEN+9]; /* +9 to avoid array bounds overflow */
  clientid4 computed_value = 0;
  unsigned int i = 0;
  unsigned int offset = 0;
  uint64_t extract = 0;
  uint64_t sum = 0;
  uint64_t i1;
  uint64_t i2;
  uint64_t i3;
  uint64_t i4;
  uint64_t i5;
  uint64_t i6;
  uint64_t i7;
  uint64_t i8;
  uint64_t i9;
  uint64_t l;

  if(name == NULL || pclientid == NULL)
    return CLIENT_ID_INVALID_ARGUMENT;

  memset(padded_name, 0, CLIENT_ID_MAX_LEN+9);

  /* Copy the string to the padded one */
  for(i = 0; i < strnlen(name, CLIENT_ID_MAX_LEN); padded_name[i] = name[i], i++) ;

  /* For each 9 character pack:
   *   - keep the 7 first bit (the 8th is often 0: ascii string) 
   *   - pack 7x9 bit to 63 bits using xor
   *   - xor the last 8th bit to a single 0 , or-ed with the rest
   * Proceeding with the next 9 bytes pack will produce a new value that is xored with the 
   * one of the previous iteration */

  for(offset = 0; offset < CLIENT_ID_MAX_LEN; offset += 9)
    {
      /* input name is ascii string, remove 8th bit on each byte, not significant */
      i1 = padded_name[offset + 0] & 0x7F;
      i2 = (uint64_t) (padded_name[offset + 1] & 0x7F) << 7;
      i3 = (uint64_t) (padded_name[offset + 2] & 0x7F) << 14;
      i4 = (uint64_t) (padded_name[offset + 3] & 0x7F) << 21;
      i5 = (uint64_t) (padded_name[offset + 4] & 0x7F) << 28;
      i6 = (uint64_t) (padded_name[offset + 5] & 0x7F) << 35;
      i7 = (uint64_t) (padded_name[offset + 6] & 0x7F) << 42;
      i8 = (uint64_t) (padded_name[offset + 7] & 0x7F) << 49;
      i9 = (uint64_t) (padded_name[offset + 8] & 0x7F) << 56;

      sum = (uint64_t) padded_name[offset + 0] +
          (uint64_t) padded_name[offset + 1] +
          (uint64_t) padded_name[offset + 2] +
          (uint64_t) padded_name[offset + 3] +
          (uint64_t) padded_name[offset + 4] +
          (uint64_t) padded_name[offset + 5] +
          (uint64_t) padded_name[offset + 6] +
          (uint64_t) padded_name[offset + 7] + (uint64_t) padded_name[offset + 8];

      /* Get xor combibation of all the 8h bit */
      l = (padded_name[offset + 0] & 0x80) ^
          (padded_name[offset + 1] & 0x80) ^
          (padded_name[offset + 2] & 0x80) ^
          (padded_name[offset + 3] & 0x80) ^
          (padded_name[offset + 4] & 0x80) ^
          (padded_name[offset + 5] & 0x80) ^
          (padded_name[offset + 6] & 0x80) ^
          (padded_name[offset + 7] & 0x80) ^
          (padded_name[offset + 8] & 0x80) ^ (padded_name[offset + 9] & 0x80);

      extract = (i1 ^ i2 ^ i3 ^ i4 ^ i5 ^ i6 ^ i7 ^ i8 ^ i9) | l;

      computed_value ^= extract;
      computed_value ^= sum;
    }

  computed_value = (computed_value >> 32) ^ (computed_value & 0x00000000FFFFFFFFLL);

  *pclientid = computed_value;
  return CLIENT_ID_SUCCESS;
}                               /* nfs_client_id_compute */
