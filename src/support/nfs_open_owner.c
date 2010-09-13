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
 * nfs_open_owner.c : The management of the open owner cache.
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
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
#include "log_macros.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

size_t strnlen(const char *s, size_t maxlen);

extern time_t ServerBootTime;
extern nfs_parameter_t nfs_param;

hash_table_t *ht_open_owner;

uint32_t open_owner_counter = 0;
pthread_mutex_t open_owner_counter_lock = PTHREAD_MUTEX_INITIALIZER;

int display_open_owner_key(hash_buffer_t * pbuff, char *str)
{
  char strtmp[MAXNAMLEN * 2];
  unsigned int i = 0;
  unsigned int len = 0;

  cache_inode_open_owner_name_t *pname = (cache_inode_open_owner_name_t *) pbuff->pdata;

  for(i = 0; i < pname->owner_len; i++)
    len += sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)pname->owner_val[i]);

  return len + sprintf(str, "clientid=%llu owner=(%u|%s)",
                       (unsigned long long)pname->clientid, pname->owner_len, strtmp);
}                               /* display_state_id_val */

int display_open_owner_val(hash_buffer_t * pbuff, char *str)
{
  char strtmp[MAXNAMLEN * 2];
  unsigned int i = 0;
  unsigned int len = 0;

  cache_inode_open_owner_t *powner = (cache_inode_open_owner_t *) (pbuff->pdata);

  for(i = 0; i < powner->owner_len; i++)
    len += sprintf(&(strtmp[i * 2]), "%02x", (unsigned char)powner->owner_val[i]);

  return len + sprintf(str, "clientid=%llu owner=(%u|%s) confirmed=%u seqid=%u",
                       (unsigned long long)powner->clientid, powner->owner_len, strtmp,
                       powner->confirmed, powner->seqid);
}                               /* display_state_id_val */

int compare_open_owner(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  unsigned int rc;

  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str1[MAXPATHLEN];
      char str2[MAXPATHLEN];

      display_open_owner_key(buff1, str1);
      display_open_owner_key(buff2, str2);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "compare_open_owner => {%s}|{%s}\n", str1, str2);
    }

  cache_inode_open_owner_name_t *pname1 = (cache_inode_open_owner_name_t *) buff1->pdata;
  cache_inode_open_owner_name_t *pname2 = (cache_inode_open_owner_name_t *) buff2->pdata;

  if(pname1 == NULL || pname2 == NULL)
    return 1;

  if(pname1->clientid != pname2->clientid)
    return 1;

  if(pname1->owner_len != pname2->owner_len)
    return 1;

  return memcmp((char *)pname1->owner_val, (char *)pname2->owner_val, pname1->owner_len);
}                               /* compare_open_owner */

unsigned long open_owner_value_hash_func(hash_parameter_t * p_hparam,
                                         hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  cache_inode_open_owner_name_t *pname =
      (cache_inode_open_owner_name_t *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "---> rbt_hash_val = %lu\n", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);

}                               /* open_owner_value_hash_func */

unsigned long open_owner_rbt_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  cache_inode_open_owner_name_t *pname =
      (cache_inode_open_owner_name_t *) buffclef->pdata;

  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c = 0;
  unsigned long res = 0;

  /* Compute the sum of all the characters */
  for(i = 0; i < pname->owner_len; i++)
    {
      c = ((char *)pname->owner_val)[i];
      sum += c;
    }

  res = (unsigned long)(pname->clientid) + (unsigned long)sum + pname->owner_len;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "---> rbt_hash_func = %lu\n", res);

  return res;
}                               /* state_id_rbt_hash_func */

/**
 *
 * nfs4_Init_state_id: Init the hashtable for Client Id cache.
 *
 * Perform all the required initialization for hashtable State Id cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs4_Init_open_owner(nfs_open_owner_parameter_t param)
{

  if((ht_open_owner = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_OPEN_OWNER_HASH, "NFS STATE_ID: Cannot init State Id cache");
      return -1;
    }

  return 0;
}                               /* nfs4_Init_open_owner */

/**
 * nfs_open_owner_Set
 * 
 *
 * This routine sets a open owner into the related hashtable
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Set(cache_inode_open_owner_name_t * pname,
                       cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str[MAXPATHLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(cache_inode_open_owner_name_t);

      display_open_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "nfs_open_owner_Set => KEY {%s}\n", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  buffval.pdata = (caddr_t) powner;
  buffval.len = sizeof(cache_inode_open_owner_t);

  P(open_owner_counter_lock);
  open_owner_counter += 1;
  powner->counter = open_owner_counter;
  V(open_owner_counter_lock);

  if(HashTable_Test_And_Set
     (ht_open_owner, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs_open_owner_Set */

/**
 *
 * nfs_open_owner_Get
 *
 * This routine gets open owner from the openowner's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Get(cache_inode_open_owner_name_t * pname,
                       cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  memcpy((char *)powner, buffval.pdata, sizeof(cache_inode_open_owner_t));

  return 1;
}                               /* nfs_open_owner_Get */

/**
 *
 * nfs_open_owner_Get_Pointer
 *
 * This routine gets a pointer to an open owner from the open owners's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Get_Pointer(cache_inode_open_owner_name_t * pname,
                               cache_inode_open_owner_t * *powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_OPEN_OWNER_HASH))
    {
      char str[MAXPATHLEN];

      buffkey.pdata = (caddr_t) pname;
      buffkey.len = sizeof(cache_inode_open_owner_name_t);

      display_open_owner_key(&buffkey, str);
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "nfs_open_owner_Get_Pointer => KEY {%s}\n", str);
    }

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "nfs_open_owner_Get_Pointer => NOTFOUND\n");
      return 0;
    }

  *powner = (cache_inode_open_owner_t *) buffval.pdata;

  LogFullDebug(COMPONENT_OPEN_OWNER_HASH, "nfs_open_owner_Get_Pointer => FOUND\n");

  return 1;
}                               /* nfs_open_owner_Get_Pointer */

/**
 * 
 * nfs_open_owner_Update
 *
 * This routine updates a open owner from the open owners's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [IN] new state
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nfs_open_owner_Update(cache_inode_open_owner_name_t * pname,
                          cache_inode_open_owner_t * powner)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Get(ht_open_owner, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      return 0;
    }

  memcpy(buffval.pdata, powner, sizeof(cache_inode_open_owner_t));

  return 1;
}                               /* nfs_open_owner_Update */

/**
 *
 * nfs_open_owner_Del
 *
 * This routine removes a open owner from the open_owner's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs_open_owner_Del(cache_inode_open_owner_name_t * pname)
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) pname;
  buffkey.len = sizeof(cache_inode_open_owner_name_t);

  if(HashTable_Del(ht_open_owner, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nfs_open_owner_Del */

/**
 * 
 *  nfs_open_owner_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the open owners. 
 * 
 * @return nothing (void function)
 */

void nfs_open_owner_PrintAll(void)
{
  HashTable_Log(COMPONENT_NFSPROTO, ht_open_owner);
}                               /* nfs_open_owner_PrintAll */

int nfs_convert_open_owner(open_owner4 * pnfsowner,
                           cache_inode_open_owner_name_t * pname_owner)
{
  if(pnfsowner == NULL || pname_owner == NULL)
    return 0;

  pname_owner->clientid = pnfsowner->clientid;
  pname_owner->owner_len = pnfsowner->owner.owner_len;
  memcpy((char *)pname_owner->owner_val, (char *)pnfsowner->owner.owner_val,
         pnfsowner->owner.owner_len);

  return 1;
}                               /* nfs_convert_open_owner */
