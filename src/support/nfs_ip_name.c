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
unsigned int expiration_time;

/**
 *
 *  ip_name_rbt_hash_func: computes the hash value for the entry in IP/name cache.
 * 
 * Computes the hash value for the entry in IP/name cache.
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
  return hash_sockaddr((sockaddr_t *)buffclef->pdata, IGNORE_PORT) % p_hparam->index_size;
}

/**
 *
 *  ip_name_rbt_hash_func: computes the rbt value for the entry in IP/name cache.
 * 
 * Computes the rbt value for the entry in IP/name cache.
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
  return hash_sockaddr((sockaddr_t *)buffclef->pdata, IGNORE_PORT);
} 

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
  return (cmp_sockaddr((sockaddr_t *)(buff1->pdata), (sockaddr_t *)(buff2->pdata), IGNORE_PORT) != 0) ? 0 : 1;
}

/**
 *
 * display_ip_name_key: displays the ip_name stored in the buffer.
 *
 * displays the ip_name key stored in the buffer. This function is to be used as 'key_to_str' field. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_ip_name_key(hash_buffer_t * pbuff, char *str)
{
  sockaddr_t *addr = (sockaddr_t *)(pbuff->pdata);

  sprint_sockaddr(addr, str, HASHTABLE_DISPLAY_STRLEN);
  return strlen(str);
}

/**
 *
 * display_ip_name_vale: displays the ip_name stored in the buffer.
 *
 * displays the ip_name value stored in the buffer. This function is to be used as 'val_to_str' field. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_ip_name_val(hash_buffer_t * pbuff, char *str)
{
  nfs_ip_name_t *pnfs_ip_name = (nfs_ip_name_t *) (pbuff->pdata);

  return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "%s", pnfs_ip_name->hostname);
}

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

int nfs_ip_name_add(sockaddr_t *ipaddr, char *hostname)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffdata;
  nfs_ip_name_t *pnfs_ip_name = NULL;
  sockaddr_t *pipaddr = NULL;
  struct timeval tv0, tv1, dur;
  int rc;
  char ipstring[SOCK_NAME_MAX];

  pnfs_ip_name = (nfs_ip_name_t *) Mem_Alloc_Label(sizeof(nfs_ip_name_t), "nfs_ip_name_t");

  if(pnfs_ip_name == NULL)
    return IP_NAME_INSERT_MALLOC_ERROR;

  pipaddr = (sockaddr_t *) Mem_Alloc(sizeof(sockaddr_t));
  if(pipaddr == NULL) 
    {
      Mem_Free(pnfs_ip_name);
      return IP_NAME_INSERT_MALLOC_ERROR;
    }

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  memcpy(pipaddr, ipaddr, sizeof(sockaddr_t));
 
  buffkey.pdata = (caddr_t) pipaddr;
  buffkey.len = sizeof(sockaddr_t);

  gettimeofday(&tv0, NULL) ;
  rc = getnameinfo((struct sockaddr *)pipaddr, sizeof(sockaddr_t),
                   pnfs_ip_name->hostname, sizeof(pnfs_ip_name->hostname),
                   NULL, 0, 0);
  gettimeofday(&tv1, NULL) ;
  timersub(&tv1, &tv0, &dur) ;


  sprint_sockaddr(pipaddr, ipstring, sizeof(ipstring));

  /* display warning if DNS resolution took more that 1.0s */
  if (dur.tv_sec >= 1)
  {
       LogEvent(COMPONENT_DISPATCH,
                "Warning: long DNS query for %s: %u.%06u sec", ipstring,
                (unsigned int)dur.tv_sec, (unsigned int)dur.tv_usec );
  }


  /* Ask for the name to be cached */
  if(rc != 0)
    {
       LogEvent(COMPONENT_DISPATCH,
                "Cannot resolve address %s, error %s",
                ipstring, gai_strerror(rc));

       Mem_Free(pnfs_ip_name);
       Mem_Free(pipaddr);
       return IP_NAME_NETDB_ERROR;
    }

  LogDebug(COMPONENT_DISPATCH,
           "Inserting %s->%s to addr cache",
           ipstring, pnfs_ip_name->hostname);

  /* I build the data with the request pointer that should be in state 'IN USE' */
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
int nfs_ip_name_get(sockaddr_t *ipaddr, char *hostname)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  nfs_ip_name_t *pnfs_ip_name;
  char ipstring[SOCK_NAME_MAX];

  sprint_sockaddr(ipaddr, ipstring, sizeof(ipstring));

  buffkey.pdata = (caddr_t) ipaddr;
  buffkey.len = sizeof(sockaddr_t);

  if(HashTable_Get(ht_ip_name, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      pnfs_ip_name = (nfs_ip_name_t *) buffval.pdata;
      strncpy(hostname, pnfs_ip_name->hostname, MAXHOSTNAMELEN);

      LogFullDebug(COMPONENT_DISPATCH,
                   "Cache get hit for %s->%s",
                   ipstring, pnfs_ip_name->hostname);

      return IP_NAME_SUCCESS;
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Cache get miss for %s",
               ipstring);

  return IP_NAME_NOT_FOUND;
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
int nfs_ip_name_remove(sockaddr_t *ipaddr)
{
  hash_buffer_t buffkey, old_value;
  nfs_ip_name_t *pnfs_ip_name = NULL;
  char ipstring[SOCK_NAME_MAX];

  sprint_sockaddr(ipaddr, ipstring, sizeof(ipstring));

  buffkey.pdata = (caddr_t) ipaddr;
  buffkey.len = sizeof(sockaddr_t);

  if(HashTable_Del(ht_ip_name, &buffkey, NULL, &old_value) == HASHTABLE_SUCCESS)
    {
      pnfs_ip_name = (nfs_ip_name_t *) old_value.pdata;

      LogFullDebug(COMPONENT_DISPATCH,
                   "Cache remove hit for %s->%s",
                   ipstring, pnfs_ip_name->hostname);

      Mem_Free((void *)pnfs_ip_name);
      return IP_NAME_SUCCESS;
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Cache remove miss for %s",
               ipstring);

  return IP_NAME_NOT_FOUND;
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
  sockaddr_t ipaddr;
  nfs_ip_name_t *pnfs_ip_name;
  sockaddr_t *pipaddr;
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
      LogCrit(COMPONENT_CONFIG,
              "Can't get label %s in file %s",
              CONF_LABEL_IP_NAME_HOSTS, path);
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
                  "Error reading key[%d] from section \"%s\" of configuration file.",
                  var_index, label);
          return IP_NAME_NOT_FOUND;
        }

      err = ipstring_to_sockaddr(key_value, &ipaddr);
      if(err != 0)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Error converting %s to an ipaddress %s",
                  key_value, gai_strerror(err));
          return IP_NAME_NOT_FOUND;
        }

      /* Entry to be cached */
      pnfs_ip_name = (nfs_ip_name_t *) Mem_Alloc(sizeof(nfs_ip_name_t));
      if(pnfs_ip_name == NULL)
        return IP_NAME_INSERT_MALLOC_ERROR;

      pipaddr = (sockaddr_t *) Mem_Alloc(sizeof(sockaddr_t));
      if(pipaddr == NULL)
        {
          Mem_Free(pnfs_ip_name);
          return IP_NAME_INSERT_MALLOC_ERROR;
        }

      strncpy(pnfs_ip_name->hostname, key_name, MAXHOSTNAMELEN);
      pnfs_ip_name->timestamp = time(NULL);
      memcpy(pipaddr, &ipaddr, sizeof(sockaddr_t));

      buffdata.pdata = (caddr_t) pnfs_ip_name;
      buffdata.len = sizeof(nfs_ip_name_t);

      buffkey.pdata = (caddr_t) pipaddr;
      buffkey.len = sizeof(sockaddr_t);

      if(HashTable_Set(ht_ip_name, &buffkey, &buffdata) != HASHTABLE_SUCCESS)
        {
          Mem_Free(pnfs_ip_name);
          Mem_Free(pipaddr);
          return IP_NAME_INSERT_MALLOC_ERROR;
        }
    }

  if(isFullDebug(COMPONENT_CONFIG))
    HashTable_Log(COMPONENT_CONFIG, ht_ip_name);

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
