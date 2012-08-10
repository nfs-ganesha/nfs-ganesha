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
#include "log.h"
#include "ganesha_rpc.h"
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
#define seqid_all_one 0xFFFFFFFF

pthread_mutex_t StateIdMutex = PTHREAD_MUTEX_INITIALIZER;
uint64_t state_id_counter;

int display_stateid_other(char * other, char * str)
{
  uint32_t epoch = *((uint32_t *)other);
  uint64_t count = *((uint64_t *)(other + sizeof(uint32_t)));
  return sprintf(str, "epoch=0x%08x counter=0x%016llx",
                 (unsigned int) epoch, (unsigned long long) count);
}

int display_state_id_key(hash_buffer_t * pbuff, char *str)
{
  return display_stateid_other(pbuff->pdata, str);
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
      char str1[OTHERSIZE * 2 + 32], str2[OTHERSIZE * 2 + 32];

      display_stateid_other(buff1->pdata, str1);
      display_stateid_other(buff2->pdata, str2);

      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_STATE,
                     "{%s} vs {%s}",
                     str1, str2);
    }

  return memcmp(buff1->pdata, buff2->pdata, OTHERSIZE);
}                               /* compare_state_id */

inline uint32_t compute_stateid_hash_value(uint32_t * pstate)
{
  return pstate[1] ^ pstate[2];
}

uint32_t state_id_value_hash_func(hash_parameter_t * p_hparam,
                                  hash_buffer_t    * buffclef)
{
  uint32_t val = compute_stateid_hash_value((uint32_t *) buffclef->pdata) %
                      p_hparam->index_size;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "val = %"PRIu32, val);

  return val;
}

uint64_t state_id_rbt_hash_func(hash_parameter_t * p_hparam,
                                hash_buffer_t    * buffclef)
{
  uint64_t val = compute_stateid_hash_value((uint32_t *) buffclef->pdata);

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %"PRIu64, val);

  return val;
}

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

  if((ht_state_id = HashTable_Init(&param.hash_param)) == NULL)
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
 * This routine builds the 12 byte "other" portion of a stateid from
 * the ServerEpoch and a 64 bit global counter.
 * @param other       [OUT]   the stateid.other object (a char[OTHERSIZE] string)
 *
 */
void nfs4_BuildStateId_Other(char * other)
{
  /* Use only 32 bits of server epoch */
  uint32_t epoch = (uint32_t) ServerEpoch;
  memcpy(other, &epoch, sizeof(uint32_t));

  P(StateIdMutex);
  memcpy(other + sizeof(uint32_t), &state_id_counter, sizeof(uint64_t));
  state_id_counter++;
  V(StateIdMutex);
}

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

  if((buffkey.pdata = gsh_malloc(OTHERSIZE)) == NULL)
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
                            HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "HashTable_Test_And_Set failed for key %p",
               buffkey.pdata);
      gsh_free(buffkey.pdata);
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
      gsh_free(old_key.pdata);

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
                       state_t        ** ppstate,
                       compound_data_t * data,
                       char              flags,
                       seqid4            owner_seqid,
                       unsigned char     version4,
                       const char      * tag)
{
  uint32_t          epoch = 0;
  state_t         * pstate2;
  char              str[OTHERSIZE * 2 + 1 + 6];
  int32_t           diff;

  *ppstate = NULL;
  data->current_stateid_valid = FALSE;

  if(pstate == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry == NULL)
    return NFS4ERR_SERVERFAULT;

  if(pentry->type != REGULAR_FILE)
    return NFS4ERR_SERVERFAULT;

  if(isDebug(COMPONENT_STATE))
    {
      sprint_mem(str, (char *)pstate->other, OTHERSIZE);
      sprintf(str + OTHERSIZE * 2, ":%u", (unsigned int) pstate->seqid);
    }

  /* Test for OTHER is all zeros */
  if(memcmp(pstate->other, all_zero, OTHERSIZE) == 0)
    {
      if(pstate->seqid == 0 && (flags & STATEID_SPECIAL_ALL_0) != 0)
        {
          /* All 0 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special all 0 stateid", tag);
          /* TODO FSF: eventually this may want to return an actual state for
           * use in temporary locks for I/O.
           */
          return NFS4_OK;
        }
      if(pstate->seqid == 1 && (flags & STATEID_SPECIAL_CURRENT) != 0)
        {
          /* Special current stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special 'current' stateid", tag);
          /* Copy current stateid in and proceed to checks */
          *pstate = data->current_stateid;
        }

      LogDebug(COMPONENT_STATE,
               "Check %s stateid with OTHER all zeros, seqid %u unexpected",
               tag, (unsigned int) pstate->seqid);
      return NFS4ERR_BAD_STATEID;
    }

  /* Test for OTHER is all ones */
  if(memcmp(pstate->other, all_one, OTHERSIZE) == 0)
    {
      /* Test for special all ones stateid */
      if(pstate->seqid == seqid_all_one && (flags & STATEID_SPECIAL_ALL_1) != 0)
        {
          /* All 1 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special all 1 stateid", tag);
          /* TODO FSF: eventually this may want to return an actual state for
           * use in temporary locks for I/O.
           */
          return NFS4_OK;
        }

      LogDebug(COMPONENT_STATE,
               "Check %s stateid with OTHER all ones, seqid %u unexpected",
               tag, (unsigned int) pstate->seqid);
      return NFS4ERR_BAD_STATEID;
    }

  /* Check if stateid was made from this server instance */
  memcpy(&epoch, pstate->other, sizeof(uint32_t));

  if(epoch != ServerEpoch)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stale stateid %s", tag, str);
      return NFS4ERR_STALE_STATEID;
    }

  /* Try to get the related state */
  if(!nfs4_State_Get_Pointer(pstate->other, &pstate2))
    {
      /*
       * We matched this server's Epoch, but could not find the 
       * stateid.  Chances are, the client was expired and the state
       * has all been freed.
       *
       * We could use another check here for a BAD stateid
       */
      LogDebug(COMPONENT_STATE,
               "Check %s stateid could not find state %s", tag, str);
      if(nfs_param.nfsv4_param.return_bad_stateid == TRUE)      /* Dirty work-around for HPC environment */
        return NFS4ERR_EXPIRED;
      else
        return NFS4_OK;
    }

  /* Now, if this lease is not already reserved, reserve it */
  if(data->preserved_clientid != pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid)
    {
      if(data->preserved_clientid != NULL)
        {
          /* We don't ever expect this to happen, but, just in case...
           * Update and release already reserved lease.
           */
         P(data->preserved_clientid->cid_mutex);

         update_lease(data->preserved_clientid);

         V(data->preserved_clientid->cid_mutex);

         data->preserved_clientid = NULL;
        }

      /* Check if lease is expired and reserve it */
      P(pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);

      if(!reserve_lease(pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid))
        {
          LogDebug(COMPONENT_STATE,
                   "Returning NFS4ERR_EXPIRED");

          V(pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);

          dec_client_id_ref(pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid);

          return NFS4ERR_EXPIRED;
        }

      data->preserved_clientid = pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid;

      V(pstate2->state_powner->so_owner.so_nfs4_owner.so_pclientid->cid_mutex);
    }

  /* Sanity check : Is this the right file ? */
  if(pstate2->state_pentry != pentry)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stateid %s has wrong file", tag, str);
      return NFS4ERR_BAD_STATEID;
    }

  /* Whether stateid.seqid may be zero depends on the state type
     exclusively, See RFC 5661 pp. 161,287-288. */
  if((pstate2->state_type == STATE_TYPE_LAYOUT) ||
     (pstate->seqid != 0))
    {
      /* Check seqid in stateid */
      /** @todo fsf: maybe change to simple comparison pstate->seqid < pstate2->state_seqid
       *             as good enough and maybe makes pynfs happy.
       */
      diff = pstate->seqid - pstate2->state_seqid;
      if(diff < 0)
        {
          /* if this is NFSv4 and stateid's seqid is one less than current */
          /* AND if owner_seqid is current */
          /* pass state back to allow replay check */
          if((version4 == TRUE) &&
             ((diff == -1) ||
              ((pstate2->state_seqid == 1) && (pstate->seqid == seqid_all_one))) &&
             (owner_seqid == pstate2->state_powner->so_owner.so_nfs4_owner.so_seqid))
          {
             LogDebug(COMPONENT_STATE,
                   "possible replay?");
             *ppstate = pstate2;
             return NFS4ERR_REPLAY;
          }
          /* OLD_STATEID */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found OLD stateid %s, expected seqid %u",
                   tag, str, (unsigned int) pstate2->state_seqid);
          return NFS4ERR_OLD_STATEID;
        }
      /* stateid seqid is current and owner seqid is previous, replay (should be
         an error condition that did not change the stateid, no real need to check
         since the operation must be the same) */
      else if((diff == 0) &&
              (version4 == TRUE) &&
              (owner_seqid == pstate2->state_powner->so_owner.so_nfs4_owner.so_seqid))
        {
          LogDebug(COMPONENT_STATE,
                   "possible replay?");
          *ppstate = pstate2;
          return NFS4ERR_REPLAY;
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

/**
 *
 *  nfs4_check_special_stateid
 *
 *  Special stateid, no open state, check to see if any share conflicts
 *  The stateid is all-0 or all-1
 *
 *  @return NFS4_OK if ok, anything else if otherwise
 *
 */
int nfs4_check_special_stateid(cache_entry_t *pentry,
                               const char    *tag,
                               int access)
{

  struct glist_head * glist;
  state_t           * pstate_iterate;
  int                 rc = NFS4_OK;

  if(pentry == NULL)
    {
      rc = NFS4ERR_SERVERFAULT;
      return rc;
    }

  /* Acquire lock to enter critical section on this entry */
  pthread_rwlock_rdlock(&pentry->state_lock);

  /* Iterate through file's state to look for conflicts */
  glist_for_each(glist, &pentry->state_list)
    {
      pstate_iterate = glist_entry(glist, state_t, state_list);

      switch(pstate_iterate->state_type)
        {
          case STATE_TYPE_SHARE:
            if((access == FATTR4_ATTR_READ) &&
               (pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_READ))
              {
                /* Reading to this file is prohibited, file is read-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         pstate_iterate);
                goto ssid_out;
              }

            if((access == FATTR4_ATTR_WRITE) &&
               (pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE))
              {
                /* Writing to this file is prohibited, file is write-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         pstate_iterate);
                goto ssid_out;
              }

            if((access == FATTR4_ATTR_READ_WRITE) &&
               (pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_BOTH))
              {
                /* Reading and writing to this file is prohibited, file is rw-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         pstate_iterate);
                goto ssid_out;
              }

            break;

          case STATE_TYPE_LOCK:
            /* Skip, will check for conflicting locks later */
            break;

          case STATE_TYPE_DELEG:
            // TODO FSF: should check for conflicting delegations, may need to recall
            break;

          case STATE_TYPE_LAYOUT:
            // TODO FSF: should check for conflicting layouts, may need to recall
            // Need to look at this even for NFS v4 WRITE since there may be NFS v4.1 users of the file
            break;

          case STATE_TYPE_NONE:
            break;

          default:
            break;
        }
    }
  // TODO FSF: need to check against existing locks

 ssid_out:  // Use this exit point if the lock was already acquired.
  pthread_rwlock_unlock(&pentry->state_lock);
  return rc;
}
