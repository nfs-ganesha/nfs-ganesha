/*
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
 */

/**
 * \file    nfs_ip_name.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:39:23 $
 * \version $Revision: 1.6 $
 * \brief   The management of the IP/name cache.
 *
 * nfs_ip_name.c : The management of the IP/name cache.
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
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "config_parsing.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Hashtable used to cache the hostname, accessed by their IP addess */
hash_table_t *ht_ip_name;
extern nfs_parameter_t nfs_param;
unsigned int expiration_time;

/**
 *
 *  ip_name_rbt_hash_func: computes the hash value for the entry in IP/name cache.
 * 
 * Computes the hash value for the entry in IP/name cache. In fact, it just use addresse as value (identity function) modulo the size of the hash.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long int ip_name_value_hash_func(hash_parameter_t * p_hparam,
                                          hash_buffer_t * buffclef)
{
  return (unsigned long int)(buffclef->pdata) % p_hparam->index_size;
}                               /*  ip_name_value_hash_func */

/**
 *
 *  ip_name_rbt_hash_func: computes the rbt value for the entry in IP/name cache.
 * 
 * Computes the rbt value for the entry in IP/name cache. In fact, it just use the address value
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
unsigned long int ip_name_rbt_hash_func(hash_parameter_t * p_hparam,
                                        hash_buffer_t * buffclef)
{
  /* We use the Xid as the rbt value */
  return (unsigned long int)(buffclef->pdata);
}                               /* ip_name_rbt_hash_func */

/**
 *
 * compare_ip_name: compares the ip address stored in the key buffers.
 *
 * compare the ip address stored in the key buffers. This function is to be used as 'compare_key' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_ip_name(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  unsigned long int xid1 = (unsigned long int)(buff1->pdata);
  unsigned long int xid2 = (unsigned long int)(buff2->pdata);
  return (xid1 == xid2) ? 0 : 1;
}                               /* compare_xid */

/**
 *
 * display_ip_name: displays the ip_name stored in the buffer.
 *
 * displays the ip_name stored in the buffer. This function is to be used as 'key_to_str' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_ip_name(hash_buffer_t * pbuff, char *str)
{
  unsigned long int ip_name = ntohl((unsigned long int)(pbuff->pdata));

  return sprintf(str, "%x : %u.%u.%u.%u",
                 (unsigned int)ip_name,
                 ((unsigned int)ip_name & 0xFF000000) >> 24,
                 ((unsigned int)ip_name & 0x00FF0000) >> 16,
                 ((unsigned int)ip_name & 0x0000FF00) >> 8,
                 ((unsigned int)ip_name & 0x000000FF));
}                               /* display_ip_name */

int display_ip_value(hash_buffer_t * pbuff, char *str)
{
  nfs_ip_name_t *pnfs_ip_name;

  pnfs_ip_name = (nfs_ip_name_t *) (pbuff->pdata);

  return sprintf(str, "%s", pnfs_ip_name->hostname);
}                               /* display_ip_name */

/**
 *
 * nfs_ip_name_add: adds an entry in the duplicate requests cache.
 *
 * Adds an entry in the duplicate requests cache.
 *
 * @param ipaddr           [IN]    the ipaddr to be used as key
 * @param hostname         [IN]    the hostname added (found by using gethostbyaddr)
 *
 * @return IP_NAME_SUCCESS if successfull\n.
 * @return IP_NAME_INSERT_MALLOC_ERROR if an error occured during the insertion process \n
 * @return IP_NAME_NETDB_ERROR if an error occured during the netdb query (via gethostbyaddr).
 *
 */

int nfs_ip_name_add(unsigned int ipaddr, char *hostname)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  nfs_ip_name_t *pnfs_ip_name = NULL;
  struct hostent *hp;
  unsigned long int local_ipaddr = ipaddr;
  int length = sizeof(local_ipaddr);

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("nfs_ip_name_t");
#endif

  pnfs_ip_name = (nfs_ip_name_t *) Mem_Alloc(sizeof(nfs_ip_name_t));

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  if(pnfs_ip_name == NULL)
    return IP_NAME_INSERT_MALLOC_ERROR;

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) local_ipaddr;
  buffkey.len = 0;

  /* Ask for the name to be cached */
  if((hp = gethostbyaddr((char *)&local_ipaddr, length, AF_INET)) == NULL)
    {
      Mem_Free((void *)pnfs_ip_name);
      return IP_NAME_NETDB_ERROR;
    }

  /* I build the data with the request pointer that should be in state 'IN USE' */
  pnfs_ip_name->ipaddr = ipaddr;
  strncpy(pnfs_ip_name->hostname, hp->h_name, MAXHOSTNAMELEN);
  pnfs_ip_name->timestamp = time(NULL);

  buffdata.pdata = (caddr_t) pnfs_ip_name;
  buffdata.len = sizeof(nfs_ip_name_t);

  if(HashTable_Set(ht_ip_name, &buffkey, &buffdata) != HASHTABLE_SUCCESS)
    return IP_NAME_INSERT_MALLOC_ERROR;

  /* Copy the value for the caller */
  strncpy(hostname, pnfs_ip_name->hostname, MAXHOSTNAMELEN);

  return IP_NAME_SUCCESS;
}                               /* nfs_ip_name_add */

/**
 *
 * nfs_ip_name_get: Tries to get an entry for ip_name cache.
 *
 * Tries to get an entry for ip_name cache.
 * 
 * @param ipaddr   [IN]  the ip address requested
 * @param hostname [OUT] the hostname
 *
 * @return the result previously set if *pstatus == IP_NAME_SUCCESS
 *
 */
int nfs_ip_name_get(unsigned int ipaddr, char *hostname)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int status;
  nfs_ip_name_t *pnfs_ip_name;
  unsigned long int local_ipaddr = ipaddr;

  buffkey.pdata = (caddr_t) local_ipaddr;
  buffkey.len = 0;

  if(HashTable_Get(ht_ip_name, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      pnfs_ip_name = (nfs_ip_name_t *) buffval.pdata;
      strncpy(hostname, pnfs_ip_name->hostname, MAXHOSTNAMELEN);

      status = IP_NAME_SUCCESS;
    }
  else
    {
      status = IP_NAME_NOT_FOUND;
    }
  return status;
}                               /* nfs_ip_name_get */

/**
 *
 * nfs_ip_name_remove: Tries to remove an entry for ip_name cache
 *
 * Tries to remove an entry for ip_name cache.
 * 
 * @param ipaddr           [IN]    the ip address to be uncached.
 *
 * @return the result previously set if *pstatus == IP_NAME_SUCCESS
 *
 */
int nfs_ip_name_remove(int ipaddr)
{
  hash_buffer_t buffkey, old_value;
  int status = IP_NAME_SUCCESS;
  nfs_ip_name_t *pnfs_ip_name = NULL;
  unsigned long int local_ipaddr = ipaddr;

  buffkey.pdata = (caddr_t) local_ipaddr;
  buffkey.len = 0;

  if(HashTable_Del(ht_ip_name, &buffkey, NULL, &old_value) == HASHTABLE_SUCCESS)
    {
      pnfs_ip_name = (nfs_ip_name_t *) old_value.pdata;
      Mem_Free((void *)pnfs_ip_name);
    }
  else
    {
      status = IP_NAME_NOT_FOUND;
    }

  return status;
}                               /* nfs_ip_name_remove */

/**
 *
 * nfs_Init_ip_name: Init the hashtable for IP/name cache.
 *
 * Perform all the required initialization for hashtable IP/name cache
 * 
 * @param param [IN] parameter used to init the ip name cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_ip_name(nfs_ip_name_parameter_t param)
{
  if((ht_ip_name = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS IP_NAME: Cannot init IP/name cache");
      return -1;
    }

  /* Set the expiration time */
  expiration_time = param.expiration_time;

  return IP_NAME_SUCCESS;
}                               /* nfs_Init_ip_name */

int nfs_ip_name_populate(char *path)
{
  config_file_t config_file;
  config_item_t block;
  int var_max;
  int var_index;
  int err;
  char *key_name;
  char *key_value;
  char label[MAXNAMLEN];
  unsigned int ipaddr;
  unsigned long int long_ipaddr;
  nfs_ip_name_t *pnfs_ip_name;
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;

  config_file = config_ParseFile(path);

  if(!config_file)
    {
      LogCrit(COMPONENT_CONFIG, "Can't open file %s", path);

      return IP_NAME_NOT_FOUND;
    }

  /* Get the config BLOCK */
  if((block = config_FindItemByName(config_file, CONF_LABEL_IP_NAME_HOSTS)) == NULL)
    {
      LogCrit(COMPONENT_CONFIG, "Can't get label %s in file %s", CONF_LABEL_IP_NAME_HOSTS, path);
      return IP_NAME_NOT_FOUND;
    }
  else if(config_ItemType(block) != CONFIG_ITEM_BLOCK)
    {
      /* Expected to be a block */
      return IP_NAME_NOT_FOUND;
    }

  var_max = config_GetNbItems(block);

  for(var_index = 0; var_index < var_max; var_index++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, var_index);

      /* Get key's name */
      if((err = config_GetKeyValue(item, &key_name, &key_value)) != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error reading key[%d] from section \"%s\" of configuration file.\n",
                  var_index, label);
          return ID_MAPPER_NOT_FOUND;
        }

      ipaddr = inet_addr(key_value);
      long_ipaddr = ipaddr;

      /* Entry to be cached */
      if((pnfs_ip_name = (nfs_ip_name_t *) Mem_Alloc(sizeof(nfs_ip_name_t))) == NULL)
        return ID_MAPPER_INSERT_MALLOC_ERROR;

      /* I build the data with the request pointer that should be in state 'IN USE' */
      pnfs_ip_name->ipaddr = ipaddr;
      strncpy(pnfs_ip_name->hostname, key_name, MAXHOSTNAMELEN);
      pnfs_ip_name->timestamp = time(NULL);

      buffdata.pdata = (caddr_t) pnfs_ip_name;
      buffdata.len = sizeof(nfs_ip_name_t);

      /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this,
       * this also means that buffkey->len will be 0 */
      buffkey.pdata = (caddr_t) long_ipaddr;

      buffkey.len = 0;

      if(HashTable_Set(ht_ip_name, &buffkey, &buffdata) != HASHTABLE_SUCCESS)
        return IP_NAME_INSERT_MALLOC_ERROR;

    }

  /* HashTable_Log( ht_ip_name ) ; */
  return IP_NAME_SUCCESS;
}                               /* nfs_ip_name_populate */

/**
 *
 * nfs_ip_name_get_stats: gets the hash table statistics for the IP_NAME Table.
 *
 * Gets the hash table statistics for the IP_NAME Table.
 *
 * @param phstat [OUT] pointer to the resulting stats.
 *
 * @return nothing (void function)
 *
 * @see HashTable_GetStats
 *
 */
void nfs_ip_name_get_stats(hash_stat_t * phstat)
{
  HashTable_GetStats(ht_ip_name, phstat);
}                               /* nfs_ip_name_get_stats */
