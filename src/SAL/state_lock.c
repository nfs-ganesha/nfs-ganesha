/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
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
 * \file    state_lock.c
 * \brief   This file contains functions used in lock management.
 *
 * This file contains functions used in lock management.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs4.h"
#include "sal_functions.h"
#ifdef _USE_NLM
#include "nlm_util.h"
#endif
#include "cache_inode_lru.h"

/*
 * state_lock_entry_t locking rule:
 * The value is always updated/read with nlm_lock_entry->lock held
 * If we have nlm_lock_list mutex held we can read it safely, because the
 * value is always updated while walking the list with pentry->state_lock held.
 * The updation happens as below:
 *  pthread_rwlock_wrlock(&pentry->state_mutex)
 *  pthread_mutex_lock(lock_entry->sle_mutex)
 *    update the lock_entry value
 *  ........
 * The value is ref counted with nlm_lock_entry->sle_ref_count so that a
 * parallel cancel/unlock won't endup freeing the datastructure. The last
 * release on the data structure ensure that it is freed.
 */

/* The following constant defines the number of errors we will accept
 * before giving up in state recovery routines. These routines need to
 * terminate at some point.
 */
#define STATE_ERR_MAX 100

#ifdef _DEBUG_MEMLEAKS
static struct glist_head state_all_locks;
pthread_mutex_t all_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef _USE_BLOCKING_LOCKS

/* List of all locks blocked in FSAL */
struct glist_head state_blocked_locks;

/* List of all async blocking locks notified by FSAL but not processed */
struct glist_head state_notified_locks;

/* Mutex to protect above lists */
pthread_mutex_t blocked_locks_mutex = PTHREAD_MUTEX_INITIALIZER;

#endif

state_owner_t unknown_owner;

#ifdef _USE_BLOCKING_LOCKS
hash_table_t *ht_lock_cookies;

state_status_t state_lock_init(state_status_t   * pstatus,
                               hash_parameter_t   cookie_param)
#else
state_status_t state_lock_init(state_status_t * pstatus)
#endif
{
  *pstatus = STATE_SUCCESS;

  memset(&unknown_owner, 0, sizeof(unknown_owner));
  unknown_owner.so_owner_val = "ganesha_unknown_owner";
  unknown_owner.so_type      = STATE_LOCK_OWNER_UNKNOWN;
  unknown_owner.so_refcount  = 1;
  unknown_owner.so_owner_len = strlen(unknown_owner.so_owner_val);

  init_glist(&unknown_owner.so_lock_list);

  if(pthread_mutex_init(&unknown_owner.so_mutex, NULL) == -1)
    {
      *pstatus = STATE_INIT_ENTRY_FAILED;
      return *pstatus;
    }

#ifdef _USE_BLOCKING_LOCKS
  ht_lock_cookies = HashTable_Init(&cookie_param);
  if(ht_lock_cookies == NULL)
    {
      LogCrit(COMPONENT_STATE,
              "Cannot init NLM Client cache");
      *pstatus = STATE_INIT_ENTRY_FAILED;
      return *pstatus;
    }
#endif

#ifdef _DEBUG_MEMLEAKS
  init_glist(&state_all_locks);
  init_glist(&state_owners_all);
  init_glist(&state_v4_all);
#endif
 
#ifdef _USE_BLOCKING_LOCKS
  init_glist(&state_blocked_locks);
  init_glist(&state_notified_locks);

  *pstatus = state_async_init();
#endif

  state_owner_pool = pool_init("NFSv4 state owners",
                               sizeof(state_owner_t),
                               pool_basic_substrate,
                               NULL, NULL, NULL);

  state_v4_pool = pool_init("NFSv4 files states",
                            sizeof(state_t),
                            pool_basic_substrate,
                            NULL, NULL, NULL);
  return *pstatus;
}

bool_t lock_owner_is_nlm(state_lock_entry_t * lock_entry)
{
#ifdef _USE_NLM
  return lock_entry->sle_owner->so_type == STATE_LOCK_OWNER_NLM;
#else
  return FALSE;
#endif
}

state_status_t do_lock_op(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          exportlist_t         * pexport,
                          fsal_lock_op_t         lock_op,
                          bool_t                 is_reclaim,
                          state_owner_t        * powner,
                          fsal_lock_param_t    * plock,
                          state_owner_t       ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t    * conflict, /* description of conflicting lock */
                          bool_t                 overlap);  /* hint that lock overlaps */

/******************************************************************************
 *
 * Functions to display various aspects of a lock
 *
 ******************************************************************************/
static inline uint64_t lock_end(fsal_lock_param_t *plock)
{
  if(plock->lock_length == 0)
    return UINT64_MAX;
  else
    return plock->lock_start + plock->lock_length - 1;
}

const char *str_lockt(fsal_lock_t ltype)
{
  switch(ltype)
    {
      case FSAL_LOCK_R:  return "READ ";
      case FSAL_LOCK_W:  return "WRITE";
      case FSAL_NO_LOCK: return "NO LOCK";
      default:           return "?????";
    }
  return "????";
}

const char *str_blocking(state_blocking_t blocking)
{
  switch(blocking)
    {
      case STATE_NON_BLOCKING:   return "NON_BLOCKING  ";
      case STATE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case STATE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      case STATE_GRANTING:       return "GRANTING      ";
      case STATE_CANCELED:       return "CANCELED      ";
    }
  return "unknown       ";
}

const char *str_blocked(state_blocking_t blocked)
{
  switch(blocked)
    {
      case STATE_NON_BLOCKING:   return "GRANTED       ";
      case STATE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case STATE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      case STATE_GRANTING:       return "GRANTING      ";
      case STATE_CANCELED:       return "CANCELED      ";
    }
  return "unknown       ";
}

/******************************************************************************
 *
 * Function to compare lock parameters
 *
 ******************************************************************************/

/* This is not complete, it doesn't check the owner's IP address...*/
static inline int different_lock(fsal_lock_param_t *lock1, fsal_lock_param_t *lock2)
{
  return (lock1->lock_type   != lock2->lock_type)  ||
         (lock1->lock_start  != lock2->lock_start) ||
         (lock1->lock_length != lock2->lock_length);
}

/******************************************************************************
 *
 * Functions to log locks in various ways
 *
 ******************************************************************************/
static void LogEntry(const char         *reason,
                     state_lock_entry_t *ple)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      char                  owner[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(owner), owner, owner};

      (void) display_owner(&dspbuf, ple->sle_owner);

      LogFullDebug(COMPONENT_STATE,
                   "%s Entry: %p pentry=%p, fileid=%"PRIu64", export=%u, type=%s, start=0x%llx, end=0x%llx, blocked=%s/%p, state=%p, refcount=%d, owner={%s}",
                   reason, ple,
                   ple->sle_pentry, (uint64_t)ple->sle_pentry->attributes.fileid,
                   (unsigned int) ple->sle_pexport->id,
                   str_lockt(ple->sle_lock.lock_type),
                   (unsigned long long) ple->sle_lock.lock_start,
                   (unsigned long long) lock_end(&ple->sle_lock),
                   str_blocked(ple->sle_blocked),
                   ple->sle_block_data,
                   ple->sle_state,
                   ple->sle_ref_count,
                   owner);
    }
}

static bool_t LogList(const char        * reason,
                      cache_entry_t     * pentry,
                      struct glist_head * list)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      struct glist_head  * glist;
      state_lock_entry_t * found_entry;

      if(glist_empty(list))
        {
          if(pentry != NULL)
            LogFullDebug(COMPONENT_STATE,
                         "%s for %p is empty",
                         reason, pentry);
          else
            LogFullDebug(COMPONENT_STATE,
                         "%s is empty",
                         reason);
          return TRUE;
        }


      glist_for_each(glist, list)
        {
          found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
          LogEntry(reason, found_entry);
          if(found_entry->sle_pentry == NULL)
            break;
        }
    }

  return FALSE;
}

#ifdef _USE_BLOCKING_LOCKS
static bool_t LogBlockedList(const char        * reason,
                             cache_entry_t     * pentry,
                             struct glist_head * list)
{
  if(isFullDebug(COMPONENT_STATE))
    {
      struct glist_head  * glist;
      state_lock_entry_t * found_entry;
      state_block_data_t * block_entry;

      if(glist_empty(list))
        {
          if(pentry != NULL)
            LogFullDebug(COMPONENT_STATE,
                         "%s for %p is empty",
                         reason, pentry);
          else
            LogFullDebug(COMPONENT_STATE,
                         "%s is empty",
                         reason);
          return TRUE;
        }


      glist_for_each(glist, list)
        {
          block_entry = glist_entry(glist, state_block_data_t, sbd_list);
          found_entry = block_entry->sbd_lock_entry;
          LogEntry(reason, found_entry);
          if(found_entry->sle_pentry == NULL)
            break;
        }
    }

  return FALSE;
}
#endif /* _USE_BLOCKING_LOCKS */

void LogLock(log_components_t     component,
             log_levels_t         debug,
             const char         * reason,
             cache_entry_t      * pentry,
             fsal_op_context_t  * pcontext,
             state_owner_t      * powner,
             fsal_lock_param_t  * plock)
{
  if(isLevel(component, debug))
    {
      char                  owner[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(owner), owner, owner};

      if(powner != NULL)
        (void) display_owner(&dspbuf, powner);
      else
        (void) display_cat(&dspbuf, "NONE");

      LogAtLevel(component, debug,
                 "%s Lock: pentry=%p, fileid=%"PRIu64", type=%s, start=0x%llx, end=0x%llx, owner={%s}",
                 reason, pentry, (uint64_t)pentry->attributes.fileid,
                 str_lockt(plock->lock_type),
                 (unsigned long long) plock->lock_start,
                 (unsigned long long) lock_end(plock),
                 owner);
    }
}

void LogLockDesc(log_components_t     component,
                 log_levels_t         debug,
                 const char         * reason,
                 cache_entry_t      * pentry,
                 void               * powner,
                 fsal_lock_param_t  * plock)
{
  LogAtLevel(component, debug,
             "%s Lock: pentry=%p, powner=%p, type=%s, start=0x%llx, end=0x%llx",
             reason,
             pentry,
             powner,
             str_lockt(plock->lock_type),
             (unsigned long long) plock->lock_start,
             (unsigned long long) lock_end(plock));
}

void dump_all_locks(const char * label)
{
#ifdef _DEBUG_MEMLEAKS
  struct glist_head *glist;

  P(all_locks_mutex);

  if(glist_empty(&state_all_locks))
    {
      LogFullDebug(COMPONENT_STATE, "All Locks are freed");
      V(all_locks_mutex);
      return;
    }

  glist_for_each(glist, &state_all_locks)
    LogEntry(label, glist_entry(glist, state_lock_entry_t, sle_all_locks));

  V(all_locks_mutex);
#else
  return;
#endif
}

/******************************************************************************
 *
 * Functions to manage lock entries and lock list
 *
 ******************************************************************************/
static state_lock_entry_t *create_state_lock_entry(cache_entry_t      * pentry,
                                                   fsal_op_context_t  * pcontext,
                                                   exportlist_t       * pexport,
                                                   state_blocking_t     blocked,
                                                   state_owner_t      * powner,
                                                   state_t            * pstate,
                                                   fsal_lock_param_t  * plock)
{
  state_lock_entry_t *new_entry;

  new_entry = gsh_malloc(sizeof(*new_entry));
  if(!new_entry)
      return NULL;

  LogFullDebug(COMPONENT_STATE,
               "new_entry = %p", new_entry);

  memset(new_entry, 0, sizeof(*new_entry));

  if(pthread_mutex_init(&new_entry->sle_mutex, NULL) == -1)
    {
      gsh_free(new_entry);
      return NULL;
    }

  new_entry->sle_ref_count  = 1;
  new_entry->sle_pentry     = pentry;
  new_entry->sle_blocked    = blocked;
  new_entry->sle_owner      = powner;
  new_entry->sle_state      = pstate;
  new_entry->sle_block_data = NULL;   /* will be filled in later if necessary */
  new_entry->sle_lock       = *plock;
  new_entry->sle_pexport    = pexport;

#ifdef _USE_NLM
  if(powner->so_type == STATE_LOCK_OWNER_NLM)
    {
      /* Add to list of locks owned by client that powner belongs to */
      inc_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

      P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      glist_add_tail(&powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_lock_list,
                     &new_entry->sle_client_locks);

      V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

      /* Add to list of locks owned by export */
      P(pexport->exp_state_mutex);
      glist_add_tail(&pexport->exp_lock_list,
                     &new_entry->sle_export_locks);
      V(pexport->exp_state_mutex);
    }
#endif
  /* Add to list of locks owned by powner */
  inc_state_owner_ref(powner);

  P(powner->so_mutex);

  if(powner->so_type == STATE_LOCK_OWNER_NFSV4 && pstate != NULL)
    {
      glist_add_tail(&pstate->state_data.lock.state_locklist,
                     &new_entry->sle_state_locks);
    }

  glist_add_tail(&powner->so_lock_list, &new_entry->sle_owner_locks);

  V(powner->so_mutex);

#ifdef _DEBUG_MEMLEAKS
  P(all_locks_mutex);

  glist_add_tail(&state_all_locks, &new_entry->sle_all_locks);

  V(all_locks_mutex);
#endif

  return new_entry;
}

inline state_lock_entry_t *state_lock_entry_t_dup(fsal_op_context_t  * pcontext,
                                                  state_lock_entry_t * orig_entry)
{
  return create_state_lock_entry(orig_entry->sle_pentry,
                                 pcontext,
                                 orig_entry->sle_pexport,
                                 orig_entry->sle_blocked,
                                 orig_entry->sle_owner,
                                 orig_entry->sle_state,
                                 &orig_entry->sle_lock);
}

void lock_entry_inc_ref(state_lock_entry_t *lock_entry)
{
    P(lock_entry->sle_mutex);
    lock_entry->sle_ref_count++;
    LogEntry("Increment refcount", lock_entry);
    V(lock_entry->sle_mutex);
}

#ifdef _USE_BLOCKING_LOCKS
void free_block_data(state_block_data_t * block_data)
{
  if(block_data == NULL)
    return;

  if(block_data->sbd_credential.alt_groups != NULL)
    gsh_free(block_data->sbd_credential.alt_groups);

  gsh_free(block_data);
}
#endif

void lock_entry_dec_ref(state_lock_entry_t *lock_entry)
{
  bool_t to_free = FALSE;

  P(lock_entry->sle_mutex);

  lock_entry->sle_ref_count--;

    LogEntry("Decrement refcount", lock_entry);

  if(!lock_entry->sle_ref_count)
    {
      /*
       * We should already be removed from the lock_list
       * So we can free the lock_entry without any locking
       */
      to_free = TRUE;
    }

  V(lock_entry->sle_mutex);

  if(to_free)
    {
      LogEntry("Freeing", lock_entry);

#ifdef _USE_BLOCKING_LOCKS
      /* Release block data if present */
      if(lock_entry->sle_block_data != NULL)
        {
          /* need to remove from the state_blocked_locks list */
          if (lock_entry->sle_block_data != NULL)
            glist_del(&lock_entry->sle_block_data->sbd_list);
          free_block_data(lock_entry->sle_block_data);
        }
#endif
#ifdef _DEBUG_MEMLEAKS
      P(all_locks_mutex);
      glist_del(&lock_entry->sle_all_locks);
      V(all_locks_mutex);
#endif

      gsh_free(lock_entry);
    }
}

static void remove_from_locklist(state_lock_entry_t   * lock_entry)
{
  state_owner_t * powner = lock_entry->sle_owner;

  LogEntry("Removing", lock_entry);

  /*
   * If some other thread is holding a reference to this nlm_lock_entry
   * don't free the structure. But drop from the lock list
   */
  if(powner != NULL)
    {
#ifdef _USE_NLM
      if(powner->so_type == STATE_LOCK_OWNER_NLM)
        {
          /* Remove from list of locks owned by client that powner belongs to */
          P(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

          glist_del(&lock_entry->sle_client_locks);

          V(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client->ssc_mutex);

          dec_nsm_client_ref(powner->so_owner.so_nlm_owner.so_client->slc_nsm_client);

          /* Remove from list of locks owned by export */
          P(lock_entry->sle_pexport->exp_state_mutex);
          glist_del(&lock_entry->sle_export_locks);
          V(lock_entry->sle_pexport->exp_state_mutex);
        }
#endif

      /* Remove from list of locks owned by powner */
      P(powner->so_mutex);

      if(powner->so_type == STATE_LOCK_OWNER_NFSV4)
        {
          glist_del(&lock_entry->sle_state_locks);
        }

      glist_del(&lock_entry->sle_owner_locks);

      V(powner->so_mutex);

      dec_state_owner_ref(powner);
    }

  lock_entry->sle_owner = NULL;
  glist_del(&lock_entry->sle_list);
  lock_entry_dec_ref(lock_entry);
}

static state_lock_entry_t *get_overlapping_entry(cache_entry_t     * pentry,
                                                 fsal_op_context_t * pcontext,
                                                 state_owner_t     * powner,
                                                 fsal_lock_param_t * plock)
{
  struct glist_head *glist;
  state_lock_entry_t *found_entry = NULL;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      LogEntry("Checking", found_entry);

      /* Skip blocked or cancelled locks */
      if(found_entry->sle_blocked == STATE_NLM_BLOCKING ||
         found_entry->sle_blocked == STATE_NFSV4_BLOCKING ||
         found_entry->sle_blocked == STATE_CANCELED)
          continue;

      found_entry_end = lock_end(&found_entry->sle_lock);

      if((found_entry_end >= plock->lock_start) &&
         (found_entry->sle_lock.lock_start <= plock_end))
        {
          /* lock overlaps see if we can allow
           * allow if neither lock is exclusive or the owner is the same
           */
          if((found_entry->sle_lock.lock_type == FSAL_LOCK_W ||
              plock->lock_type == FSAL_LOCK_W) &&
             different_owners(found_entry->sle_owner, powner)
             )
            {
              /* found a conflicting lock, return it */
              return found_entry;
            }
        }
    }

  return NULL;
}

/* We need to iterate over the full lock list and remove
 * any mapping entry. And l_offset = 0 and sle_lock.lock_length = 0 lock_entry
 * implies remove all entries
 */
static void merge_lock_entry(cache_entry_t        * pentry,
                             fsal_op_context_t    * pcontext,
                             state_lock_entry_t   * lock_entry)
{
  state_lock_entry_t * check_entry;
  state_lock_entry_t * check_entry_right;
  uint64_t             check_entry_end;
  uint64_t             lock_entry_end;
  struct glist_head  * glist;
  struct glist_head  * glistn;

  /* lock_entry might be STATE_NON_BLOCKING or STATE_GRANTING */

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      check_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      /* Skip entry being merged - it could be in the list */
      if(check_entry == lock_entry)
        continue;

      if(different_owners(check_entry->sle_owner, lock_entry->sle_owner))
        continue;

      /* Only merge fully granted locks */
      if(check_entry->sle_blocked != STATE_NON_BLOCKING)
        continue;

      check_entry_end = lock_end(&check_entry->sle_lock);
      lock_entry_end  = lock_end(&lock_entry->sle_lock);

      if((check_entry_end + 1) < lock_entry->sle_lock.lock_start)
        /* nothing to merge */
        continue;

      if((lock_entry_end + 1) < check_entry->sle_lock.lock_start)
        /* nothing to merge */
        continue;

      /* Need to handle locks of different types differently, may split an old lock.
       * If new lock totally overlaps old lock, the new lock will replace the old
       * lock so no special work to be done.
       */
      if((check_entry->sle_lock.lock_type != lock_entry->sle_lock.lock_type) &&
         ((lock_entry_end < check_entry_end) ||
          (check_entry->sle_lock.lock_start < lock_entry->sle_lock.lock_start)))
        {
          if(lock_entry_end < check_entry_end &&
             check_entry->sle_lock.lock_start < lock_entry->sle_lock.lock_start)
            {
              /* Need to split old lock */
              check_entry_right = state_lock_entry_t_dup(pcontext, check_entry);
              if(check_entry_right == NULL)
                {
                  // TODO FSF: OOPS....
                  // Leave old lock in place, it may cause false conflicts, but should eventually be released
                  LogMajor(COMPONENT_STATE,
                           "Memory allocation failure during lock upgrade/downgrade");
                  continue;
                }
              glist_add_tail(&pentry->object.file.lock_list, &(check_entry_right->sle_list));
            }
          else
            {
              /* No split, just shrink, make the logic below work on original lock */
              check_entry_right = check_entry;
            }
          if(lock_entry_end < check_entry_end)
            {
              /* Need to shrink old lock from beginning (right lock if split) */
              LogEntry("Merge shrinking right", check_entry_right);
              check_entry_right->sle_lock.lock_start  = lock_entry_end + 1;
              check_entry_right->sle_lock.lock_length = check_entry_end - lock_entry_end;
              LogEntry("Merge shrunk right", check_entry_right);
            }
          if(check_entry->sle_lock.lock_start < lock_entry->sle_lock.lock_start)
            {
              /* Need to shrink old lock from end (left lock if split) */
              LogEntry("Merge shrinking left", check_entry);
              check_entry->sle_lock.lock_length = lock_entry->sle_lock.lock_start - check_entry->sle_lock.lock_start;
              LogEntry("Merge shrunk left", check_entry);
            }
          /* Done splitting/shrinking old lock */
          continue;
        }

      /* check_entry touches or overlaps lock_entry, expand lock_entry */
      if(lock_entry_end < check_entry_end)
        /* Expand end of lock_entry */
        lock_entry_end = check_entry_end;

      if(check_entry->sle_lock.lock_start < lock_entry->sle_lock.lock_start)
        /* Expand start of lock_entry */
        lock_entry->sle_lock.lock_start = check_entry->sle_lock.lock_start;

      /* Compute new lock length */
      lock_entry->sle_lock.lock_length = lock_entry_end - lock_entry->sle_lock.lock_start + 1;

      /* Remove merged entry */
      LogEntry("Merged", lock_entry);
      LogEntry("Merging removing", check_entry);
      remove_from_locklist(check_entry);
    }
}

static void free_list(struct glist_head    * list)
{
  state_lock_entry_t *found_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      remove_from_locklist(found_entry);
    }
}

/* Subtract a lock from a lock entry, placing any remaining bits into the split list. */
static bool_t subtract_lock_from_entry(cache_entry_t        * pentry,
                                       fsal_op_context_t    * pcontext,
                                       state_lock_entry_t   * found_entry,
                                       fsal_lock_param_t    * plock,
                                       struct glist_head    * split_list,
                                       struct glist_head    * remove_list,
                                       state_status_t       * pstatus)
{
  uint64_t found_entry_end = lock_end(&found_entry->sle_lock);
  uint64_t plock_end = lock_end(plock);
  state_lock_entry_t *found_entry_left = NULL;
  state_lock_entry_t *found_entry_right = NULL;

  *pstatus = STATE_SUCCESS;

  if(plock_end < found_entry->sle_lock.lock_start)
    /* nothing to split */
    return FALSE;

  if(found_entry_end < plock->lock_start)
    /* nothing to split */
    return FALSE;

  if((plock->lock_start <= found_entry->sle_lock.lock_start) &&
     plock_end >= found_entry_end)
    {
      /* Fully overlap */
      LogEntry("Remove Complete", found_entry);
      goto complete_remove;
    }

  LogEntry("Split", found_entry);

  /* Delete the old entry and add one or two new entries */
  if(plock->lock_start > found_entry->sle_lock.lock_start)
    {
      found_entry_left = state_lock_entry_t_dup(pcontext, found_entry);
      if(found_entry_left == NULL)
        {
          free_list(split_list);
          *pstatus = STATE_MALLOC_ERROR;
          return FALSE;
        }

      found_entry_left->sle_lock.lock_length = plock->lock_start - found_entry->sle_lock.lock_start;
      LogEntry("Left split", found_entry_left);
      glist_add_tail(split_list, &(found_entry_left->sle_list));
    }

  if(plock_end < found_entry_end)
    {
      found_entry_right = state_lock_entry_t_dup(pcontext, found_entry);
      if(found_entry_right == NULL)
        {
          free_list(split_list);
          *pstatus = STATE_MALLOC_ERROR;
          return FALSE;
        }

      found_entry_right->sle_lock.lock_start  = plock_end + 1;
      found_entry_right->sle_lock.lock_length = found_entry_end - plock_end;
      LogEntry("Right split", found_entry_right);
      glist_add_tail(split_list, &(found_entry_right->sle_list));
    }

complete_remove:

  /* Remove the lock from the list it's on and put it on the remove_list */
  glist_del(&found_entry->sle_list);
  glist_add_tail(remove_list, &(found_entry->sle_list));

  return TRUE;
}

/* Subtract a lock from a list of locks, possibly splitting entries in the list. */
static bool_t subtract_lock_from_list(cache_entry_t        * pentry,
                                      fsal_op_context_t    * pcontext,
                                      state_owner_t        * powner,
                                      state_t              * pstate,
                                      fsal_lock_param_t    * plock,
                                      state_status_t       * pstatus,
                                      struct glist_head    * list)
{
  state_lock_entry_t *found_entry;
  struct glist_head split_lock_list, remove_list;
  struct glist_head *glist, *glistn;
  bool_t rc = FALSE;

  init_glist(&split_lock_list);
  init_glist(&remove_list);

  *pstatus = STATE_SUCCESS;
  glist_for_each_safe(glist, glistn, list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      if(powner != NULL && different_owners(found_entry->sle_owner, powner))
        continue;

      /* Only care about granted locks */
      if(found_entry->sle_blocked != STATE_NON_BLOCKING)
        continue;


#ifdef _USE_NLM
      /* Skip locks owned by this NLM state.
       * This protects NLM locks from the current iteration of an NLM
       * client from being released by SM_NOTIFY.
       */
      if(pstate != NULL &&
         lock_owner_is_nlm(found_entry) &&
         found_entry->sle_state == pstate)
        continue;
#endif

      /*
       * We have matched owner.
       * Even though we are taking a reference to found_entry, we
       * don't inc the ref count because we want to drop the lock entry.
       */
      rc |= subtract_lock_from_entry(pentry,
                                     pcontext,
                                     found_entry,
                                     plock,
                                     &split_lock_list,
                                     &remove_list,
                                     pstatus);
      if(*pstatus != STATE_SUCCESS)
        {
          /* We ran out of memory while splitting, deal with it outside loop */
          break;
        }
    }

  if(*pstatus != STATE_SUCCESS)
    {
      /* We ran out of memory while splitting. split_lock_list has been freed.
       * For each entry on the remove_list, put it back on the list.
       */
      LogDebug(COMPONENT_STATE,
               "Failed %s",
               state_err_str(*pstatus));
      glist_for_each_safe(glist, glistn, &remove_list)
        {
          found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
          glist_del(&found_entry->sle_list);
          glist_add_tail(list, &(found_entry->sle_list));
        }
    }
  else
    {
      /* free the enttries on the remove_list*/
      free_list(&remove_list);

      /* now add the split lock list */
      glist_add_list_tail(list, &split_lock_list);
    }

  LogFullDebug(COMPONENT_STATE,
               "List of all locks for pentry=%p returning %d",
               pentry, (int) rc);

  return rc;
}

static state_status_t subtract_list_from_list(cache_entry_t        * pentry,
                                              fsal_op_context_t    * pcontext,
                                              struct glist_head    * target,
                                              struct glist_head    * source,
                                              state_status_t       * pstatus)
{
  state_lock_entry_t *found_entry;
  struct glist_head *glist, *glistn;

  *pstatus = STATE_SUCCESS;

  glist_for_each_safe(glist, glistn, source)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      subtract_lock_from_list(pentry,
                              pcontext,
                              NULL,
                              NULL,
                              &found_entry->sle_lock,
                              pstatus,
                              target);
      if(*pstatus != STATE_SUCCESS)
        break;
    }

  return *pstatus;
}

/******************************************************************************
 *
 * Implement hash table to hash blocked lock entries by cookie
 *
 ******************************************************************************/

#ifdef _USE_BLOCKING_LOCKS
static void grant_blocked_locks(cache_entry_t        * pentry,
                                fsal_op_context_t    * pcontext);

int display_lock_cookie_key(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff)
{
  return display_opaque_value(dspbuf, pbuff->pdata, pbuff->len);
}

int display_lock_cookie_entry(struct display_buffer * dspbuf,
                              state_cookie_entry_t  * he)
{
  int b_left;

  b_left = display_printf(dspbuf, "%p: cookie ", he);

  if(b_left <= 0)
    return b_left;

  b_left = display_opaque_value(dspbuf, he->sce_pcookie, he->sce_cookie_size);

  if(b_left <= 0)
    return b_left;

  b_left = display_printf(dspbuf, " entry {%p fileid=%"PRIu64"} lock {",
                          he->sce_pentry,
                          (uint64_t)he->sce_pentry->attributes.fileid);

  if(b_left <= 0)
    return b_left;

  if(he->sce_lock_entry != NULL)
    {
      b_left = display_printf(dspbuf, "%p owner {", he->sce_lock_entry);

      if(b_left <= 0)
        return b_left;

      b_left = display_owner(dspbuf, he->sce_lock_entry->sle_owner);

      if(b_left <= 0)
        return b_left;

      return display_printf(dspbuf, "} type=%s start=0x%llx end=0x%llx blocked=%s}",
                            str_lockt(he->sce_lock_entry->sle_lock.lock_type),
                            (unsigned long long) he->sce_lock_entry->sle_lock.lock_start,
                            (unsigned long long) lock_end(&he->sce_lock_entry->sle_lock),
                            str_blocked(he->sce_lock_entry->sle_blocked));
    }
  else
    {
      return display_printf(dspbuf, "<NULL>}");
    }
}

int display_lock_cookie_val(struct display_buffer * dspbuf,
                            hash_buffer_t         * pbuff)
{
  return display_lock_cookie_entry(dspbuf, pbuff->pdata);
}

int compare_lock_cookie_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      char                  str1[LOG_BUFF_LEN / 2];
      char                  str2[LOG_BUFF_LEN / 2];
      struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
      struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

      (void) display_lock_cookie_key(&dspbuf1, buff1);
      (void) display_lock_cookie_key(&dspbuf2, buff2);
      LogFullDebug(COMPONENT_STATE,
                   "{%s} vs {%s}", str1, str2);
    }

  if(buff1->pdata == buff2->pdata)
    return 0;

  if(buff1->len != buff2->len)
    return 1;

  if(buff1->pdata == NULL || buff2->pdata == NULL)
    return 1;

  return memcmp(buff1->pdata, buff2->pdata, buff1->len);
}

uint32_t lock_cookie_value_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  unsigned char *pdata = (unsigned char *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < buffclef->len; i++)
    sum +=(unsigned char) pdata[i];

  res = (unsigned long) sum +
        (unsigned long) buffclef->len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE,
                 "value = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);
}

uint64_t lock_cookie_rbt_hash_func(hash_parameter_t * p_hparam,
                                   hash_buffer_t * buffclef)
{
  unsigned int sum = 0;
  unsigned int i;
  unsigned long res;
  unsigned char *pdata = (unsigned char *) buffclef->pdata;

  /* Compute the sum of all the characters */
  for(i = 0; i < buffclef->len; i++)
    sum +=(unsigned char) pdata[i];

  res = (unsigned long) sum +
        (unsigned long) buffclef->len;

  if(isDebug(COMPONENT_HASHTABLE))
    LogFullDebug(COMPONENT_STATE, "rbt = %lu", res);

  return res;
}

void free_cookie(state_cookie_entry_t * p_cookie_entry,
                 bool_t                 unblock)
{
  char                    str[LOG_BUFF_LEN];
  struct display_buffer   dspbuf = {sizeof(str), str, str};
  void                  * pcookie = p_cookie_entry->sce_pcookie;

  if(isFullDebug(COMPONENT_STATE))
    (void) display_lock_cookie_entry(&dspbuf, p_cookie_entry);

  /* Since the cookie is not in the hash table, we can just free the memory */
  LogFullDebug(COMPONENT_STATE,
               "Free Lock Cookie {%s}",
               str);

  /* If block data is still attached to lock entry, remove it */
  if(p_cookie_entry->sce_lock_entry != NULL && unblock)
    {
      if(p_cookie_entry->sce_lock_entry->sle_block_data != NULL)
        p_cookie_entry->sce_lock_entry->sle_block_data->sbd_blocked_cookie = NULL;

      lock_entry_dec_ref(p_cookie_entry->sce_lock_entry);
    }

  /* Free the memory for the cookie and the cookie entry */
  gsh_free(pcookie);
  gsh_free(p_cookie_entry);
}

state_status_t state_add_grant_cookie(cache_entry_t         * pentry,
                                      fsal_op_context_t     * pcontext,
                                      void                  * pcookie,
                                      int                     cookie_size,
                                      state_lock_entry_t    * lock_entry,
                                      state_cookie_entry_t ** ppcookie_entry,
                                      state_status_t        * pstatus)
{
  hash_buffer_t          buffkey, buffval;
  state_cookie_entry_t * hash_entry;
  char                   str[LOG_BUFF_LEN];
  struct display_buffer  dspbuf = {sizeof(str), str, str};

  *ppcookie_entry = NULL;

  if(lock_entry->sle_block_data == NULL || pcookie == NULL || cookie_size == 0)
    {
      /* Something's wrong with this entry */
      *pstatus = STATE_INCONSISTENT_ENTRY;
      return *pstatus;
    }

  if(isFullDebug(COMPONENT_STATE))
    (void) display_opaque_value(&dspbuf, pcookie, cookie_size);

  hash_entry = gsh_malloc(sizeof(*hash_entry));
  if(hash_entry == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s} NO MEMORY",
                   str);
      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
    }

  memset(hash_entry, 0, sizeof(*hash_entry));

  buffkey.pdata = gsh_malloc(cookie_size);
  if(buffkey.pdata == NULL)
    {
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s} NO MEMORY",
                   str);
      gsh_free(hash_entry);
      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
    }

  hash_entry->sce_pentry      = pentry;
  hash_entry->sce_lock_entry  = lock_entry;
  hash_entry->sce_pcookie     = buffkey.pdata;
  hash_entry->sce_cookie_size = cookie_size;

  memcpy(buffkey.pdata, pcookie, cookie_size);
  buffkey.len   = cookie_size;
  buffval.pdata = (void *)hash_entry;
  buffval.len   = sizeof(*hash_entry);

  if(isFullDebug(COMPONENT_STATE))
    (void) display_lock_cookie_entry(&dspbuf, hash_entry);


  if(HashTable_Test_And_Set
     (ht_lock_cookies, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    {
      gsh_free(hash_entry);
      LogFullDebug(COMPONENT_STATE,
                   "Lock Cookie {%s} HASH TABLE ERROR",
                   str);
      *pstatus = STATE_HASH_TABLE_ERROR;
      return *pstatus;
    }

  /* Increment lock entry reference count and link it to the cookie */
  lock_entry_inc_ref(lock_entry);
  lock_entry->sle_block_data->sbd_blocked_cookie = hash_entry;

  LogFullDebug(COMPONENT_STATE,
               "Lock Cookie {%s} Added",
               str);

  switch(lock_entry->sle_block_data->sbd_grant_type)
    {
      case STATE_GRANT_NONE:
        /* Shouldn't get here */
        *pstatus = STATE_INCONSISTENT_ENTRY;
        break;

      case STATE_GRANT_FSAL_AVAILABLE:
        /* Now that we are sure we can continue, try to acquire the FSAL lock */
        /* If we get STATE_LOCK_BLOCKED we need to return... */
        *pstatus = do_lock_op(pentry,
                              pcontext,
                              lock_entry->sle_pexport,
                              FSAL_OP_LOCKB,
                              0, /* Not a reclaim */
                              lock_entry->sle_owner,
                              &lock_entry->sle_lock,
                              NULL,
                              NULL,
                              FALSE);
        break;

      case STATE_GRANT_INTERNAL:
        /* Now that we are sure we can continue, acquire the FSAL lock */
        /* If we get STATE_LOCK_BLOCKED we need to return... */
        *pstatus = do_lock_op(pentry,
                              pcontext,
                              lock_entry->sle_pexport,
                              FSAL_OP_LOCK,
                              0, /* Not a reclaim */
                              lock_entry->sle_owner,
                              &lock_entry->sle_lock,
                              NULL,
                              NULL,
                              FALSE);
        break;

      case STATE_GRANT_FSAL:
        /* No need to go to FSAL for lock */
        *pstatus = STATE_SUCCESS;
        break;
    }

  if(*pstatus != STATE_SUCCESS)
    {
      /* lock will be returned to right blocking type if it is still blocking
       * we could lose a block if we failed for any other reason
       */
      if(*pstatus == STATE_LOCK_BLOCKED)
        LogDebug(COMPONENT_STATE,
                 "Unable to lock FSAL for %s lock, error=%s",
                 str_blocked(lock_entry->sle_blocked),
                 state_err_str(*pstatus));
      else
        LogMajor(COMPONENT_STATE,
                 "Unable to lock FSAL for %s lock, error=%s",
                 str_blocked(lock_entry->sle_blocked),
                 state_err_str(*pstatus));

      LogEntry("Entry", lock_entry);

      /* And release the cookie without unblocking the lock.
       * grant_blocked_locks() will decide whether to keep or free the block.
       */
      free_cookie(hash_entry, FALSE);

      return *pstatus;
    }

  *ppcookie_entry = hash_entry;
  return *pstatus;
}

state_status_t state_cancel_grant(fsal_op_context_t    * pcontext,
                                  state_cookie_entry_t * cookie_entry,
                                  state_status_t       * pstatus)
{
  /* We had acquired an FSAL lock, need to release it. */
  *pstatus = do_lock_op(cookie_entry->sce_pentry,
                        pcontext,
                        cookie_entry->sce_lock_entry->sle_pexport,
                        FSAL_OP_UNLOCK,
                        0, /* Not a reclaim */
                        cookie_entry->sce_lock_entry->sle_owner,
                        &cookie_entry->sce_lock_entry->sle_lock,
                        NULL,   /* no conflict expected */
                        NULL,
                        FALSE);

  if(*pstatus != STATE_SUCCESS)
    LogMajor(COMPONENT_STATE,
             "Unable to unlock FSAL for canceled GRANTED lock, error=%s",
             state_err_str(*pstatus));

  /* And release the cookie and unblock lock (because lock will be removed) */
  free_cookie(cookie_entry, TRUE);

  return *pstatus;
}

state_status_t state_find_grant(void                  * pcookie,
                                int                     cookie_size,
                                state_cookie_entry_t ** ppcookie_entry,
                                state_status_t        * pstatus)
{
  hash_buffer_t         buffkey;
  hash_buffer_t         buffval;
  hash_buffer_t         buffused_key;
  char                  str[LOG_BUFF_LEN];
  struct display_buffer dspbuf = {sizeof(str), str, str};

  buffkey.pdata = (caddr_t) pcookie;
  buffkey.len   = cookie_size;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      (void) display_lock_cookie_key(&dspbuf, &buffkey);

      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s}", str);
    }

  if(HashTable_Get_and_Del(ht_lock_cookies, &buffkey, &buffval, &buffused_key) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_STATE,
                   "KEY {%s} NOTFOUND", str);
      *pstatus = STATE_BAD_COOKIE;
      return *pstatus;
    }

  *ppcookie_entry = (state_cookie_entry_t *) buffval.pdata;

  if(isFullDebug(COMPONENT_STATE) && isDebug(COMPONENT_HASHTABLE))
    {
      display_reset_buffer(&dspbuf);
      (void) display_lock_cookie_entry(&dspbuf, *ppcookie_entry);

      LogFullDebug(COMPONENT_STATE,
                   "Found Lock Cookie {%s}", str);
    }

  *pstatus = STATE_SUCCESS;
  return *pstatus;
}

void grant_blocked_lock_immediate(cache_entry_t         * pentry,
                                  fsal_op_context_t     * pcontext,
                                  state_lock_entry_t    * lock_entry)
{
  state_cookie_entry_t * pcookie = NULL;
  state_status_t         state_status;

  /* Try to clean up blocked lock. */
  if(lock_entry->sle_block_data != NULL)
    {
      if(lock_entry->sle_block_data->sbd_blocked_cookie != NULL)
        {
          /* Cookie is attached, try to get it */
          pcookie = lock_entry->sle_block_data->sbd_blocked_cookie;

          if(state_find_grant(pcookie->sce_pcookie,
                              pcookie->sce_cookie_size,
                              &pcookie,
                              &state_status) == STATE_SUCCESS)
            {
              /* We've got the cookie, free the cookie and the blocked lock */
              free_cookie(pcookie, TRUE);
            }
          else
            {
              /* Otherwise, another thread has the cookie, let it do it's business. */
              return;
            }
        }
      else
        {
          /* We have block data but no cookie, so we can just free the block data */
          free_block_data(lock_entry->sle_block_data);
          lock_entry->sle_block_data = NULL;
        }
    }

  /* Mark lock as granted */
  lock_entry->sle_blocked = STATE_NON_BLOCKING;

  /* Merge any touching or overlapping locks into this one. */
  LogEntry("Granted immediate, merging locks for", lock_entry);

  merge_lock_entry(pentry, pcontext, lock_entry);
  LogEntry("Immediate Granted entry", lock_entry);

  /* A lock downgrade could unblock blocked locks */
  grant_blocked_locks(pentry, pcontext);
}

void state_complete_grant(fsal_op_context_t     * pcontext,
                          state_cookie_entry_t  * cookie_entry)
{
  state_lock_entry_t   * lock_entry;
  cache_entry_t        * pentry;

  lock_entry = cookie_entry->sce_lock_entry;
  pentry     = cookie_entry->sce_pentry;

  /* This routine does not call cache_inode_inc_pin_ref() because there MUST be
   * at least one lock present for there to be a cookie_entry to even allow this
   * routine to be called, and therefor the cache entry MUST be pinned.
   */

  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

  /* We need to make sure lock is ready to be granted */
  if(lock_entry->sle_blocked == STATE_GRANTING)
    {
      /* Mark lock as granted */
      lock_entry->sle_blocked = STATE_NON_BLOCKING;

      /* Merge any touching or overlapping locks into this one. */
      LogEntry("Granted, merging locks for", lock_entry);
      merge_lock_entry(pentry, pcontext, lock_entry);

      LogEntry("Granted entry", lock_entry);

      /* A lock downgrade could unblock blocked locks */
      grant_blocked_locks(pentry, pcontext);
    }

  /* Free cookie and unblock lock.
   * If somehow the lock was unlocked/canceled while the GRANT
   * was in progress, this will completely clean up the lock.
   */
  free_cookie(cookie_entry, TRUE);

  /* In case all locks have wound up free, we must release the pin reference. */
  if(glist_empty(&pentry->object.file.lock_list))
      cache_inode_dec_pin_ref(pentry, FALSE);

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);
}

void try_to_grant_lock(state_lock_entry_t   * lock_entry)
{
  granted_callback_t     call_back;
  state_blocking_t       blocked;
  state_status_t         status;

  /* Try to grant if not cancelled and has block data */
  if(lock_entry->sle_blocked != STATE_CANCELED &&
     lock_entry->sle_block_data != NULL)
    {
      call_back = lock_entry->sle_block_data->sbd_granted_callback;
      /*
       * Mark the lock_entry as provisionally granted and make the granted
       * call back. The granted call back is responsible for acquiring a
       * reference to the lock entry if needed.
       */
      blocked = lock_entry->sle_blocked;
      lock_entry->sle_blocked = STATE_GRANTING;
      if(lock_entry->sle_block_data->sbd_grant_type == STATE_GRANT_NONE)
        lock_entry->sle_block_data->sbd_grant_type = STATE_GRANT_INTERNAL;

      if(call_back(lock_entry->sle_pentry,
                   lock_entry,
                   &status) == STATE_LOCK_BLOCKED)
        {
          /* The lock is still blocked, restore it's type and leave it in the list */
          lock_entry->sle_blocked = blocked;
          return;
        }

      if(status == STATE_SUCCESS)
        return;
    }

  /* There was no call back data, the call back failed, or the block was cancelled.
   * Remove lock from list.
   */
  LogEntry("Removing blocked entry", lock_entry);
  remove_from_locklist(lock_entry);
}

void process_blocked_lock_upcall(state_block_data_t   * block_data)
{
  state_lock_entry_t * lock_entry = block_data->sbd_lock_entry;
  cache_entry_t      * pentry = lock_entry->sle_pentry;

  /* This routine does not call cache_inode_inc_pin_ref() because there MUST be
   * at least one lock present for there to be a block_data to even allow this
   * routine to be called, and therefor the cache entry MUST be pinned.
   */

  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

  try_to_grant_lock(lock_entry);

  /* In case all locks have wound up free, we must release the pin reference. */
  if(glist_empty(&pentry->object.file.lock_list))
      cache_inode_dec_pin_ref(pentry, FALSE);

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);
}

static void grant_blocked_locks(cache_entry_t        * pentry,
                                fsal_op_context_t    * pcontext)
{
  state_lock_entry_t   * found_entry;
  struct glist_head    * glist, * glistn;
  fsal_staticfsinfo_t  * pstatic = pcontext->export_context->fe_static_fs_info;

  /* If FSAL supports async blocking locks, allow it to grant blocked locks. */
  if(pstatic->lock_support_async_block)
    return;

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      if(found_entry->sle_blocked != STATE_NLM_BLOCKING &&
         found_entry->sle_blocked != STATE_NFSV4_BLOCKING)
          continue;

      /* Found a blocked entry for this file, see if we can place the lock. */
      if(get_overlapping_entry(pentry,
                               pcontext,
                               found_entry->sle_owner,
                               &found_entry->sle_lock) != NULL)
        continue;

      /* Found an entry that might work, try to grant it. */
      try_to_grant_lock(found_entry);
    }
}

state_status_t cancel_blocked_lock(cache_entry_t        * pentry,
                                   fsal_op_context_t    * pcontext,
                                   state_lock_entry_t   * lock_entry)
{
  state_cookie_entry_t * pcookie = NULL;
  state_status_t         state_status;

  /* Mark lock as canceled */
  LogEntry("Cancelling blocked", lock_entry);
  lock_entry->sle_blocked = STATE_CANCELED;

      /* Unlocking the entire region will remove any FSAL locks we held, whether
       * from fully granted locks, or from blocking locks that were in the process
       * of being granted.
       */

  /* Try to clean up blocked lock if a cookie is present */
  if(lock_entry->sle_block_data != NULL &&
     lock_entry->sle_block_data->sbd_blocked_cookie != NULL)
    {
      /* Cookie is attached, try to get it */
      pcookie = lock_entry->sle_block_data->sbd_blocked_cookie;

      if(state_find_grant(pcookie->sce_pcookie,
                          pcookie->sce_cookie_size,
                          &pcookie,
                          &state_status) == STATE_SUCCESS)
        {
          /* We've got the cookie, free the cookie and the blocked lock */
          free_cookie(pcookie, TRUE);
        }
      /* otherwise, another thread has the cookie, let it do it's business,
       * which won't be much, since we've already marked the lock CANCELED.
       */
    }
  else
    {
      /* Otherwise, if block data is present, it will be freed when the lock
       * entry is freed. If the cookie is held, the refcount it holds will
       * prevent the lock entry from being released until the cookie is
       * freed.
       */

      /* Since a cookie was not found, the lock must still be in a state
       * of needing cancelling.
       */
      state_status = do_lock_op(pentry,
                                pcontext,
                                lock_entry->sle_pexport,
                                FSAL_OP_CANCEL,
                                0, /*Not a reclaim */
                                lock_entry->sle_owner,
                                &lock_entry->sle_lock,
                                NULL,   /* no conflict expected */
                                NULL,
                                FALSE); /* overlap not relevant */

      if(state_status != STATE_SUCCESS)
        {
          /* Unable to cancel, assume that granted upcall is on it's way. */
          LogEntry("Unable to cancel (grant upcall expected)", lock_entry);
          return STATE_SUCCESS;
        }
    }

  /* Remove the lock from the lock list*/
  LogEntry("Removing", lock_entry);
  remove_from_locklist(lock_entry);

  return state_status;
}

/**
 *
 * cancel_blocked_locks_range: Cancel blocked locks that overlap this lock.
 *
 * Handle the situation where we have granted a lock and the client now
 * assumes it holds the lock, but we haven't received the GRANTED RSP, and
 * now the client is unlocking the lock.
 *
 * This will also handle the case of a client that uses UNLOCK to cancel
 * a blocked lock.
 *
 * Because this will release any blocked lock that was in the process of
 * being granted that overlaps the lock at all, we protect ourselves from
 * having a stuck lock at the risk of the client thinking it has a lock
 * it now doesn't.
 *
 * If the client unlock doesn't happen to fully overlap a blocked lock,
 * the blocked lock will be cancelled in full. Hopefully the client will
 * retry the remainder lock that should have still been blocking.
 */
void cancel_blocked_locks_range(cache_entry_t        * pentry,
                                fsal_op_context_t    * pcontext,
                                state_owner_t        * powner,
                                state_t              * pstate,
                                fsal_lock_param_t    * plock)
{
  struct glist_head  * glist, * glistn;
  state_lock_entry_t * found_entry = NULL;
  uint64_t             found_entry_end, plock_end = lock_end(plock);

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      /* Skip locks not owned by owner */
      if(powner != NULL && different_owners(found_entry->sle_owner, powner))
        continue;

      /* Skip locks owned by this NLM state.
       * This protects NLM locks from the current iteration of an NLM
       * client from being released by SM_NOTIFY.
       */
      if(pstate != NULL &&
         lock_owner_is_nlm(found_entry) &&
         found_entry->sle_state == pstate)
        continue;

      /* Skip granted locks */
      if(found_entry->sle_blocked == STATE_NON_BLOCKING)
        continue;

      LogEntry("Checking", found_entry);

      found_entry_end = lock_end(&found_entry->sle_lock);

      if((found_entry_end >= plock->lock_start) &&
         (found_entry->sle_lock.lock_start <= plock_end))
        {
          /* lock overlaps, cancel it. */
          (void) cancel_blocked_lock(pentry, pcontext, found_entry);
        }
    }
}

state_status_t state_release_grant(fsal_op_context_t     * pcontext,
                                   state_cookie_entry_t  * cookie_entry,
                                   state_status_t        * pstatus)
{
  state_lock_entry_t   * lock_entry;
  cache_entry_t        * pentry;

  *pstatus = STATE_SUCCESS;

  lock_entry = cookie_entry->sce_lock_entry;
  pentry     = cookie_entry->sce_pentry;

  /* This routine does not call cache_inode_inc_pin_ref() because there MUST be
   * at least one lock present for there to be a cookie_entry to even allow this
   * routine to be called, and therefor the cache entry MUST be pinned.
   */

  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

  /* We need to make sure lock is only "granted" once...
   * It's (remotely) possible that due to latency, we might end up processing
   * two GRANTED_RSP calls at the same time.
   */
  if(lock_entry->sle_blocked == STATE_GRANTING)
    {
      /* Mark lock as canceled */
      lock_entry->sle_blocked = STATE_CANCELED;

      /* We had acquired an FSAL lock, need to release it. */
      *pstatus = do_lock_op(pentry,
                            pcontext,
                            lock_entry->sle_pexport,
                            FSAL_OP_UNLOCK,
                            0, /*Not a reclaim */
                            lock_entry->sle_owner,
                            &lock_entry->sle_lock,
                            NULL,   /* no conflict expected */
                            NULL,
                            FALSE);

      if(*pstatus != STATE_SUCCESS)
        LogMajor(COMPONENT_STATE,
                 "Unable to unlock FSAL for released GRANTED lock, error=%s",
                 state_err_str(*pstatus));
      else {
        /*
         * Remove the lock from the lock list.
         * Will not free yet because of cookie reference to lock entry.
         */
        LogEntry("Release Grant Removing", lock_entry);
        remove_from_locklist(lock_entry);
      }
    }

  /* Free the cookie and unblock the lock.
   * This will release our final reference on the lock entry and should free it.
   * (Unless another thread has a reference for some reason.
   */
  free_cookie(cookie_entry, TRUE);

  /* Check to see if we can grant any blocked locks. */
  grant_blocked_locks(pentry, pcontext);

  /* In case all locks have wound up free, we must release the pin reference. */
  if(glist_empty(&pentry->object.file.lock_list))
      cache_inode_dec_pin_ref(pentry, FALSE);

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

  return *pstatus;
}
#endif

/******************************************************************************
 *
 * Functions to interract with FSAL
 *
 ******************************************************************************/
inline const char *fsal_lock_op_str(fsal_lock_op_t op)
{
  switch(op)
    {
      case FSAL_OP_LOCKT:  return "FSAL_OP_LOCKT ";
      case FSAL_OP_LOCK:   return "FSAL_OP_LOCK  ";
      case FSAL_OP_LOCKB:  return "FSAL_OP_LOCKB ";
      case FSAL_OP_UNLOCK: return "FSAL_OP_UNLOCK";
      case FSAL_OP_CANCEL: return "FSAL_OP_CANCEL";
    }
  return "unknown";
}

/**
 *
 * FSAL_unlock_no_owner: Handle FSAL unlock when owner is not supported.
 *
 * When the FSAL doesn't support lock owners, we can't just arbitrarily
 * unlock the entire range in the FSAL, we might have locks owned by
 * other owners that still exist, either because there were several
 * lock owners with read locks, or the client unlocked a larger range
 * that is actually locked (some (most) clients will actually unlock the
 * entire file when closing a file or terminating a process).
 *
 * Basically, we want to create a list of ranges to unlock. To do so
 * we create a dummy entry in a dummy list for the unlock range. Then
 * we subtract each existing lock from the dummy list.
 *
 * The list of unlock ranges will include ranges that the original onwer
 * didn't actually have locks in. This behavior is actually helpful
 * for some callers of FSAL_OP_UNLOCK.
 */
state_status_t do_unlock_no_owner(cache_entry_t        * pentry,
                                  fsal_op_context_t    * pcontext,
                                  exportlist_t         * pexport,
                                  fsal_lock_param_t    * plock)
{
  state_lock_entry_t * unlock_entry;
  struct glist_head    fsal_unlock_list;
  struct glist_head  * glist, *glistn;
  state_lock_entry_t * found_entry;
  fsal_status_t        fsal_status;
  state_status_t       status = STATE_SUCCESS, t_status;
  fsal_lock_param_t  * punlock;

  unlock_entry = create_state_lock_entry(pentry,
                                         pcontext,
                                         pexport,
                                         STATE_NON_BLOCKING,
                                         &unknown_owner, /* no real owner */
                                         NULL, /* no real state */
                                         plock);

  if(unlock_entry == NULL)
    return STATE_MALLOC_ERROR;

  init_glist(&fsal_unlock_list);

  glist_add_tail(&fsal_unlock_list, &unlock_entry->sle_list);

  LogEntry("Generating FSAL Unlock List", unlock_entry);

  if(subtract_list_from_list(pentry,
                             pcontext,
                             &fsal_unlock_list,
                             &pentry->object.file.lock_list,
                             &status) != STATE_SUCCESS)
    {
      /* We ran out of memory while trying to build the unlock list.
       * We have already released the locks from cache inode lock list.
       */
      // TODO FSF: what do we do now?
      LogMajor(COMPONENT_STATE,
               "Error %s while trying to create unlock list",
               state_err_str(status));
    }

  glist_for_each_safe(glist, glistn, &fsal_unlock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);
      punlock     = &found_entry->sle_lock;

      LogEntry("FSAL Unlock", found_entry);

      fsal_status = FSAL_lock_op(cache_inode_fd(pentry),
                                 &pentry->handle,
                                 pcontext,
                                 NULL,
                                 FSAL_OP_UNLOCK,
                                 *punlock,
                                 NULL);

      t_status = state_error_convert(fsal_status);

      LogFullDebug(COMPONENT_STATE,
                   "FSAL_lock_op returned %s",
                   state_err_str(t_status));

      if(t_status != STATE_SUCCESS)
        {
          // TODO FSF: what do we do now?
          LogMajor(COMPONENT_STATE,
                   "Error %s while trying to do FSAL Unlock",
                   state_err_str(status));
          status = t_status;
        }

      remove_from_locklist(found_entry);
    }

  return status;
}

state_status_t do_lock_op(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          exportlist_t         * pexport,
                          fsal_lock_op_t         lock_op,
                          bool_t                 is_reclaim,
                          state_owner_t        * powner,
                          fsal_lock_param_t    * plock,
                          state_owner_t       ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t    * conflict, /* description of conflicting lock */
                          bool_t                 overlap)  /* hint that lock overlaps */
{
  fsal_status_t         fsal_status;
  state_status_t        status = STATE_SUCCESS;
  fsal_lock_param_t     conflicting_lock;
  fsal_staticfsinfo_t * pstatic = pcontext->export_context->fe_static_fs_info;

  /* Quick exit if:
   * Locks are not supported by FSAL
   * Async blocking locks are not supported and this is a cancel
   * Async blocking locks are not supported and this lock overlaps
   * Lock owners are not supported and hint tells us that lock fully overlaps a
   *   lock we already have (no need to make another FSAL call in that case)
   */
  if(!pstatic->lock_support ||
     (!pstatic->lock_support_async_block && lock_op == FSAL_OP_CANCEL) ||
     (!pstatic->lock_support_async_block && overlap) ||
     (!pstatic->lock_support_owner && overlap))
    return STATE_SUCCESS;

  LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
          fsal_lock_op_str(lock_op), pentry, pcontext, powner, plock);

  memset(&conflicting_lock, 0, sizeof(conflicting_lock));

  if(pstatic->lock_support_owner || lock_op != FSAL_OP_UNLOCK)
    {
      if(lock_op == FSAL_OP_LOCKB && !pstatic->lock_support_async_block)
        lock_op = FSAL_OP_LOCK;

      /* In the case of ip move the source node may be still releasing
       * the state that we need to acquire. So let us be little patient
       * to STATE_LOCK_CONFLICT errors if the lock is a reclaim and we are
       * in grace.
       */
      do
        {
          if (status == STATE_LOCK_CONFLICT)
            sleep(1); /* We looped back so let us not bombard the filesystem. */

          fsal_status = FSAL_lock_op(cache_inode_fd(pentry),
                                     &pentry->handle,
                                     pcontext,
                                     pstatic->lock_support_owner ? powner : NULL,
                                     lock_op,
                                     *plock,
                                     &conflicting_lock);

          status = state_error_convert(fsal_status);
        }
      while(is_reclaim && status == STATE_LOCK_CONFLICT && nfs_in_grace());

      LogFullDebug(COMPONENT_STATE,
                   "FSAL_lock_op returned %s",
                   state_err_str(status));

      if(status == STATE_LOCK_BLOCKED && lock_op != FSAL_OP_LOCKB)
        {
          /* This is an unexpected return code, make sure caller reports an error */
          LogMajor(COMPONENT_STATE,
                   "FSAL returned unexpected STATE_LOCK_BLOCKED result");
          status = STATE_FSAL_ERROR;
        }
    }
  else
    {
      status = do_unlock_no_owner(pentry, pcontext, pexport, plock);
    }

  if(status == STATE_LOCK_CONFLICT)
    {
      if(holder != NULL)
        {
          *holder = &unknown_owner;
          inc_state_owner_ref(&unknown_owner);
        }
      if(conflict != NULL)
        {
          *conflict = conflicting_lock;
        }
    }

  return status;
}

void copy_conflict(state_lock_entry_t  * found_entry,
                   state_owner_t      ** holder,   /* owner that holds conflicting lock */
                   fsal_lock_param_t   * conflict) /* description of conflicting lock */
{
  if(found_entry == NULL)
    return;

  if(holder != NULL)
    {
      *holder = found_entry->sle_owner;
      inc_state_owner_ref(found_entry->sle_owner);
    }
  if(conflict != NULL)
    *conflict = found_entry->sle_lock;
}

/******************************************************************************
 *
 * Primary lock interface functions
 *
 ******************************************************************************/

/**
 *
 * state_test: Test for lock availability
 *
 * This function acquires the state lock on an entry and thus is only
 * suitable for operations like lockt.  If one wishes to use it as
 * part of a larger lock or state operation one would need to split it
 * out.
 */
state_status_t state_test(cache_entry_t        * pentry,
                          fsal_op_context_t    * pcontext,
                          exportlist_t         * pexport,
                          state_owner_t        * powner,
                          fsal_lock_param_t    * plock,
                          state_owner_t       ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t    * conflict, /* description of conflicting lock */
                          state_status_t       * pstatus)
{
  state_lock_entry_t   * found_entry;
  cache_inode_status_t   cache_status;

  LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
          "TEST",
          pentry, pcontext, powner, plock);

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  if(cache_inode_open(pentry, FSAL_O_RDWR, pcontext, 0, &cache_status) != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogFullDebug(COMPONENT_STATE,
                   "Could not open file");

      cache_inode_dec_pin_ref(pentry, FALSE);

      return *pstatus;
    }

  PTHREAD_RWLOCK_RDLOCK(&pentry->state_lock);

  found_entry = get_overlapping_entry(pentry, pcontext, powner, plock);

  if(found_entry != NULL)
    {
      /* found a conflicting lock, return it */
      LogEntry("Found conflict", found_entry);
      copy_conflict(found_entry, holder, conflict);
      *pstatus = STATE_LOCK_CONFLICT;
    }
  else
    {
      /* Prepare to make call to FSAL for this lock */
      *pstatus = do_lock_op(pentry,
                            pcontext,
                            pexport,
                            FSAL_OP_LOCKT,
                            0, /*Not a reclaim */
                            powner,
                            plock,
                            holder,
                            conflict,
                            FALSE);

      if(*pstatus != STATE_SUCCESS &&
         *pstatus != STATE_LOCK_CONFLICT)
        {
          LogMajor(COMPONENT_STATE,
                   "Got error from FSAL lock operation, error=%s",
                   state_err_str(*pstatus));
        }
      if(*pstatus == STATE_SUCCESS)
        LogFullDebug(COMPONENT_STATE,
                     "No Conflict");
      else
        LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
                "Conflict from FSAL",
                pentry, pcontext, *holder, conflict);
    }

  if(isFullDebug(COMPONENT_STATE) && isFullDebug(COMPONENT_MEMLEAKS))
    LogList("Lock List", pentry, &pentry->object.file.lock_list);

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, FALSE);

  return *pstatus;
}

/**
 *
 * state_lock: Attempt to acquire a lock
 *
 * This function acquires the state lock on a file.
 *
 */
state_status_t state_lock(cache_entry_t         * pentry,
                          fsal_op_context_t     * pcontext,
                          exportlist_t          * pexport,
                          state_owner_t         * powner,
                          state_t               * pstate,
                          state_blocking_t        blocking,
                          state_block_data_t    * block_data,
                          fsal_lock_param_t     * plock,
                          bool_t                  is_reclaim,
                          state_owner_t        ** holder,   /* owner that holds conflicting lock */
                          fsal_lock_param_t     * conflict, /* description of conflicting lock */
                          state_status_t        * pstatus)
{
  bool_t                 allow = TRUE, overlap = FALSE;
  struct glist_head    * glist;
  state_lock_entry_t   * found_entry;
  uint64_t               found_entry_end;
  uint64_t               plock_end = lock_end(plock);
  cache_inode_status_t   cache_status;
  fsal_staticfsinfo_t  * pstatic = pcontext->export_context->fe_static_fs_info;
  fsal_lock_op_t         lock_op;

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  if(cache_inode_open(pentry, FSAL_O_RDWR, pcontext, 0, &cache_status) != CACHE_INODE_SUCCESS)
    {
      cache_inode_dec_pin_ref(pentry, FALSE);
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogFullDebug(COMPONENT_STATE,
                   "Could not open file");
      return *pstatus;
    }

  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

#ifdef _USE_BLOCKING_LOCKS

  if(blocking != STATE_NON_BLOCKING)
    {
      /*
       * First search for a blocked request. Client can ignore the blocked
       * request and keep sending us new lock request again and again. So if
       * we have a mapping blocked request return that
       */
      glist_for_each(glist, &pentry->object.file.lock_list)
        {
          found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

          if(different_owners(found_entry->sle_owner, powner))
            continue;

          /* Need to reject lock request if this lock owner already has a lock
           * on this file via a different export.
           */
          if(found_entry->sle_pexport != pexport)
            {
              PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

              cache_inode_dec_pin_ref(pentry, FALSE);

              LogEvent(COMPONENT_STATE,
                       "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
                       found_entry->sle_pexport->id,
                       found_entry->sle_pexport->fullpath,
                       pexport->id,
                       pexport->fullpath);
              LogEntry("Found lock entry belonging to another export", found_entry);
              *pstatus = STATE_INVALID_ARGUMENT;
              return *pstatus;
            }

          if(found_entry->sle_blocked != blocking)
            continue;

          if(different_lock(&found_entry->sle_lock, plock))
            continue;

          PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

          cache_inode_dec_pin_ref(pentry, FALSE);

          /*
           * We have matched all atribute of the existing lock.
           * Just return with blocked status. Client may be polling.
           */
          LogEntry("Found blocked", found_entry);
          *pstatus = STATE_LOCK_BLOCKED;
          return *pstatus;
        }
    }
#endif

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      /* Need to reject lock request if this lock owner already has a lock
       * on this file via a different export.
       */
      if(found_entry->sle_pexport != pexport &&
         !different_owners(found_entry->sle_owner, powner))
        {
          PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

          cache_inode_dec_pin_ref(pentry, FALSE);

          LogEvent(COMPONENT_STATE,
                   "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
                   found_entry->sle_pexport->id,
                   found_entry->sle_pexport->fullpath,
                   pexport->id,
                   pexport->fullpath);

          LogEntry("Found lock entry belonging to another export", found_entry);

          *pstatus = STATE_INVALID_ARGUMENT;
          return *pstatus;
        }

      /* Don't skip blocked locks for fairness */

      found_entry_end = lock_end(&found_entry->sle_lock);

      if (!is_reclaim) {
        if((found_entry_end >= plock->lock_start) &&
           (found_entry->sle_lock.lock_start <= plock_end))
          {
            /* lock overlaps see if we can allow
             * allow if neither lock is exclusive or the owner is the same
             */
            if((found_entry->sle_lock.lock_type == FSAL_LOCK_W ||
                plock->lock_type == FSAL_LOCK_W) &&
               different_owners(found_entry->sle_owner, powner))
              {
                /* Found a conflicting lock, break out of loop.
                 * Also indicate overlap hint.
                 */
                LogEntry("Conflicts with", found_entry);
                LogList("Locks", pentry, &pentry->object.file.lock_list);
                copy_conflict(found_entry, holder, conflict);
                allow   = FALSE;
                overlap = TRUE;
              break;
              }
          }
      }

      if(found_entry_end >= plock_end &&
         found_entry->sle_lock.lock_start <= plock->lock_start &&
         found_entry->sle_lock.lock_type == plock->lock_type &&
         (found_entry->sle_blocked == STATE_NON_BLOCKING ||
          found_entry->sle_blocked == STATE_GRANTING))
        {
          /* Found an entry that entirely overlaps the new entry
           * (and due to the preceding test does not prevent
           * granting this lock - therefore there can't be any
           * other locks that would prevent granting this lock
           */
          if(!different_owners(found_entry->sle_owner, powner))
            {
 #ifdef _USE_BLOCKING_LOCKS
             /* The lock actually has the same owner, we're done,
              * other than dealing with a lock in GRANTING state.
              */
              if(found_entry->sle_blocked == STATE_GRANTING)
                {
                  /* Need to handle completion of granting of this lock
                   * because a GRANT was in progress.
                   * This could be a client retrying a blocked lock
                   * due to mis-trust of server. If the client
                   * also accepts the GRANT_MSG with a GRANT_RESP,
                   * that will be just fine.
                   */
                  grant_blocked_lock_immediate(pentry,
                                               pcontext,
                                               found_entry);
                }
#endif
              PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

              cache_inode_dec_pin_ref(pentry, FALSE);

              LogEntry("Found existing", found_entry);

              *pstatus = STATE_SUCCESS;
              return *pstatus;
            }

          /* Found a compatible lock with a different lock owner that
           * fully overlaps, set hint.
           */
          LogEntry("state_lock Found overlapping", found_entry);
          overlap = TRUE;
        }
    }

  /* Decide how to proceed */
  if(pstatic->lock_support_async_block && blocking == STATE_NLM_BLOCKING)
    {
      /* FSAL supports blocking locks, and this is an NLM blocking lock request,
       * request blocking lock from FSAL.
       */
      lock_op = FSAL_OP_LOCKB;
    }
  else if(allow || blocking == STATE_NLM_BLOCKING)
    {
      /* No conflict found in Ganesha, or NLM blocking lock when FSAL doesn't
       * support blocking locks. In either case, proceed with non-blocking
       * request to FSAL.
       */
      lock_op = FSAL_OP_LOCK;
    }
  else
    {
      /* Can't do async blocking lock in FSAL and have a conflict.
       * Return it.
       */
      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);

      *pstatus = STATE_LOCK_CONFLICT;
      return *pstatus;
    }

  /* We have already returned if:
   * + we have found an identical blocking lock
   * + we have found an entirely overlapping lock with the same lock owner
   * + this was not a supported blocking lock and we found a conflict
   *
   * So at this point, we are either going to do one of the following (all
   * descriptions below assume no problems occur):
   *
   * (1) FSAL supports async blocking locks, we know there is a conflict, and
   *     this is a supported blocking lock request
   *
   *     Make FSAL_OP_LOCKB call anyway, we will rely on FSAL to grant blocking
   *     locks. We will return the conflict we know about rather than what the
   *     FSAL returns. Insert blocking lock into queue.
   *
   * (2) FSAL supports async blocking locks, we don't know about any conflict,
   *     and this is a supported blocking lock request
   *
   *     Make FSAL_OP_LOCKB call, if it indicates block, insert blocking lock
   *     into queue, and return the conflict the FSAL indicates. If FSAL grants
   *     lock, then return granted lock and insert into lock list, otherwise
   *     insert blocking lock into queue.
   *
   * (3) FSAL doesn't support async blocking locks, this is a supported blocking
   *     lock and we know there is a conflict
   *
   *     Insert blocking lock into queue, we will grant lock when possible.
   *
   * (4) FSAL doesn't support async blocking locks and we don't know about any
   *     conflict
   *
   *     Make FSAL_OP_LOCK call, if it indicates conflict, return that. Even if
   *     this is a supported blocking lock call, there is no way to block. If
   *     lock is granted, return that and insert lock into list.
   *
   * (5) FSAL supports async blocking locks, we don't know about any conflict,
   *     and this is not a supported blocking lock request
   *
   *     Make FSAL_OP_LOCK call, if it indicates conflict, return that. If
   *     lock is granted, return that and insert lock into list.
   */

  /* Create the new lock entry.
   * Provisionally mark this lock as granted.
   */
  found_entry = create_state_lock_entry(pentry,
                                        pcontext,
                                        pexport,
                                        STATE_NON_BLOCKING,
                                        powner,
                                        pstate,
                                        plock);
  if(!found_entry)
    {
      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);

      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
    }

  /* If no conflict in lock list, or FSAL supports async blocking locks,
   * make FSAL call. Don't ask for conflict if we know about a conflict.
   */
  if(allow || pstatic->lock_support_async_block)
    {
      /* Prepare to make call to FSAL for this lock */
      *pstatus = do_lock_op(pentry,
                            pcontext,
                            pexport,
                            lock_op,
                            is_reclaim,
                            powner,
                            plock,
                            allow ? holder : NULL,
                            allow ? conflict : NULL,
                            overlap);
    }
  else
    *pstatus = STATE_LOCK_BLOCKED;

  if(*pstatus == STATE_SUCCESS)
    {
      /* Merge any touching or overlapping locks into this one */
      LogEntry("FSAL lock acquired, merging locks for", found_entry);

      merge_lock_entry(pentry, pcontext, found_entry);

      /* Insert entry into lock list */
      LogEntry("New", found_entry);

      /* if the list is empty to start with; increment the pin ref count
       * before adding it to the list
       */
      if(glist_empty(&pentry->object.file.lock_list))
          cache_inode_inc_pin_ref(pentry);

      glist_add_tail(&pentry->object.file.lock_list, &found_entry->sle_list);

#ifdef _USE_BLOCKING_LOCKS
      /* A lock downgrade could unblock blocked locks */
      grant_blocked_locks(pentry, pcontext);
#endif
      /* Don't need to unpin, we know there is state on file. */
    }
  else if(*pstatus == STATE_LOCK_CONFLICT)
    {
      LogEntry("Conflict in FSAL for", found_entry);

      /* Discard lock entry */
      remove_from_locklist(found_entry);
    }
#ifdef _USE_BLOCKING_LOCKS
  else if(*pstatus == STATE_LOCK_BLOCKED)
    {
      /* Mark entry as blocking and attach block_data */
      found_entry->sle_block_data = block_data;
      found_entry->sle_blocked    = blocking;
      block_data->sbd_lock_entry  = found_entry;

      /* Insert entry into lock list */
      LogEntry("FSAL block for", found_entry);

      /* if the list is empty to start with; increment the pin ref count
       * before adding it to the list
       */
      if(glist_empty(&pentry->object.file.lock_list))
          cache_inode_inc_pin_ref(pentry);

      glist_add_tail(&pentry->object.file.lock_list, &found_entry->sle_list);

      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);

      P(blocked_locks_mutex);

      glist_add_tail(&state_blocked_locks, &block_data->sbd_list);

      V(blocked_locks_mutex);

      return *pstatus;
    }
#endif /* _USE_BLOCKING_LOCKS */
  else
    {
      LogMajor(COMPONENT_STATE,
               "Unable to lock FSAL, error=%s",
               state_err_str(*pstatus));

      /* Discard lock entry */
      remove_from_locklist(found_entry);
    }

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, FALSE);

  return *pstatus;
}

/**
 *
 * state_unlock: Release a lock
 *
 */
state_status_t state_unlock(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            exportlist_t         * pexport,
                            state_owner_t        * powner,
                            state_t              * pstate,
                            fsal_lock_param_t    * plock,
                            state_status_t       * pstatus)
{
  bool_t                 empty = FALSE;
  cache_inode_status_t   cache_status;

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  if(pentry->type != REGULAR_FILE)
    {
      LogLock(COMPONENT_STATE, NIV_DEBUG,
              "Bad Unlock",
              pentry, pcontext, powner, plock);
      *pstatus = STATE_BAD_TYPE;
      return *pstatus;
    }

  /* We need to iterate over the full lock list and remove
   * any mapping entry. And sle_lock.lock_start = 0 and sle_lock.lock_length = 0 nlm_lock
   * implies remove all entries
   */
  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

  /* If lock list is empty, there really isn't any work for us to do. */
  if(glist_empty(&pentry->object.file.lock_list))
    {
      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);
      LogDebug(COMPONENT_STATE,
               "Unlock success on file with no locks");

      *pstatus = STATE_SUCCESS;
      return *pstatus;
    }

  LogFullDebug(COMPONENT_STATE,
               "----------------------------------------------------------------------");
  LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
          "Subtracting",
          pentry, pcontext, powner, plock);
  LogFullDebug(COMPONENT_STATE,
               "----------------------------------------------------------------------");

#ifdef _USE_BLOCKING_LOCKS
  /* First cancel any blocking locks that might overlap the unlocked range. */
  cancel_blocked_locks_range(pentry,
                             pcontext,
                             powner,
                             pstate,
                             plock);
#endif

  /* Release the lock from cache inode lock list for pentry */
  subtract_lock_from_list(pentry,
                          pcontext,
                          powner,
                          pstate,
                          plock,
                          pstatus,
                          &pentry->object.file.lock_list);

  /* If the lock list has become zero; decrement the pin ref count pt placed.
   * Do this here just in case subtract_lock_from_list has made list empty
   * even if it failed.
   */
  if(glist_empty(&pentry->object.file.lock_list))
      cache_inode_dec_pin_ref(pentry, FALSE);

  if(*pstatus != STATE_SUCCESS)
    {
      /* The unlock has not taken affect (other than canceling any blocking locks. */
      LogMajor(COMPONENT_STATE,
               "Unable to remove lock from list for unlock, error=%s",
               state_err_str(*pstatus));

      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);

      return *pstatus;
    }

  /* Unlocking the entire region will remove any FSAL locks we held, whether
   * from fully granted locks, or from blocking locks that were in the process
   * of being granted.
   */
  *pstatus = do_lock_op(pentry,
                        pcontext,
                        pexport,
                        FSAL_OP_UNLOCK,
                        0, /*Not a reclaim */
                        powner,
                        plock,
                        NULL,   /* no conflict expected */
                        NULL,
                        FALSE);

  if(*pstatus != STATE_SUCCESS)
    LogMajor(COMPONENT_STATE,
             "Unable to unlock FSAL, error=%s",
             state_err_str(*pstatus));

  LogFullDebug(COMPONENT_STATE,
               "----------------------------------------------------------------------");
  LogLock(COMPONENT_STATE, NIV_FULL_DEBUG,
          "Done", pentry, pcontext, powner, plock);
  LogFullDebug(COMPONENT_STATE,
               "----------------------------------------------------------------------");

  if(isFullDebug(COMPONENT_STATE) &&
     isFullDebug(COMPONENT_MEMLEAKS) &&
     plock->lock_start == 0 && plock->lock_length == 0)
    empty = LogList("Lock List", pentry, &pentry->object.file.lock_list);

#ifdef _USE_BLOCKING_LOCKS
  grant_blocked_locks(pentry, pcontext);
#endif

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, FALSE);

  if(isFullDebug(COMPONENT_STATE) &&
     isFullDebug(COMPONENT_MEMLEAKS) &&
     plock->lock_start == 0 && plock->lock_length == 0 &&
     empty)
    dump_all_locks("All locks (after unlock)");

  return *pstatus;
}

#ifdef _USE_BLOCKING_LOCKS

/**
 *
 * state_cancel: Cancel a blocking lock
 *
 */
state_status_t state_cancel(cache_entry_t        * pentry,
                            fsal_op_context_t    * pcontext,
                            exportlist_t         * pexport,
                            state_owner_t        * powner,
                            fsal_lock_param_t    * plock,
                            state_status_t       * pstatus)
{
  struct glist_head    * glist;
  state_lock_entry_t   * found_entry;
  cache_inode_status_t   cache_status;

  if(pentry->type != REGULAR_FILE)
    {
      LogLock(COMPONENT_STATE, NIV_DEBUG,
              "Bad Cancel",
              pentry, pcontext, powner, plock);
      *pstatus = STATE_BAD_TYPE;
      return *pstatus;
    }

  *pstatus = STATE_NOT_FOUND;

  cache_status = cache_inode_inc_pin_ref(pentry);

  if(cache_status != CACHE_INODE_SUCCESS)
    {
      *pstatus = cache_inode_status_to_state_status(cache_status);
      LogDebug(COMPONENT_STATE,
               "Could not pin file");
      return *pstatus;
    }

  PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

  /* If lock list is empty, there really isn't any work for us to do. */
  if(glist_empty(&pentry->object.file.lock_list))
    {
      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      cache_inode_dec_pin_ref(pentry, FALSE);
      LogDebug(COMPONENT_STATE,
               "Cancel success on file with no locks");

      *pstatus = STATE_SUCCESS;
      return *pstatus;
    }

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      if(different_owners(found_entry->sle_owner, powner))
        continue;

      /* Can not cancel a lock once it is granted */
      if(found_entry->sle_blocked == STATE_NON_BLOCKING)
        continue;

      if(different_lock(&found_entry->sle_lock, plock))
        continue;

      /* Cancel the blocked lock */
      *pstatus = cancel_blocked_lock(pentry, pcontext, found_entry);

      /* Check to see if we can grant any blocked locks. */
      grant_blocked_locks(pentry, pcontext);

      break;
    }

  /* If the lock list has become zero; decrement the pin ref count pt placed */
  if(glist_empty(&pentry->object.file.lock_list))
      cache_inode_dec_pin_ref(pentry, FALSE);

  PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

  cache_inode_dec_pin_ref(pentry, FALSE);

  return *pstatus;
}
#endif

#ifdef _USE_NLM

/**
 *
 * state_nlm_notify: Handle an SM_NOTIFY from NLM
 *
 * Also used to handle NLM_FREE_ALL
 *
 */
state_status_t state_nlm_notify(state_nsm_client_t   * pnsmclient,
                                state_t              * pstate,
                                state_status_t       * pstatus,
                                bool_t                 release_all)
{
  state_owner_t      * powner;
  state_lock_entry_t * found_entry;
  exportlist_t       * pexport;
  fsal_lock_param_t    lock;
  cache_entry_t      * pentry;
  int                  errcnt = 0;
  struct glist_head    newlocks;
  fsal_op_context_t    fsal_context;
  fsal_status_t        fsal_status;
  state_nlm_share_t  * found_share;

  if(isFullDebug(COMPONENT_STATE))
    {
      char                  str[LOG_BUFF_LEN];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_nsm_client(&dspbuf, pnsmclient);

      LogFullDebug(COMPONENT_STATE,
                   "state_nlm_notify for %s", str);
    }

  init_glist(&newlocks);

  /* First remove byte range locks.
   * Only accept so many errors before giving up.
   */
  while(errcnt < STATE_ERR_MAX)
    {
      P(pnsmclient->ssc_mutex);

      /* We just need to find any file this client has locks on.
       * We pick the first lock the client holds, and use it's file.
       */
      found_entry = glist_first_entry(&pnsmclient->ssc_lock_list,
                                      state_lock_entry_t,
                                      sle_client_locks);

      /* If we don't find any entries, then we are done. */
      if(found_entry == NULL)
        {
          V(pnsmclient->ssc_mutex);
          break;
        }

      /* Get a reference so the lock entry will still be valid when we release the ssc_mutex */
      lock_entry_inc_ref(found_entry);

      /* Remove from the client lock list */
      glist_del(&found_entry->sle_client_locks);

      if(!release_all && (found_entry->sle_state == pstate))
        {
          /* This is a new lock acquired since the client rebooted, retain it. */
          LogEntry("Don't release new lock", found_entry);
          glist_add_tail(&newlocks, &found_entry->sle_client_locks);
          V(pnsmclient->ssc_mutex);
          continue;
        }

      LogEntry("Release client locks based on", found_entry);

      /* Move this entry to the end of the list (this will help if errors occur) */
      glist_add_tail(&pnsmclient->ssc_lock_list, &found_entry->sle_client_locks);

      V(pnsmclient->ssc_mutex);

      /* Extract the cache inode entry from the lock entry and release the lock entry */
      pentry  = found_entry->sle_pentry;
      powner  = found_entry->sle_owner;
      pexport = found_entry->sle_pexport;

      PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

      lock_entry_dec_ref(found_entry);

      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      /* Make lock that covers the whole file - type doesn't matter for unlock */
      lock.lock_type   = FSAL_LOCK_R;
      lock.lock_start  = 0;
      lock.lock_length = 0;

      /* construct the fsal context based on the export and root credential */
      fsal_status = FSAL_GetClientContext(&fsal_context,
                                          &pexport->FS_export_context,
                                          0,
                                          0,
                                          NULL,
                                          0);
      if(FSAL_IS_ERROR(fsal_status))
        {
          /* log error here , and continue? */
          LogDebug(COMPONENT_STATE,
                   "FSAL_GetClientConext failed");
          continue;
        }

      /* Make sure we hold an lru ref to the cache inode while calling unlock */
      if(cache_inode_lru_ref(pentry, 0) != CACHE_INODE_SUCCESS)
        LogCrit(COMPONENT_STATE,
                "Ugliness - cache_inode_lru_ref has returned non-success");

      /* Remove all locks held by this NLM Client on the file */
      if(state_unlock(pentry,
                      &fsal_context,
                      pexport,
                      powner,
                      pstate,
                      &lock,
                      pstatus) != STATE_SUCCESS)
        {
          /* Increment the error count and try the next lock, with any luck
           * the memory pressure which is causing the problem will resolve itself.
           */
          LogFullDebug(COMPONENT_STATE,
                       "state_unlock returned %s",
                       state_err_str(*pstatus));
          errcnt++;
        }

      /* Release the lru ref to the cache inode we held while calling unlock */
      cache_inode_lru_unref(pentry, 0);
    }

  /* Now remove NLM_SHARE reservations.
   * Only accept so many errors before giving up.
   */
  while(errcnt < STATE_ERR_MAX)
    {
      P(pnsmclient->ssc_mutex);

      /* We just need to find any file this client has locks on.
       * We pick the first lock the client holds, and use it's file.
       */
      found_share = glist_first_entry(&pnsmclient->ssc_share_list,
                                      state_nlm_share_t,
                                      sns_share_per_client);

      /* If we don't find any entries, then we are done. */
      if(found_share == NULL)
        {
          V(pnsmclient->ssc_mutex);
          break;
        }

      /* Extract the cache inode entry from the share */
      pentry  = found_share->sns_pentry;
      powner  = found_share->sns_powner;
      pexport = found_share->sns_pexport;

      /* get a reference to the owner */
      inc_state_owner_ref(powner);

      V(pnsmclient->ssc_mutex);

      /* construct the fsal context based on the export and root credential */
      fsal_status = FSAL_GetClientContext(&fsal_context,
                                          &pexport->FS_export_context,
                                          0,
                                          0,
                                          NULL,
                                          0);
      if(FSAL_IS_ERROR(fsal_status))
        {
          dec_state_owner_ref(powner);

          /* log error here , and continue? */
          LogDebug(COMPONENT_STATE,
                   "FSAL_GetClientConext failed");
          continue;
        }

      /* Remove all shares held by this NSM Client and Owner on the file */
      if(state_nlm_unshare(pentry,
                           &fsal_context,
                           OPEN4_SHARE_ACCESS_NONE,
                           OPEN4_SHARE_DENY_NONE,
                           powner,
                           pstatus) != STATE_SUCCESS)
        {
          /* Increment the error count and try the next share, with any luck
           * the memory pressure which is causing the problem will resolve itself.
           */
          LogFullDebug(COMPONENT_STATE,
                       "state_nlm_unshare returned %s",
                       state_err_str(*pstatus));
          errcnt++;
        }

      dec_state_owner_ref(powner);
    }

  /* Put locks from current client incarnation onto end of list */
  P(pnsmclient->ssc_mutex);
  glist_add_list_tail(&pnsmclient->ssc_lock_list, &newlocks);
  V(pnsmclient->ssc_mutex);
  LogFullDebug(COMPONENT_STATE, "DONE");

  return *pstatus;
}
#endif

/**
 *
 * state_owner_unlock_all: Release all locks held by a lock owner
 *
 */
state_status_t state_owner_unlock_all(fsal_op_context_t    * pcontext,
                                      state_owner_t        * powner,
                                      state_t              * pstate,
                                      state_status_t       * pstatus)
{
  state_lock_entry_t * found_entry;
  exportlist_t       * pexport;
  fsal_lock_param_t    lock;
  cache_entry_t      * pentry;
  int                  errcnt = 0;

  /* Only accept so many errors before giving up. */
  while(errcnt < STATE_ERR_MAX)
    {
      P(powner->so_mutex);

      /* We just need to find any file this owner has locks on.
       * We pick the first lock the owner holds, and use it's file.
       */
      found_entry = glist_first_entry(&powner->so_lock_list, state_lock_entry_t, sle_owner_locks);

      /* If we don't find any entries, then we are done. */
      if((found_entry == NULL) || (found_entry->sle_state != pstate))
      {
        V(powner->so_mutex);
        break;
      }

      lock_entry_inc_ref(found_entry);

      /* Move this entry to the end of the list (this will help if errors occur) */
      glist_del(&found_entry->sle_owner_locks);
      glist_add_tail(&powner->so_lock_list, &found_entry->sle_owner_locks);

      V(powner->so_mutex);

      /* Extract the cache inode entry from the lock entry and release the lock entry */
      pentry  = found_entry->sle_pentry;
      pexport = found_entry->sle_pexport;

      PTHREAD_RWLOCK_WRLOCK(&pentry->state_lock);

      lock_entry_dec_ref(found_entry);

      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);

      /* Make lock that covers the whole file - type doesn't matter for unlock */
      lock.lock_type   = FSAL_LOCK_R;
      lock.lock_start  = 0;
      lock.lock_length = 0;

      /* Make sure we hold an lru ref to the cache inode while calling unlock */
      if(cache_inode_lru_ref(pentry, 0) != CACHE_INODE_SUCCESS)
        LogCrit(COMPONENT_STATE,
                "Ugliness - cache_inode_lru_ref has returned non-success");

      /* Remove all locks held by this owner on the file */
      if(state_unlock(pentry,
                      pcontext,
                      pexport,
                      powner,
                      pstate,
                      &lock,
                      pstatus) != STATE_SUCCESS)
        {
          /* Increment the error count and try the next lock, with any luck
           * the memory pressure which is causing the problem will resolve itself.
           */
          LogDebug(COMPONENT_STATE,
               "state_unlock failed %s",
               state_err_str(*pstatus));
          errcnt++;
        }

      /* Release the lru ref to the cache inode we held while calling unlock */
      cache_inode_lru_unref(pentry, 0);
    }
  return *pstatus;
}

#ifdef _USE_BLOCKING_LOCKS

void find_blocked_lock_upcall(cache_entry_t        * pentry,
                              void                 * powner,
                              fsal_lock_param_t    * plock,
                              state_grant_type_t     grant_type)
{
  state_lock_entry_t   * found_entry;
  struct glist_head    * glist;
  state_block_data_t   * pblock;

  P(blocked_locks_mutex);

  glist_for_each(glist, &state_blocked_locks)
    {
      pblock = glist_entry(glist, state_block_data_t, sbd_list);

      found_entry = pblock->sbd_lock_entry;

      /* Check if for same file */
      if(found_entry->sle_pentry != pentry)
        continue;

      /* Check if for same owner */
      if(found_entry->sle_owner != powner)
        continue;

      /* Check if same lock */
      if(different_lock(&found_entry->sle_lock, plock))
        continue;

      /* Put lock on list of locks granted by FSAL */
      glist_del(&pblock->sbd_list);
      glist_add_tail(&state_notified_locks, &pblock->sbd_list);
      pblock->sbd_grant_type = grant_type;

      LogEntry("Blocked Lock found", found_entry);

      V(blocked_locks_mutex);

      signal_async_work();

      return;
    } /* glist_for_each_safe */

  if(isFullDebug(COMPONENT_STATE) &&
     isFullDebug(COMPONENT_MEMLEAKS))
    LogBlockedList("Blocked Lock List", NULL, &state_blocked_locks);

  V(blocked_locks_mutex);

  if(isFullDebug(COMPONENT_STATE) &&
     isFullDebug(COMPONENT_MEMLEAKS))
    {
      PTHREAD_RWLOCK_RDLOCK(&pentry->state_lock);

      LogList("File Lock List", pentry, &pentry->object.file.lock_list);

      PTHREAD_RWLOCK_UNLOCK(&pentry->state_lock);
    }

  /* We must be out of sync with FSAL, this is fatal */
  LogLockDesc(COMPONENT_STATE, NIV_MAJOR,
              "Blocked Lock Not Found for", pentry, powner, plock);
  LogFatal(COMPONENT_STATE, "Locks out of sync with FSAL");
}

/**
 *
 * grant_blocked_lock_upcall: Handle upcall for granted lock
 *
 */
void grant_blocked_lock_upcall(cache_entry_t        * pentry,
                               void                 * powner,
                               fsal_lock_param_t    * plock)
{
  LogLockDesc(COMPONENT_STATE, NIV_DEBUG,
              "Grant Upcall for", pentry, powner, plock);

  find_blocked_lock_upcall(pentry, powner, plock, STATE_GRANT_FSAL);
}

/**
 *
 * available_blocked_lock_upcall: Handle upcall for available lock
 *
 */
void available_blocked_lock_upcall(cache_entry_t        * pentry,
                                   void                 * powner,
                                   fsal_lock_param_t    * plock)
{
  LogLockDesc(COMPONENT_STATE, NIV_DEBUG,
              "Available Upcall for", pentry, powner, plock);

  find_blocked_lock_upcall(pentry, powner, plock, STATE_GRANT_FSAL_AVAILABLE);
}

#endif

void state_lock_wipe(cache_entry_t        * pentry)
{
  if(glist_empty(&pentry->object.file.lock_list))
    return;

  free_list(&pentry->object.file.lock_list);

  cache_inode_dec_pin_ref(pentry, FALSE);
}

#ifdef _USE_NLM
void cancel_all_nlm_blocked()
{
  struct glist_head *glist, *glistn;
  state_owner_t *nlm_owner;
  state_lock_entry_t *found_entry;
  exportlist_t *pexport;
  cache_entry_t *pentry;
  fsal_op_context_t    fsal_context;
  fsal_status_t        fsal_status;
  state_block_data_t *pblock;
  state_status_t state_status = STATE_SUCCESS;

  LogDebug(COMPONENT_STATE, "Cancel all blocked locks");

  P(blocked_locks_mutex);

  if(glist_empty(&state_blocked_locks))
    {
      LogFullDebug(COMPONENT_STATE, "No blocked locks");
      V(blocked_locks_mutex);
      return;
    }

  glist_for_each_safe(glist, glistn, &state_blocked_locks)
    {
      pblock = glist_entry(glist, state_block_data_t, sbd_list);

      found_entry = pblock->sbd_lock_entry;

      /* Check if got an entry */
      if(found_entry == NULL)
      {
        LogWarn(COMPONENT_STATE, "Blocked lock without an state_lock_entry_t");
        continue;
      }

      /* Remove lock from blocked list */
      glist_del(&pblock->sbd_list);

      lock_entry_inc_ref(found_entry);

      V(blocked_locks_mutex);

      LogEntry("Blocked Lock found", found_entry);

      pentry = found_entry->sle_pentry;
      nlm_owner = found_entry->sle_owner;
      pexport = found_entry->sle_pexport;

      /* Construct the fsal context based on the export and root credential */
      fsal_status = FSAL_GetClientContext(&fsal_context,
                                          &pexport->FS_export_context,
                                          0,
                                          0,
                                          NULL,
                                          0);

      state_status = cancel_blocked_lock(pentry, &fsal_context, found_entry);

      if(pblock->sbd_blocked_cookie != NULL)
        gsh_free(pblock->sbd_blocked_cookie);

      gsh_free(found_entry->sle_block_data);
      found_entry->sle_block_data = NULL;

      LogEntry("Canceled Lock", found_entry);

      lock_entry_dec_ref(found_entry);

      P(blocked_locks_mutex);
    } /* glist_for_each_safe */

    V(blocked_locks_mutex);
    return;
}

void
resub_all_nlm_blocked()
{
        struct glist_head       *glist;
        struct glist_head       *glistn;
        state_owner_t           *nlm_owner;
        state_lock_entry_t      *found_entry;
        exportlist_t            *pexport;
        cache_entry_t           *pentry;
        fsal_op_context_t        fsal_context;
        fsal_op_context_t       *pcontext;
        fsal_lock_param_t       *plock;
        fsal_status_t            fsal_status;
        state_block_data_t      *pblock;
        state_status_t           state_status = STATE_SUCCESS;

        LogDebug(COMPONENT_STATE, "Resubmit all blocked locks");

        P(blocked_locks_mutex);

        if (glist_empty(&state_blocked_locks)) {
                LogFullDebug(COMPONENT_STATE, "No blocked locks to resubmit");
                V(blocked_locks_mutex);
                return;
        }

        glist_for_each_safe(glist, glistn, &state_blocked_locks) {
                pblock = glist_entry(glist, state_block_data_t, sbd_list);

                /* Check if got an entry */
                if ((found_entry = pblock->sbd_lock_entry) == NULL) {
                        LogWarn(COMPONENT_STATE,
                            "Blocked lock w/o a state_lock_entry_t");
                        continue;
                }
                lock_entry_inc_ref(found_entry);
                V(blocked_locks_mutex);

                LogEntry("Blocked Lock found", found_entry);

                pentry = found_entry->sle_pentry;
                nlm_owner = found_entry->sle_owner;
                pexport = found_entry->sle_pexport;
                plock = &found_entry->sle_lock;
                pcontext = &fsal_context;

                /*
                 * Reconstruct the FSAL context based
                 * on the export and root credential
                 */
                fsal_status = FSAL_GetClientContext(
                                         pcontext,
                                         &pexport->FS_export_context,
                                         0,
                                         0,
                                         NULL,
                                         0);

                state_status = do_lock_op(pentry,
                                          pcontext,
                                          pexport,
                                          FSAL_OP_LOCKB,
                                          0, /*Not a reclaim */
                                          nlm_owner,
                                          plock,
                                          NULL,
                                          NULL,
                                          FALSE);

                switch (state_status) {
                case STATE_SUCCESS:
                        /* Resubmitted block successfully; let client know */
                        glist_del(&pblock->sbd_list);
                        found_entry->sle_block_data->sbd_grant_type =
                                STATE_GRANT_FSAL;
                        (void) nlm_granted_callback(pentry, found_entry,
                            &state_status);
                        LogEntry("Lock successfully acquired", found_entry);
                        break;

                case STATE_LOCK_BLOCKED:
                        /* Able to resubmit blocked lock */
                        LogEntry("Lock successfully resubmitted", found_entry);
                        lock_entry_dec_ref(found_entry);
                        break;

                default:
                        /* Anything else, we throw up our hands... */
                        LogMajor(COMPONENT_STATE,
                                "WARNING: Blocked Lock %s got %s status",
                                str_blocked(found_entry->sle_blocked),
                                state_err_str(state_status));
                        remove_from_locklist(found_entry);
                }
                P(blocked_locks_mutex);
        }
        V(blocked_locks_mutex);
        return;
}
#endif
