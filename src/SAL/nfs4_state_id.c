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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

size_t strnlen(const char *s, size_t maxlen);

hash_table_t *ht_state_id;

char all_zero[OTHERSIZE];
char all_one[OTHERSIZE];

int display_state_id_key(hash_buffer_t * pbuff, char *str)
{
  unsigned int i = 0;
  unsigned int len = 0;

  for(i = 0; i < OTHERSIZE; i++)
    len += sprintf(&(str[i * 2]), "%02x", (unsigned char)pbuff->pdata[i]);
  return len;
}                               /* display_state_id_val */

int display_state_id_val(hash_buffer_t * pbuff, char *str)
{
  state_t *pstate = (state_t *) (pbuff->pdata);

  return sprintf(str,
                 "state %p is associated with pentry=%p type=%u seqid=%u",
                 pstate,
                 pstate->state_pentry,
                 pstate->state_type,
                 pstate->state_seqid);
}                               /* display_state_id_val */

int compare_state_id(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      char str1[OTHERSIZE * 2 + 1], str2[OTHERSIZE * 2 + 1];

      sprint_mem(str1, buff1->pdata, OTHERSIZE);
      sprint_mem(str2, buff2->pdata, OTHERSIZE);

      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_STATE,
                     "%s vs %s",
                     str1, str2);
    }

  return memcmp(buff1->pdata, buff2->pdata, OTHERSIZE);
}                               /* compare_state_id */

unsigned long state_id_value_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i = 0;
  unsigned char c;

  /* Compute the sum of all the characters */
  for(i = 0; i < OTHERSIZE; i++)
    {
      c = ((char *)buffclef->pdata)[i];
      sum += c;
    }

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "value = %lu",
                 (unsigned long)(sum % p_hparam->index_size));

  return (unsigned long)(sum % p_hparam->index_size);
}                               /*  client_id_reverse_value_hash_func */

unsigned long state_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef)
{

  u_int32_t i1 = 0;
  u_int32_t i2 = 0;
  u_int32_t i3 = 0;

  memcpy(&i1, &(buffclef->pdata[0]), sizeof(u_int32_t));
  memcpy(&i2, &(buffclef->pdata[4]), sizeof(u_int32_t));
  memcpy(&i3, &(buffclef->pdata[8]), sizeof(u_int32_t));

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "rbt = %lu",
                 (unsigned long)(i1 ^ i2 ^ i3));

  return (unsigned long)(i1 ^ i2 ^ i3);
}                               /* state_id_rbt_hash_func */

unsigned int state_id_hash_both( hash_parameter_t * p_hparam,
				 hash_buffer_t    * buffclef, 
				 uint32_t * phashval, uint32_t * prbtval )
{
   uint32_t h1 = 0 ;
   uint32_t h2 = 0 ;

   Lookup3_hash_buff_dual( (char *)(buffclef->pdata), OTHERSIZE, &h1, &h2 ) ;

    h1 = h1 % p_hparam->index_size ;

    *phashval = h1 ;
    *prbtval = h2 ; 

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu rbt = %lu",
                 (unsigned long) h1, (unsigned long) h2);

   /* Success */
   return 1 ;
} /* state_id_hash_both */


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
  memset(all_zero, 0, OTHERSIZE);
  memset(all_one, 0xFF, OTHERSIZE);

  if((ht_state_id = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_STATE, "Cannot init State Id cache");
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
 * @param other       [OUT]   the stateid.other object (a char[OTHERSIZE] string)
 *
 * @return 1 if ok, 0 otherwise.
 *
 */

int nfs4_BuildStateId_Other(cache_entry_t     * pentry,
                            fsal_op_context_t * pcontext,
                            state_owner_t     * popen_owner,
                            char              * other)
{
  uint64_t fileid_digest = 0;
  u_int16_t srvboot_digest = 0;
  uint32_t open_owner_digest = 0;

  LogFullDebug(COMPONENT_STATE,
               "pentry=%p popen_owner=%u|%s",
               pentry,
               popen_owner->so_owner_len,
               popen_owner->so_owner_val);

  /* Get several digests to build the stateid : the server boot time, the fileid and a monotonic counter */
  if(FSAL_IS_ERROR(FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                                     FSAL_DIGEST_FILEID3,
                                     &(pentry->object.file.handle),
                                     (caddr_t) & fileid_digest)))
    return 0;

  srvboot_digest = (u_int16_t) (ServerBootTime & 0x0000FFFF);;
  open_owner_digest = popen_owner->so_owner.so_nfs4_owner.so_counter;

  LogFullDebug(COMPONENT_STATE,
               "pentry=%p fileid=%"PRIu64" open_owner_digest=%u",
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
int nfs4_State_Set(char other[OTHERSIZE], state_t * pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if((buffkey.pdata = (caddr_t) Mem_Alloc_Label(OTHERSIZE, "nfs4_State_Set")) == NULL)
    return 0;

  LogFullDebug(COMPONENT_STATE,
               "Allocating stateid key %p", buffkey.pdata);

  memcpy(buffkey.pdata, other, OTHERSIZE);
  buffkey.len = OTHERSIZE;

  buffval.pdata = (caddr_t) pstate_data;
  buffval.len = sizeof(state_t);

  if(HashTable_Test_And_Set(ht_state_id,
                            &buffkey,
                            &buffval,
                            HASHTABLE_SET_HOW_SET_OVERWRITE) != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "HashTable_Test_And_Set failed for key %p",
               buffkey.pdata);
      Mem_Free(buffkey.pdata);
      return 0;
    }

  return 1;
}                               /* nfs4_State_Set */

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
int nfs4_State_Get_Pointer(char other[OTHERSIZE], state_t * *pstate_data)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  int           rc;

  buffkey.pdata = (caddr_t) other;
  buffkey.len = OTHERSIZE;

  rc = HashTable_Get(ht_state_id, &buffkey, &buffval);
  if(rc != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "HashTable_Get returned %d", rc);
      return 0;
    }

  *pstate_data = (state_t *) buffval.pdata;

  return 1;
}                               /* nfs4_State_Get_Pointer */

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
int nfs4_State_Del(char other[OTHERSIZE])
{
  hash_buffer_t buffkey, old_key, old_value;

  buffkey.pdata = (caddr_t) other;
  buffkey.len = OTHERSIZE;

  if(HashTable_Del(ht_state_id, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      LogFullDebug(COMPONENT_STATE,
                   "Freeing stateid key %p", old_key.pdata);
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
 * This routine checks the availability of the stateid and returns a pointer to it
 *
 * @param pstate [IN] pointer to the stateid to be checked.
 *
 * @return 1 if ok, 0 otherwise.
 *
 */
int nfs4_Check_Stateid(stateid4        * pstate,
                       cache_entry_t   * pentry,
                       clientid4         clientid,
                       state_t        ** ppstate,
                       compound_data_t * data,
                       char              flags,
                       const char      * tag)
{
  u_int16_t         time_digest = 0;
  state_t         * pstate2;
  nfs_client_id_t   nfs_clientid;
  char              str[OTHERSIZE * 2 + 1 + 6];
  int32_t           diff;

  *ppstate = NULL;
  data->current_stateid_valid = FALSE;

  if(pstate == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry->internal_md.type != REGULAR_FILE)
    return NFS4ERR_SERVERFAULT;

  if(isDebug(COMPONENT_STATE))
    {
      sprint_mem(str, (char *)pstate->other, OTHERSIZE);
      sprintf(str + OTHERSIZE * 2, ":%u", (unsigned int) pstate->seqid);
    }

  if((flags & STATEID_SPECIAL_ALL_0) != 0)
    {
      if(memcmp(pstate->other, all_zero, OTHERSIZE) == 0 && pstate->seqid == 0)
        {
          /* All 0 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special all 0 stateid", tag);
          /* TODO FSF: eventually this may want to return an actual state for
           * use in temporary locks for I/O.
           */
          return NFS4_OK;
        }
    }

  if((flags & STATEID_SPECIAL_ALL_1) != 0)
    {
      if(memcmp(pstate->other, all_one, OTHERSIZE) == 0 &&
         pstate->seqid == 0xFFFFFFFF)
        {
          /* All 1 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special all 1 stateid", tag);
          /* TODO FSF: eventually this may want to return an actual state for
           * use in temporary locks for I/O.
           */
          return NFS4_OK;
        }
    }

  if((flags & STATEID_SPECIAL_CURRENT) != 0)
    {
      if(memcmp(pstate->other, all_zero, OTHERSIZE) == 0 && pstate->seqid == 1)
        {
          /* All 0 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special 'current' stateid", tag);
          /* Copy current stateid in and proceed to checks */
          *pstate = data->current_stateid;
        }
    }

  /* Check if stateid was made from this server instance */
  memcpy((char *)&time_digest, pstate->other, 2);

  if((u_int16_t) (ServerBootTime & 0x0000FFFF) != time_digest)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stale stateid %s", tag, str);
      return NFS4ERR_STALE_STATEID;
    }

  /* Try to get the related state */
  if(!nfs4_State_Get_Pointer(pstate->other, &pstate2))
    {
      /* stat */
      data->pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_GET_STATE] += 1;

      /* State not found : return NFS4ERR_BAD_STATEID, RFC3530 page 129 */
      LogDebug(COMPONENT_STATE,
               "Check %s stateid could not find state %s", tag, str);
      if(nfs_param.nfsv4_param.return_bad_stateid == TRUE)      /* Dirty work-around for HPC environment */
        return NFS4ERR_BAD_STATEID;
      else
        return NFS4_OK;
    }

  /* Get the related clientid */
  /* If call from NFSv4.1 request, the clientid is provided through the session's structure, 
   * with NFSv4.0, the clientid is related to the stateid itself */
  if(clientid == 0LL)
    {
      if(nfs_client_id_get(pstate2->state_powner->so_owner.so_nfs4_owner.so_clientid,
                           &nfs_clientid) != CLIENT_ID_SUCCESS)
        {
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid could not find clientid for state %s",
                   tag, str);
          if(nfs_param.nfsv4_param.return_bad_stateid == TRUE)  /* Dirty work-around for HPC environment */
            return NFS4ERR_BAD_STATEID; /* Refers to a non-existing client... */
          else
            return NFS4_OK;
        }
    }

  /* Sanity check : Is this the right file ? */
  if(pstate2->state_pentry != pentry)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stateid %s has wrong file", tag, str);
      return NFS4ERR_BAD_STATEID;
    }

  /* Test for seqid = 0 if allowed */
  if((flags & STATEID_SPECIAL_SEQID_0) == 0 || pstate->seqid != 0)
    {
      /* Check seqid in stateid */
      diff = pstate->seqid - pstate2->state_seqid;
      if(diff < 0)
        {
          /* OLD_STATEID */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found OLD stateid %s, expected seqid %u",
                   tag, str, (unsigned int) pstate2->state_seqid);
          return NFS4ERR_OLD_STATEID;
        }
      else if(diff > 0)
        {
          /* BAD_STATEID */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found BAD stateid %s, expected seqid %u",
                   tag, str, (unsigned int) pstate2->state_seqid);
          return NFS4ERR_BAD_STATEID;
        }
    }

  LogFullDebug(COMPONENT_STATE,
               "Check %s stateid found valid stateid %s - %p",
               tag, str, pstate2);

  /* Copy stateid into current for later use */
  data->current_stateid       = *pstate;
  data->current_stateid.seqid = pstate2->state_seqid;
  data->current_stateid_valid = TRUE;

  *ppstate = pstate2;
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
  if(isFullDebug(COMPONENT_STATE))
    HashTable_Log(COMPONENT_STATE, ht_state_id);
}                               /* nfs_State_PrintAll */

void update_stateid(state_t         * pstate,
                    stateid4        * presp,
                    compound_data_t * data,
                    const char      * tag)
{
  /* Increment state_seqid, handling wraparound */
  pstate->state_seqid += 1;
  if(pstate->state_seqid == 0)
    pstate->state_seqid = 1;

  /* Copy stateid into current for later use */
  data->current_stateid.seqid = pstate->state_seqid;
  memcpy(data->current_stateid.other, pstate->stateid_other, OTHERSIZE);
  data->current_stateid_valid = TRUE;

  /* Copy stateid into response */
  presp->seqid = pstate->state_seqid;
  memcpy(presp->other, pstate->stateid_other, OTHERSIZE);

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[OTHERSIZE * 2 + 1 + 6];
      sprint_mem(str, (char *)pstate->stateid_other, OTHERSIZE);
      sprintf(str + OTHERSIZE * 2, ":%u", (unsigned int) pstate->state_seqid);
      LogDebug(COMPONENT_STATE,
               "Update %s stateid to %s for response",
               tag, str);
    }
}
