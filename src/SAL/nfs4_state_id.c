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
 * @file nfs4_state_id.c
 * @brief NFSv4 state ids
 */

#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <ctype.h> /* for having isalnum */
#include <stdlib.h> /* for having atoi */
#include <dirent.h> /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h> /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

size_t strnlen(const char *s, size_t maxlen);

/**
 * @brief Hash table for stateids.
 */
hash_table_t *ht_state_id;

/**
 * @brief All-zeroes stateid4.other
 */
char all_zero[OTHERSIZE];

/**
 * @brief All-zeroes stateid4.other
 */
char all_ones[OTHERSIZE];
#define seqid_all_one 0xFFFFFFFF

/**
 * @brief Mutes to protect the stateid counter
 */
pthread_mutex_t StateIdMutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Counter to create unique stateids from server epoch
 */
uint64_t state_id_counter;

/**
 * @brief Display a stateid other
 *
 * @param[in]  other The other
 * @param[out] str   Output buffer
 *
 * @return Length of output string.
 */
int display_stateid_other(char *other, char *str)
{
  uint32_t epoch = *((uint32_t *)other);
  uint64_t count = *((uint64_t *)(other + sizeof(uint32_t)));
  return sprintf(str, "epoch=0x%08x counter=0x%016llx",
                 (unsigned int) epoch, (unsigned long long) count);
}

/**
 * @brief Display a stateid other in the hash table
 *
 * @param[in]  buff The key
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_state_id_key(struct gsh_buffdesc *buff, char *str)
{
  return display_stateid_other(buff->addr, str);
}

/**
 * @brief Display a state in the hash table
 *
 * @param[in]  buff The value
 * @param[out] str  Output buffer
 *
 * @return Length of output string.
 */
int display_state_id_val(struct gsh_buffdesc *buff, char *str)
{
  state_t *state = buff->addr;

  return sprintf(str,
                 "state %p is associated with entry=%p type=%u seqid=%u",
                 state,
                 state->state_entry,
                 state->state_type,
                 state->state_seqid);
}

/**
 * @brief Compare to stateids
 *
 * @param[in] buff1 One key
 * @param[in] buff2 Another key
 *
 * @retval 0 if equal.
 * @retval 1 if not equal.
 */
int compare_state_id(struct gsh_buffdesc *buff1, struct gsh_buffdesc *buff2)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      char str1[OTHERSIZE * 2 + 32], str2[OTHERSIZE * 2 + 32];

      display_stateid_other(buff1->addr, str1);
      display_stateid_other(buff2->addr, str2);

      if(isDebug(COMPONENT_HASHTABLE))
        LogFullDebug(COMPONENT_STATE,
                     "{%s} vs {%s}",
                     str1, str2);
    }

  return memcmp(buff1->addr, buff2->addr, OTHERSIZE);
}                               /* compare_state_id */

/**
 * @brief Hash a stateid
 *
 * @param[in] stateid Array aliased to stateid
 */
inline uint32_t compute_stateid_hash_value(uint32_t *stateid)
{
  return stateid[1] ^ stateid[2];
}

/**
 * @brief Hash index for a stateid
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The hash index.
 */
uint32_t state_id_value_hash_func(hash_parameter_t *hparam,
				  struct gsh_buffdesc *key)
{
  uint32_t val = compute_stateid_hash_value(key->addr) %
    hparam->index_size;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "val = %"PRIu32, val);

  return val;
}

/**
 * @brief RBT hash for a stateid
 *
 * @param[in] hparam Hash parameter
 * @param[in] key    Key to hash
 *
 * @return The RBT hash.
 */
uint64_t state_id_rbt_hash_func(hash_parameter_t *hparam,
                                struct gsh_buffdesc *key)
{
  uint64_t val = compute_stateid_hash_value(key->addr);

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %"PRIu64, val);

  return val;
}

/**
 * @brief Init the hashtable for stateids
 *
 * @param[in] param Parameter used to init the stateid table
 *
 * @retval 0 if successful.
 * @retval -1 on failure.
 */
int nfs4_Init_state_id(hash_parameter_t *param)
{
  /* Init  all_one */
  memset(all_zero, 0, OTHERSIZE);
  memset(all_ones, 0xFF, OTHERSIZE);

  if((ht_state_id = HashTable_Init(param)) == NULL)
    {
      LogCrit(COMPONENT_STATE, "Cannot init State Id cache");
      return -1;
    }

  return 0;
}

/**
 * @brief Build the 12 byte "other" portion of a stateid
 *
 * It is built from the ServerEpoch and a 64 bit global counter.
 *
 * @param[in] other stateid.other object (a char[OTHERSIZE] string)
 */
void nfs4_BuildStateId_Other(char *other)
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
 * @brief Set a state into the stateid hashtable.
 *
 * @param[in] other stateid4.other
 * @param[in] state The state to add
 *
 * @retval 1 if ok.
 * @retval 0 if not ok.
 */
int nfs4_State_Set(char other[OTHERSIZE], state_t *state)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;

  if((buffkey.addr = gsh_malloc(OTHERSIZE)) == NULL)
    return 0;

  LogFullDebug(COMPONENT_STATE,
               "Allocating stateid key %p", buffkey.addr);

  memcpy(buffkey.addr, other, OTHERSIZE);
  buffkey.len = OTHERSIZE;

  buffval.addr = state;
  buffval.len = sizeof(state_t);

  if(HashTable_Test_And_Set(ht_state_id,
                            &buffkey,
                            &buffval,
                            HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "HashTable_Test_And_Set failed for key %p",
               buffkey.addr);
      gsh_free(buffkey.addr);
      return 0;
    }

  return 1;
}

/**
 * @brief Get the state from the stateid
 *
 * @param[in]  other      stateid4.other
 * @param[out] state_data State found
 *
 * @retval 1 if ok.
 * @retval 0 if not ok.
 */
int nfs4_State_Get_Pointer(char other[OTHERSIZE],
			   state_t **state_data)
{
  struct gsh_buffdesc buffkey;
  struct gsh_buffdesc buffval;
  int           rc;

  buffkey.addr = (caddr_t) other;
  buffkey.len = OTHERSIZE;

  rc = HashTable_Get(ht_state_id, &buffkey, &buffval);
  if(rc != HASHTABLE_SUCCESS)
    {
      LogDebug(COMPONENT_STATE,
               "HashTable_Get returned %d", rc);
      return 0;
    }

  *state_data = buffval.addr;

  return 1;
}

/**
 * @brief Remove a state from the stateid table
 *
 * @param[in] other stateid4.other
 *
 * @retval 1 if ok.
 * @retval 0 if not ok.
 */
int nfs4_State_Del(char other[OTHERSIZE])
{
  struct gsh_buffdesc buffkey, old_key, old_value;

  buffkey.addr = other;
  buffkey.len = OTHERSIZE;

  if(HashTable_Del(ht_state_id, &buffkey, &old_key, &old_value) == HASHTABLE_SUCCESS)
    {
      /* free the key that was stored in hash table */
      LogFullDebug(COMPONENT_STATE,
                   "Freeing stateid key %p", old_key.addr);
      gsh_free(old_key.addr);

      /* State is managed in stuff alloc, no fre is needed for old_value.addr */

      return 1;
    }
  else
    return 0;
}

/**
 * @brief Check and look up the supplied stateid
 *
 * This function yields the state for the stateid if it is valid.
 *
 * @param[in]  stateid     Stateid to look up
 * @param[in]  entry       Associated file
 * @param[out] state       Found state
 * @param[in]  data        Compound data
 * @param[in]  flags       Flags governing special stateids
 * @param[in]  owner_seqid seqid on v4.0 owner
 * @param[in]  check_seqid Whether to validate owner_seqid
 * @param[in]  tag     Arbitrary string for logging/debugging
 *
 * @return NFSv4 status codes
 */
nfsstat4 nfs4_Check_Stateid(stateid4 *stateid,
			    cache_entry_t *entry,
			    state_t **state,
			    compound_data_t *data,
			    char flags,
			    seqid4 owner_seqid,
			    bool check_seqid,
			    const char *tag)
{
  uint32_t          epoch = 0;
  state_t         * state2;
  char              str[OTHERSIZE * 2 + 1 + 6];
  int32_t           diff;

  *state = NULL;
  data->current_stateid_valid = false;

  if(stateid == NULL)
    return NFS4ERR_SERVERFAULT;

  if(entry == NULL)
    return NFS4ERR_SERVERFAULT;

  if(entry->type != REGULAR_FILE)
    return NFS4ERR_SERVERFAULT;

  if(isDebug(COMPONENT_STATE))
    {
      sprint_mem(str, (char *)stateid->other, OTHERSIZE);
      sprintf(str + OTHERSIZE * 2, ":%u", (unsigned int) stateid->seqid);
    }

  /* Test for OTHER is all zeros */
  if(memcmp(stateid->other, all_zero, OTHERSIZE) == 0)
    {
      if(stateid->seqid == 0 && (flags & STATEID_SPECIAL_ALL_0) != 0)
        {
          /* All 0 stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special all 0 stateid", tag);
          /* TODO FSF: eventually this may want to return an actual state for
           * use in temporary locks for I/O.
           */
          return NFS4_OK;
        }
      if(stateid->seqid == 1 && (flags & STATEID_SPECIAL_CURRENT) != 0)
        {
          /* Special current stateid */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found special 'current' stateid", tag);
          /* Copy current stateid in and proceed to checks */
          *stateid = data->current_stateid;
        }

      LogDebug(COMPONENT_STATE,
               "Check %s stateid with OTHER all zeros, seqid %u unexpected",
               tag, (unsigned int) stateid->seqid);
      return NFS4ERR_BAD_STATEID;
    }

  /* Test for OTHER is all ones */
  if(memcmp(stateid->other, all_ones, OTHERSIZE) == 0)
    {
      /* Test for special all ones stateid */
      if(stateid->seqid == seqid_all_one && (flags & STATEID_SPECIAL_ALL_1) != 0)
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
               tag, (unsigned int) stateid->seqid);
      return NFS4ERR_BAD_STATEID;
    }

  /* Check if stateid was made from this server instance */
  memcpy(&epoch, stateid->other, sizeof(uint32_t));

  if(epoch != ServerEpoch)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stale stateid %s", tag, str);
      return NFS4ERR_STALE_STATEID;
    }

  /* Try to get the related state */
  if(!nfs4_State_Get_Pointer(stateid->other, &state2))
    {
      /*
       * We matched this server's epoch, but could not find the stateid.
       * Chances are, the client was expired and the state has all been
       * freed.
       *
       * We could use another check here for a BAD stateid
       */
      LogDebug(COMPONENT_STATE,
               "Check %s stateid could not find state %s", tag, str);
      if(nfs_param.nfsv4_param.return_bad_stateid)      /* Dirty work-around for HPC environment */
        return NFS4ERR_EXPIRED;
      else
        return NFS4_OK;
    }

  /* Now, if this lease is not already reserved, reserve it */
  if(data->preserved_clientid != state2->state_owner->so_owner.so_nfs4_owner.so_clientrec)
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
      P(state2->state_owner->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);

      if(!reserve_lease(state2->state_owner->so_owner.so_nfs4_owner.so_clientrec))
        {
          LogDebug(COMPONENT_STATE,
                   "Returning NFS4ERR_EXPIRED");

          V(state2->state_owner->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);

          return NFS4ERR_EXPIRED;
        }

      data->preserved_clientid = state2->state_owner->so_owner.so_nfs4_owner.so_clientrec;

      V(state2->state_owner->so_owner.so_nfs4_owner.so_clientrec->cid_mutex);
    }

  /* Sanity check : Is this the right file ? */
  if(state2->state_entry != entry)
    {
      LogDebug(COMPONENT_STATE,
               "Check %s stateid found stateid %s has wrong file", tag, str);
      return NFS4ERR_BAD_STATEID;
    }

  /* Whether stateid.seqid may be zero depends on the state type
     exclusively, See RFC 5661 pp. 161,287-288. */
  if((state2->state_type == STATE_TYPE_LAYOUT) ||
     (stateid->seqid != 0))
    {
      /* Check seqid in stateid */
      /**
       * @todo fsf: maybe change to simple comparison stateid->seqid < state2->state_seqid
       *       as good enough and maybe makes pynfs happy.
       */
      diff = stateid->seqid - state2->state_seqid;
      if(diff < 0)
        {
          /* if this is NFSv4.0 and stateid's seqid is one less than current */
          /* AND if owner_seqid is current */
          /* pass state back to allow replay check */
          if((check_seqid) &&
             ((diff == -1) ||
              ((state2->state_seqid == 1) &&
               (stateid->seqid == seqid_all_one))) &&
             (owner_seqid == state2->state_owner->so_owner.so_nfs4_owner
                             .so_seqid))
          {
             LogDebug(COMPONENT_STATE,
                   "possible replay?");
             *state = state2;
             return NFS4ERR_REPLAY;
          }
          /* OLD_STATEID */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found OLD stateid %s, expected seqid %u",
                   tag, str, (unsigned int) state2->state_seqid);
          return NFS4ERR_OLD_STATEID;
        }
      /* stateid seqid is current and owner seqid is previous, replay (should be
         an error condition that did not change the stateid, no real need to check
         since the operation must be the same) */
      else if((diff == 0) &&
              (check_seqid) &&
              (owner_seqid == state2->state_owner->so_owner.so_nfs4_owner
                              .so_seqid))
        {
          LogDebug(COMPONENT_STATE,
                   "possible replay?");
          *state = state2;
          return NFS4ERR_REPLAY;
        }
      else if(diff > 0)
        {
          /* BAD_STATEID */
          LogDebug(COMPONENT_STATE,
                   "Check %s stateid found BAD stateid %s, expected seqid %u",
                   tag, str, (unsigned int) state2->state_seqid);
          return NFS4ERR_BAD_STATEID;
        }
    }

  LogFullDebug(COMPONENT_STATE,
               "Check %s stateid found valid stateid %s - %p",
               tag, str, state2);

  /* Copy stateid into current for later use */
  data->current_stateid = *stateid;
  data->current_stateid.seqid = state2->state_seqid;
  data->current_stateid_valid = true;

  *state = state2;
  return NFS4_OK;
}

/**
 * @brief Display the stateid table
 */

void nfs_State_PrintAll(void)
{
  if(isFullDebug(COMPONENT_STATE))
    HashTable_Log(COMPONENT_STATE, ht_state_id);
}

/**
 * @brief Update stateid and set current
 *
 * We increment the seqid, handling wraparound, and copy the id into
 * the response.
 *
 * @param[in,out] state State to update
 * @param[out]    resp  Stateid in response
 * @param[in,out] data  Compound data to upddate with current stateid
 *                      (may be NULL)
 * @param[in]     tag   Arbitrary text for debug/log
 */
void update_stateid(state_t *state,
                    stateid4 *resp,
                    compound_data_t *data,
                    const char *tag)
{
  /* Increment state_seqid, handling wraparound */
  state->state_seqid += 1;
  if(state->state_seqid == 0)
    state->state_seqid = 1;

  /* Copy stateid into current for later use */
  if (data) {
	  data->current_stateid.seqid = state->state_seqid;
	  memcpy(data->current_stateid.other, state->stateid_other, OTHERSIZE);
	  data->current_stateid_valid = true;
  }

  /* Copy stateid into response */
  resp->seqid = state->state_seqid;
  memcpy(resp->other, state->stateid_other, OTHERSIZE);

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[OTHERSIZE * 2 + 1 + 6];
      sprint_mem(str, (char *)state->stateid_other, OTHERSIZE);
      sprintf(str + OTHERSIZE * 2, ":%u", (unsigned int) state->state_seqid);
      LogDebug(COMPONENT_STATE,
               "Update %s stateid to %s for response",
               tag, str);
    }
}

/**
 * @brief Check to see if any share conflicts with anonymous stateid
 *
 * @param[in] entry  File to check
 * @param[in] tag    Arbitrary debug/log string
 * @param[in] access Access sought
 * 
 * @return NFS4 response codes
 */
nfsstat4 nfs4_check_special_stateid(cache_entry_t *entry,
				    const char *tag,
				    int access)
{

  struct glist_head * glist;
  state_t           * state_iterate;
  int                 rc = NFS4_OK;

  if(entry == NULL)
    {
      rc = NFS4ERR_SERVERFAULT;
      return rc;
    }

  /* Acquire lock to enter critical section on this entry */
  PTHREAD_RWLOCK_rdlock(&entry->state_lock);

  /* Iterate through file's state to look for conflicts */
  glist_for_each(glist, &entry->state_list)
    {
      state_iterate = glist_entry(glist, state_t, state_list);

      switch(state_iterate->state_type)
        {
          case STATE_TYPE_SHARE:
            if((access == FATTR4_ATTR_READ) &&
               (state_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_READ))
              {
                /* Reading to this file is prohibited, file is read-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         state_iterate);
                goto ssid_out;
              }

            if((access == FATTR4_ATTR_WRITE) &&
               (state_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE))
              {
                /* Writing to this file is prohibited, file is write-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         state_iterate);
                goto ssid_out;
              }

            if((access == FATTR4_ATTR_READ_WRITE) &&
               (state_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_BOTH))
              {
                /* Reading and writing to this file is prohibited, file is rw-denied */
                rc = NFS4ERR_LOCKED;
                LogDebug(COMPONENT_NFS_V4_LOCK,
                         "%s is denied by state %p",
                         tag,
                         state_iterate);
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
  PTHREAD_RWLOCK_unlock(&entry->state_lock);
  return rc;
}
/** @} */
