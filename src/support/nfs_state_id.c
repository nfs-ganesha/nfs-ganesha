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
 * nfs_state_id.c : The management of the state id cache.
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

size_t strnlen(const char *s, size_t maxlen);

extern time_t ServerBootTime;
extern nfs_parameter_t nfs_param;

hash_table_t *ht_state_id;

char all_zero[] = "\0\0\0\0\0\0\0\0\0\0\0\0";
char all_one[12];

int display_state_id_key(hash_buffer_t * pbuff, char *str)
{
  unsigned int i = 0;
  unsigned int len = 0;

  for(i = 0; i < 12; i++)
    len += sprintf(&(str[i * 2]), "%02x", (unsigned char)pbuff->pdata[i]);
  return len;
}                               /* display_state_id_val */

int display_state_id_val(hash_buffer_t * pbuff, char *str)
{
  cache_inode_state_t *pstate = (cache_inode_state_t *) (pbuff->pdata);

  return sprintf(str,
                 "state %p is associated with pentry=%p type=%u seqid=%u prev=%p next=%p\n",
                 pstate, pstate->pentry, pstate->state_type, pstate->seqid, pstate->prev,
                 pstate->next);
}                               /* display_state_id_val */

int compare_state_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return memcmp(buff1->pdata, buff2->pdata, 12);        /* The value 12 is fixed by RFC3530 */
}                               /* compare_state_id */

unsigned long state_id_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for(i = 0; i < 12; i++)
    {
      c = ((char *)buffclef->pdata)[i];
      sum += c;
    }

  LogFullDebug(COMPONENT_STATES, "---> state_id_value_hash_func=%lu\n",
         (unsigned long)(sum % p_hparam->index_size));

  return (unsigned long)(sum % p_hparam->index_size);
}                               /*  client_id_reverse_value_hash_func */

unsigned long state_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef)
{

  u_int32_t i1 = 0;
  u_int32_t i2 = 0;
  u_int32_t i3 = 0;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)buffclef->pdata, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- state_id_rbt_hash_func : %s\n", str);
    }

  memcpy(&i1, &(buffclef->pdata[0]), sizeof(u_int32_t));
  memcpy(&i2, &(buffclef->pdata[4]), sizeof(u_int32_t));
  memcpy(&i3, &(buffclef->pdata[8]), sizeof(u_int32_t));

  LogFullDebug(COMPONENT_STATES, "--->  state_id_rbt_hash_func=%lu\n", (unsigned long)(i1 ^ i2 ^ i3));

  return (unsigned long)(i1 ^ i2 ^ i3);
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
int nfs4_Init_state_id(nfs_state_id_parameter_t param)
{
  /* Init  all_one */
  memset(all_one, 0xFF, 12);

  if((ht_state_id = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATES, "NFS STATE_ID: Cannot init State Id cache");
      return -1;
    }

  return 0;
}                               /* nfs_Init_client_id */

/**
 *
 * nfs4_BuildStateId_Other
 *
 * This routine fills in the pcontext field in the compound data.
 * pentry is supposed to be locked when this function is called.
 *
 * @param pentry      [INOUT] related pentry (should be a REGULAR FILE)
 * @param pcontext    [IN]    FSAL's operation context
 * @param popen_owner [IN]    the NFSV4.x open_owner for whom this stateid is built
 * @param other       [OUT]   the stateid.other object (a char[12] string)
 *
 * @return 1 if ok, 0 otherwise.
 *
 */

int nfs4_BuildStateId_Other(cache_entry_t * pentry,
                            fsal_op_context_t * pcontext,
                            cache_inode_open_owner_t * popen_owner, char *other)
{
  uint64_t fileid_digest = 0;
  u_int16_t srvboot_digest = 0;
  uint32_t open_owner_digest = 0;

  if(pcontext == NULL)
    return 0;

  if(pentry == NULL)
    return 0;

  if(popen_owner == NULL)
    return 0;

  if(pentry->internal_md.type != REGULAR_FILE)
    return 0;

  if(other == NULL)
    return 0;

  LogFullDebug(COMPONENT_STATES,
         "----  nfs4_BuildStateId_Other : pentry=%p popen_owner=%u|%s\n",
         pentry, popen_owner->owner_len, popen_owner->owner_val);

  /* Get several digests to build the stateid : the server boot time, the fileid and a monotonic counter */
  if(FSAL_IS_ERROR(FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                     FSAL_DIGEST_FILEID3,
                                     &(pentry->object.file.handle),
                                     (caddr_t) & fileid_digest)))
    return 0;

  srvboot_digest = (u_int16_t) (ServerBootTime & 0x0000FFFF);;
  open_owner_digest = popen_owner->counter;

  LogFullDebug(COMPONENT_STATES,
         "----  nfs4_BuildStateId_Other : pentry=%p fileid=%llu open_owner_digest=%u\n",
         pentry, fileid_digest, open_owner_digest);

  /* Now, let's do the time's warp again.... Well, in fact we'll just build the stateid.other field */
  memcpy((char *)other, &srvboot_digest, 2);
  memcpy((char *)(other + 2), &fileid_digest, 8);
  memcpy((char *)(other + 10), &open_owner_digest, 2);

  return 1;
}                               /* nfs4_BuildStateId_Other */

/**
 *
 * nfs4_State_Set
 *
 * This routine sets a state into the states's hashtable.
 *
 * @param pstate [IN] pointer to the stateid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Set(char other[12], cache_inode_state_t * pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- SetStateid : %s\n", str);
    }

  if((buffkey.pdata = (caddr_t) Mem_Alloc(12)) == NULL)
    return 0;
  memcpy(buffkey.pdata, other, 12);
  buffkey.len = 12;

  buffval.pdata = (caddr_t) pstate_data;
  buffval.len = sizeof(cache_inode_state_t);

  if(HashTable_Test_And_Set
     (ht_state_id, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    return 0;

  return 1;
}                               /* nfs4_State_Set */

/**
 *
 * nfs4_State_Get
 *
 * This routine gets a state from the states's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [OUT] found state
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Get(char other[12], cache_inode_state_t * pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- GetStateid : %s\n", str);
    }

  buffkey.pdata = (caddr_t) other;
  buffkey.len = 12;

  if(HashTable_Get(ht_state_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Get  NOT FOUND !!!!!!\n");
      return 0;
    }

  memcpy(pstate_data, buffval.pdata, sizeof(cache_inode_state_t));

  LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Get Found :-)\n");

  return 1;
}                               /* nfs4_State_Get */

/**
 *
 * nfs4_State_Get_Pointer
 *
 * This routine gets a pointer to a state from the states's hashtable.
 *
 * @param pstate       [IN] pointer to the stateid to be checked.
 * @param ppstate_data [OUT] pointer's state found 
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Get_Pointer(char other[12], cache_inode_state_t * *pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- Get_PointerStateid : %s\n", str);
    }

  buffkey.pdata = (caddr_t) other;
  buffkey.len = 12;

  if(HashTable_Get(ht_state_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Get_Pointer  NOT FOUND !!!!!!\n");
      return 0;
    }

  *pstate_data = (cache_inode_state_t *) buffval.pdata;

  LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Get_Pointer Found :-)\n");

  return 1;
}                               /* nfs4_State_Get_Pointer */

/**
 * 
 * nfs4_State_Update
 *
 * This routine updates a state from the states's hashtable.
 *
 * @param pstate      [IN] pointer to the stateid to be checked.
 * @param pstate_data [IN] new state
 *
 * @return 1 if ok, 0 otherwise.
 * 
 */
int nfs4_State_Update(char other[12], cache_inode_state_t * pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- UpdateStateid : %s\n", str);
    }

  buffkey.pdata = (caddr_t) other;
  buffkey.len = 12;

  if(HashTable_Get(ht_state_id, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Update  NOT FOUND !!!!!!\n");
      return 0;
    }

  memcpy(buffval.pdata, pstate_data, sizeof(cache_inode_state_t));

  LogFullDebug(COMPONENT_STATES, "---> nfs4_State_Update Found :-)\n");

  return 1;
}                               /* nfs4_State_Update */

/**
 *
 * nfs4_State_Del
 *
 * This routine removes a state from the states's hashtable.
 *
 * @param other [IN] stateid'other field, used as a hash key
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_State_Del(char other[12])
{
  hash_buffer_t buffkey, old_key, old_value;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- DelStateid : %s\n", str);
    }

  buffkey.pdata = (caddr_t) other;
  buffkey.len = 12;

  if(HashTable_Del(ht_state_id, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      Mem_Free((void *)old_key.pdata);

      /* State is managed in stuff alloc, no fre is needed for old_value.pdata */

      return 1;
    }
  else
    return 0;
}                               /* nfs4_State_Del */

/**
 *
 * nfs4_Check_Stateid
 *
 * This routine checks the availability of the stateid
 *
 * @param pstate [IN] pointer to the stateid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Check_Stateid(struct stateid4 *pstate, cache_entry_t * pentry,
                       clientid4 clientid)
{
  u_int16_t time_digest = 0;
  u_int16_t counter_digest = 0;
  cache_inode_state_t state;
  nfs_client_id_t nfs_clientid;

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)pstate->other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- CheckStateid : %s\n", str);
    }

  if(pstate == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry->internal_md.type != REGULAR_FILE)
    return NFS4ERR_SERVERFAULT;

  /* Try to get the related state */
  if(!nfs4_State_Get(pstate->other, &state))
    {
      /* State not found : return NFS4ERR_BAD_STATEID, RFC3530 page 129 */
      if(nfs_param.nfsv4_param.return_bad_stateid == TRUE)      /* Dirty work-around for HPC environment */
        return NFS4ERR_BAD_STATEID;
      else
        return NFS4_OK;
    }

  if(isFullDebug(COMPONENT_STATES))
    {
      char str[25];

      sprint_mem(str, (char *)pstate->other, 12);
      LogFullDebug(COMPONENT_SESSIONS, "         ----- CheckStateid state found: %s\n", str);
    }

  /* Get the related clientid */
  /* If call from NFSv4.1 request, the clientid is provided through the session's structure, 
   * with NFSv4.0, the clientid is related to the stateid itself */
  if(clientid == 0LL)
    {
      if(nfs_client_id_get(state.powner->clientid, &nfs_clientid) != CLIENT_ID_SUCCESS)
        {
          if(nfs_param.nfsv4_param.return_bad_stateid == TRUE)  /* Dirty work-around for HPC environment */
            return NFS4ERR_BAD_STATEID; /* Refers to a non-existing client... */
          else
            return NFS4_OK;
        }
    }

  /* Check if stateid was made from this server instance */
  memcpy((char *)&time_digest, pstate->other, 2);

  if((u_int16_t) (ServerBootTime & 0x0000FFFF) != time_digest)
    return NFS4ERR_STALE_STATEID;

  return NFS4_OK;
}                               /* nfs4_Check_Stateid */

/**
 * 
 *  nfs4_State_PrintAll
 *  
 * This routine displays the content of the hashtable used to store the states. 
 * 
 * @return nothing (void function)
 */

void nfs_State_PrintAll(void)
{
  if(isFullDebug(COMPONENT_STATES))
    HashTable_Log(COMPONENT_STATES, ht_state_id);
}                               /* nfs_State_PrintAll */
