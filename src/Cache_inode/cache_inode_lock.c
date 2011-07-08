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
 * \file    cache_inode_lock.c
 * \author  $Author: deniel $
 * \date    $Date$
 * \version $Revision$
 * \brief   This file contains functions used in lock management.
 *
 * cache_inode_lock.c : This file contains functions used in lock management.
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

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#include "nfs_core.h"

/*
 * cache_lock_entry_t locking rule:
 * The value is always updated/read with nlm_lock_entry->lock held
 * If we have nlm_lock_list mutex held we can read it safely, because the
 * value is always updated while walking the list with pentry->object.file.lock_list_mutex held.
 * The updation happens as below:
 *  pthread_mutex_lock(pentry->object.file.lock_list_mutex)
 *  pthread_mutex_lock(lock_entry->cle_mutex)
 *    update the lock_entry value
 *  ........
 * The value is ref counted with nlm_lock_entry->cle_ref_count so that a
 * parallel cancel/unlock won't endup freeing the datastructure. The last
 * release on the data structure ensure that it is freed.
 */
#ifdef _DEBUG_MEMLEAKS
static struct glist_head cache_inode_all_locks;
pthread_mutex_t all_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef _USE_NLM
hash_table_t *ht_lock_cookies;
#endif

cache_lock_owner_t unknown_owner;

#ifdef _USE_NLM
cache_inode_status_t cache_inode_lock_init(cache_inode_status_t * pstatus,
                                           hash_parameter_t       cookie_param)
#else
cache_inode_status_t cache_inode_lock_init(cache_inode_status_t * pstatus)
#endif
{
  *pstatus = CACHE_INODE_SUCCESS;

  memset(&unknown_owner, 0, sizeof(unknown_owner));
  unknown_owner.clo_type     = CACHE_LOCK_OWNER_UNKNOWN;
  unknown_owner.clo_refcount = 1;
  init_glist(&unknown_owner.clo_lock_list);

  if(pthread_mutex_init(&unknown_owner.clo_mutex, NULL) == -1)
    {
      *pstatus = CACHE_INODE_INIT_ENTRY_FAILED;
      return *pstatus;
    }

#ifdef _USE_NLM
  ht_lock_cookies = HashTable_Init(cookie_param);
  if(ht_lock_cookies == NULL)
    {
      LogCrit(COMPONENT_NLM,
              "Cannot init NLM Client cache");
      *pstatus = CACHE_INODE_INIT_ENTRY_FAILED;
      return *pstatus;
    }
#endif
#ifdef _DEBUG_MEMLEAKS
  init_glist(&cache_inode_all_locks);
#endif

  return *pstatus;
}

cache_inode_status_t FSAL_LockOp(cache_entry_t        * pentry,
                                 fsal_op_context_t    * pcontext,
                                 fsal_lock_op_t         lock_op,
                                 cache_lock_owner_t   * powner,
                                 cache_lock_desc_t    * plock,
                                 cache_lock_owner_t  ** holder,    /* owner that holds conflicting lock */
                                 cache_lock_desc_t    * conflict); /* description of conflicting lock */

/******************************************************************************
 *
 * Functions to display various aspects of a lock
 *
 ******************************************************************************/
int DisplayOwner(cache_lock_owner_t *powner, char *buf)
{
  char *tmp = buf;

  if(powner != NULL)
    switch(powner->clo_type)
      {
#ifdef _USE_NLM
        case CACHE_LOCK_OWNER_NLM:
          tmp += sprintf(buf, "CACHE_LOCK_OWNER_NLM: ");
          return (tmp-buf) + display_nlm_owner(powner, tmp);
          break;
#endif

        case CACHE_LOCK_OWNER_NFSV4:
          tmp += sprintf(buf, "CACHE_LOCK_OWNER_NFSV4: ");
          return (tmp-buf) + sprintf(buf, "undecoded");
          break;

        case CACHE_LOCK_OWNER_UNKNOWN:
          return sprintf(buf, "CACHE_LOCK_OWNER_UNKNOWN");
          break;
    }

  return sprintf(buf, "N/A");
}

static inline uint64_t lock_end(cache_lock_desc_t *plock)
{
  if(plock->cld_length == 0)
    return UINT64_MAX;
  else
    return plock->cld_offset + plock->cld_length - 1;
}

const char *str_lockt(cache_lock_t ltype)
{
  switch(ltype)
    {
      case CACHE_INODE_LOCK_R:  return "READ ";
      case CACHE_INODE_LOCK_W:  return "WRITE";
      case CACHE_INODE_NO_LOCK: return "NO LOCK";
      default:                 return "?????";
    }
  return "????";
}

const char *str_blocking(cache_blocking_t blocking)
{
  switch(blocking)
    {
      case CACHE_NON_BLOCKING:   return "NON_BLOCKING  ";
      case CACHE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case CACHE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      case CACHE_GRANTING:       return "GRANTING      ";
      case CACHE_CANCELED:       return "CANCELED      ";
    }
  return "unknown       ";
}

const char *str_blocked(cache_blocking_t blocked)
{
  switch(blocked)
    {
      case CACHE_NON_BLOCKING:   return "GRANTED       ";
      case CACHE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case CACHE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      case CACHE_GRANTING:       return "GRANTING      ";
      case CACHE_CANCELED:       return "CANCELED      ";
    }
  return "unknown       ";
}

int display_lock_cookie(const char *cookie, int len, char *str)
{
  memcpy(str, cookie, len);
  str[len] = '\0';
  return len;
}

/******************************************************************************
 *
 * Functions to compare lock owners and lock parameters
 *
 ******************************************************************************/

/* This is not complete, it doesn't check the owner's IP address...*/
static inline int different_owners(cache_lock_owner_t *powner1, cache_lock_owner_t *powner2)
{
  /* Shortcut in case we actually are pointing to the same owner structure */
  if(powner1 == powner2)
    return 0;

  if(powner1->clo_type != powner2->clo_type)
    return 1;

  switch(powner1->clo_type)
    {
#ifdef _USE_NLM
      case CACHE_LOCK_OWNER_NLM:
        return compare_nlm_owner(powner1, powner2);
#endif
      case CACHE_LOCK_OWNER_NFSV4:
      default:
        return powner1 != powner2;
    }
}

static inline int different_lock(cache_lock_desc_t *lock1, cache_lock_desc_t *lock2)
{
  return (lock1->cld_type   != lock2->cld_type  ) ||
         (lock1->cld_offset != lock2->cld_offset) ||
         (lock1->cld_length != lock2->cld_length);
}

bool_t same_cookie(char * pcookie1,
                   int    cookie_size1,
                   char * pcookie2,
                   int    cookie_size2)
{
  if(pcookie1 == pcookie2)
    return TRUE;

  if(cookie_size1 != cookie_size2)
    return FALSE;

  if(pcookie1 == NULL || pcookie2 == NULL)
    return FALSE;

  return memcmp(pcookie1, pcookie2, cookie_size1) == 0;
}

/******************************************************************************
 *
 * Functions to log locks in various ways
 *
 ******************************************************************************/
static void LogEntry(const char         *reason, 
                     cache_entry_t      *pentry,
                     fsal_op_context_t  *pcontext,
                     cache_lock_entry_t *ple)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char owner[HASHTABLE_DISPLAY_STRLEN];
      uint64_t fileid_digest = 0;

      DisplayOwner(ple->cle_owner, owner);

      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                        FSAL_DIGEST_FILEID3,
                        &(pentry->object.file.handle),
                        (caddr_t) &fileid_digest);

      LogFullDebug(COMPONENT_NLM,
                   "%s Entry: %p fileid=%llu, owner=%s, type=%s, start=0x%llx, end=0x%llx, blocked=%s",
                   reason, pentry, (unsigned long long) fileid_digest,
                   owner, str_lockt(ple->cle_lock.cld_type),
                   (unsigned long long) ple->cle_lock.cld_offset,
                   (unsigned long long) lock_end(&ple->cle_lock),
                   str_blocked(ple->cle_blocked));
    }
}

static void LogLock(const char         *reason, 
                    cache_entry_t      *pentry,
                    fsal_op_context_t  *pcontext,
                    cache_lock_owner_t *powner,
                    cache_lock_desc_t  *plock)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char owner[HASHTABLE_DISPLAY_STRLEN];
      uint64_t fileid_digest = 0;

      DisplayOwner(powner, owner);

      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                        FSAL_DIGEST_FILEID3,
                        &(pentry->object.file.handle),
                        (caddr_t) &fileid_digest);

      LogFullDebug(COMPONENT_NLM,
                   "%s Lock: fileid=%llu, owner=%s, type=%s, start=0x%llx, end=0x%llx",
                   reason, (unsigned long long) fileid_digest,
                   owner, str_lockt(plock->cld_type),
                   (unsigned long long) plock->cld_offset,
                   (unsigned long long) lock_end(plock));
    }
}

void LogUnlock(cache_entry_t      *pentry,
               fsal_op_context_t  *pcontext,
               cache_lock_entry_t *ple)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      uint64_t fileid_digest = 0;

      FSAL_DigestHandle(FSAL_GET_EXP_CTX(pcontext),
                        FSAL_DIGEST_FILEID3,
                        &(pentry->object.file.handle),
                        (caddr_t) &fileid_digest);

      LogFullDebug(COMPONENT_NLM,
                   "FSAL Unlock fileid=%llu, type=%s, start=0x%llx, end=0x%llx",
                   (unsigned long long) fileid_digest,
                   str_lockt(ple->cle_lock.cld_type),
                   (unsigned long long) ple->cle_lock.cld_offset,
                   (unsigned long long) lock_end(&ple->cle_lock));
    }
}

#if 0
void dump_entry(cache_lock_entry_t *entry)
{
  char fh_buff[1024], oh_buff[1024];
  struct nlm4_lockargs *arg = &entry->arg;
  struct nlm4_lock *nlm_lock = &arg->alock;

  netobj_to_string(&nlm_lock->oh, oh_buff, 1023);
  netobj_to_string(&nlm_lock->fh, fh_buff, 1023);
  LogTest("Entry: %p caller=%s, fh=%s, oh=%s, svid=%d, start=0x%llx, end=0x%llx, exclusive=%d, state=%s, ref_count=%d",
          entry, nlm_lock->caller_name, fh_buff,
          oh_buff, nlm_lock->svid,
          (unsigned long long) nlm_lock->cle_lock.cld_offset,
          (unsigned long long) lock_end(nlm_lock->cle_lock.cld_offset, nlm_lock->cle_lock.cld_length),
          arg->exclusive, lock_result_str(entry->state), entry->cle_ref_count);
}
#endif

#if 0
void dump_lock_list(void)
{
  struct glist_head *glist;
  cache_lock_entry_t *entry = NULL;
  int count = 0;

  LogTest("\nLOCK LIST");
  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      count++;
      dump_entry(entry);
    }
  if(count)
    LogTest("\n");
  else
    LogTest("(empty)\n");
}

void dump_all_locks(void)
{
#ifdef _DEBUG_MEMLEAKS
  struct glist_head *glist;
  cache_lock_entry_t *entry = NULL;
  int count = 0;

  P(all_locks_mutex);
  LogTest("\nALL LOCKS");
  glist_for_each(glist, &cache_inode_all_locks)
    {
      entry = glist_entry(glist, cache_lock_entry_t, cle_all_locks);

      count++;
      dump_entry(entry);
    }
  if(count)
    LogTest("\n");
  else
    LogTest("(empty)\n");
  V(all_locks_mutex);
#else
  return;
#endif
}
#endif

/******************************************************************************
 *
 * Functions to manage lock entries and lock list
 *
 ******************************************************************************/
void release_lock_owner(cache_lock_owner_t *powner)
{
  switch(powner->clo_type)
    {
#ifdef _USE_NLM
      case CACHE_LOCK_OWNER_NLM:
        dec_nlm_client_ref(powner->clo_owner.clo_nlm_owner.clo_client);
        dec_nlm_owner_ref(powner);
        break;
#endif

      case CACHE_LOCK_OWNER_NFSV4:
      case CACHE_LOCK_OWNER_UNKNOWN:
        P(powner->clo_mutex);
        powner->clo_refcount--;
        V(powner->clo_mutex);
        break;
    }
}

void get_lock_owner(cache_lock_owner_t *powner)
{
  P(powner->clo_mutex);
  powner->clo_refcount++;
  V(powner->clo_mutex);

#ifdef _USE_NLM
  if(powner->clo_type == CACHE_LOCK_OWNER_NLM)
    {
      P(powner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
      powner->clo_owner.clo_nlm_owner.clo_client->clc_refcount++;
      V(powner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
    }
#endif
}

static cache_lock_entry_t *create_cache_lock_entry(cache_entry_t      * pentry,
                                                   cache_blocking_t     blocked,
                                                   cache_lock_owner_t * powner,
                                                   cache_lock_desc_t  * plock,
                                                   granted_callback_t   granted_callback)
{
  cache_lock_entry_t *new_entry;

  new_entry = (cache_lock_entry_t *) Mem_Alloc_Label(sizeof(*new_entry),
                                                     "cache_lock_entry_t");
  if(!new_entry)
      return NULL;

  memset(new_entry, 0, sizeof(*new_entry));

  if(pthread_mutex_init(&new_entry->cle_mutex, NULL) == -1)
    {
      Mem_Free(new_entry);
      return NULL;
    }

  new_entry->cle_ref_count        = 0;
  new_entry->cle_pentry           = pentry;
  new_entry->cle_blocked          = blocked;
  new_entry->cle_owner            = powner;
  new_entry->cle_granted_callback = granted_callback;
  memcpy(&new_entry->cle_lock, plock, sizeof(new_entry->cle_lock));

  /* Add to list of locks owned by powner */
  P(powner->clo_mutex);
  powner->clo_refcount++;
  glist_add_tail(&powner->clo_lock_list, &new_entry->cle_owner_locks);
  V(powner->clo_mutex);

#ifdef _USE_NLM
  /* Add to list of locks owner by NLM Client */
  if(powner->clo_type == CACHE_LOCK_OWNER_NLM)
    {
      P(powner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
      powner->clo_owner.clo_nlm_owner.clo_client->clc_refcount++;
      glist_add_tail(&powner->clo_owner.clo_nlm_owner.clo_client->clc_lock_list, &new_entry->cle_client_locks);
      V(powner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
    }
#endif
    
#ifdef _DEBUG_MEMLEAKS
    P(all_locks_mutex);
    glist_add_tail(&cache_inode_all_locks, &new_entry->cle_all_locks);
    V(all_locks_mutex);
#endif

  return new_entry;
}

inline cache_lock_entry_t *cache_lock_entry_t_dup(cache_lock_entry_t * orig_entry)
{
  return create_cache_lock_entry(orig_entry->cle_pentry,
                                 orig_entry->cle_blocked,
                                 orig_entry->cle_owner,
                                 &orig_entry->cle_lock,
                                 orig_entry->cle_granted_callback);
}

void lock_entry_inc_ref(cache_lock_entry_t *lock_entry)
{
    P(lock_entry->cle_mutex);
    lock_entry->cle_ref_count++;
    V(lock_entry->cle_mutex);
}

void lock_entry_dec_ref(cache_entry_t      *pentry,
                        fsal_op_context_t  *pcontext,
                        cache_lock_entry_t *lock_entry)
{
  bool_t to_free = FALSE;

  P(lock_entry->cle_mutex);

  lock_entry->cle_ref_count--;

  if(!lock_entry->cle_ref_count)
    {
      /*
       * We should already be removed from the lock_list
       * So we can free the lock_entry without any locking
       */
      to_free = TRUE;
    }

  V(lock_entry->cle_mutex);

  if(to_free)
    {
      LogEntry("nlm_lock_entry_dec_ref Freeing",
               pentry, pcontext, lock_entry);
#ifdef _DEBUG_MEMLEAKS
      P(all_locks_mutex);
      glist_del(&lock_entry->cle_all_locks);
      V(all_locks_mutex);
#endif
      Mem_Free(lock_entry);
    }
}

static void remove_from_locklist(cache_entry_t      *pentry,
                                 fsal_op_context_t  *pcontext,
                                 cache_lock_entry_t *lock_entry)
{
  /*
   * If some other thread is holding a reference to this nlm_lock_entry
   * don't free the structure. But drop from the lock list
   */
  if(lock_entry->cle_owner != NULL)
    {
      P(lock_entry->cle_owner->clo_mutex);
      glist_del(&lock_entry->cle_owner_locks);
      V(lock_entry->cle_owner->clo_mutex);

#ifdef _USE_NLM
      if(lock_entry->cle_owner->clo_type == CACHE_LOCK_OWNER_NLM)
        {
          P(lock_entry->cle_owner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
          glist_del(&lock_entry->cle_client_locks);
          V(lock_entry->cle_owner->clo_owner.clo_nlm_owner.clo_client->clc_mutex);
        }
#endif

      release_lock_owner(lock_entry->cle_owner);
    }

  glist_del(&lock_entry->cle_list);
  lock_entry_dec_ref(pentry, pcontext, lock_entry);
}

static cache_lock_entry_t *get_overlapping_entry(cache_entry_t        * pentry,
                                                 fsal_op_context_t    * pcontext,
                                                 cache_lock_owner_t   * powner,
                                                 cache_lock_desc_t    * plock)
{
  struct glist_head *glist;
  cache_lock_entry_t *found_entry = NULL;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      LogEntry("get_overlapping_entry Checking",
               pentry, pcontext, found_entry);

      /* Skip blocked locks */
      if(found_entry->cle_blocked == CACHE_NLM_BLOCKING ||
         found_entry->cle_blocked == CACHE_NFSV4_BLOCKING)
          continue;

      found_entry_end = lock_end(&found_entry->cle_lock);

      if((found_entry_end >= plock->cld_offset) &&
         (found_entry->cle_lock.cld_offset <= plock_end))
        {
          /* lock overlaps see if we can allow 
           * allow if neither lock is exclusive or the owner is the same
           */
          if((found_entry->cle_lock.cld_type == CACHE_INODE_LOCK_W ||
              plock->cld_type == CACHE_INODE_LOCK_W) &&
             different_owners(found_entry->cle_owner, powner)
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
 * any mapping entry. And l_offset = 0 and cle_lock.cld_length = 0 lock_entry
 * implies remove all entries
 */
static void merge_lock_entry(cache_entry_t        * pentry,
                             fsal_op_context_t    * pcontext,
                             cache_lock_entry_t   * lock_entry)
{
  cache_lock_entry_t *check_entry;
  uint64_t check_entry_end;
  uint64_t lock_entry_end;
  struct glist_head *glist, *glistn;

  /* lock_entry might be CACHE_NON_BLOCKING or CACHE_GRANTING */

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      check_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      /* Skip entry being merged - it could be in the list */
      if(check_entry == lock_entry)
        continue;

      if(different_owners(check_entry->cle_owner, lock_entry->cle_owner))
        continue;

      /* Don't merge blocked locks */
      if(check_entry->cle_blocked != CACHE_NON_BLOCKING)
        continue;

      /* Don't merge locks of different types */
      if(check_entry->cle_lock.cld_type != lock_entry->cle_lock.cld_type)
        continue;

      check_entry_end = lock_end(&check_entry->cle_lock);
      lock_entry_end  = lock_end(&lock_entry->cle_lock);

      if((check_entry_end + 1) < lock_entry->cle_lock.cld_offset)
        /* nothing to merge */
        continue;

      if((lock_entry_end + 1) < check_entry->cle_lock.cld_offset)
        /* nothing to merge */
        continue;

      /* check_entry touches or overlaps lock_entry, expand lock_entry */
      if(lock_entry_end < check_entry_end)
        /* Expand end of lock_entry */
        lock_entry_end = check_entry_end;

      if(check_entry->cle_lock.cld_offset < lock_entry->cle_lock.cld_offset)
        /* Expand start of lock_entry */
        check_entry->cle_lock.cld_offset = lock_entry->cle_lock.cld_offset;

      /* Compute new lock length */
      lock_entry->cle_lock.cld_length = lock_entry_end - check_entry->cle_lock.cld_offset + 1;

      /* Remove merged entry */
      LogEntry("nlm_merge_lock_entry Merging", pentry, pcontext, check_entry);
      remove_from_locklist(pentry, pcontext, check_entry);
    }
}

static void free_list(cache_entry_t        * pentry,
                      fsal_op_context_t    * pcontext,
                      struct glist_head    * list)
{
  cache_lock_entry_t *found_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      remove_from_locklist(pentry, pcontext, found_entry);
    }
}

/* Subtract a lock from a lock entry, placing any remaining bits into the split list. */
static bool_t subtract_lock_from_entry(cache_entry_t        * pentry,
                                       fsal_op_context_t    * pcontext,
                                       cache_lock_entry_t   * found_entry,
                                       cache_lock_desc_t    * plock,
                                       struct glist_head    * split_list,
                                       cache_inode_status_t * pstatus)
{
  uint64_t found_entry_end = lock_end(&found_entry->cle_lock);
  uint64_t plock_end = lock_end(plock);
  cache_lock_entry_t *found_entry_left = NULL;
  cache_lock_entry_t *found_entry_right = NULL;

  *pstatus = CACHE_INODE_SUCCESS;

  if(plock_end < found_entry->cle_lock.cld_offset)
    /* nothing to split */
    return FALSE;

  if(found_entry_end < plock->cld_offset)
    /* nothing to split */
    return FALSE;

  if((plock->cld_offset <= found_entry->cle_lock.cld_offset) &&
     plock_end >= found_entry_end)
    {
      /* Fully overlap */
      LogEntry("subtract_lock_from_entry Remove Complete",
               pentry, pcontext, found_entry);
      goto complete_remove;
    }

  LogEntry("subtract_lock_from_entry Split",
           pentry, pcontext, found_entry);

  /* Delete the old entry and add one or two new entries */
  if(plock->cld_offset > found_entry->cle_lock.cld_offset)
    {
      found_entry_left = cache_lock_entry_t_dup(found_entry);
      if(found_entry_left == NULL)
        {
          free_list(pentry, pcontext, split_list);
          *pstatus = CACHE_INODE_MALLOC_ERROR;
          return FALSE;
        }

      found_entry_left->cle_lock.cld_length = plock->cld_offset - found_entry->cle_lock.cld_offset;
      LogEntry("subtract_lock_from_entry left split",
               pentry, pcontext, found_entry_left);
      glist_add_tail(split_list, &(found_entry_left->cle_list));
    }

  if(plock_end < found_entry_end)
    {
      found_entry_right = cache_lock_entry_t_dup(found_entry);
      if(found_entry_right == NULL)
        {
          free_list(pentry, pcontext, split_list);
          *pstatus = CACHE_INODE_MALLOC_ERROR;
          return FALSE;
        }

      found_entry_right->cle_lock.cld_offset = plock_end + 1;
      found_entry_right->cle_lock.cld_length = found_entry_end - plock_end;
      LogEntry("subtract_lock_from_entry right split",
               pentry, pcontext, found_entry_right);
      glist_add_tail(split_list, &(found_entry_right->cle_list));
    }

complete_remove:
  remove_from_locklist(pentry, pcontext, found_entry);

  return TRUE;
}

/* Subtract a lock from a list of locks, possibly splitting entries in the list. */
static bool_t subtract_lock_from_list(cache_entry_t        * pentry,
                                      fsal_op_context_t    * pcontext,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * plock,
                                      cache_inode_status_t * pstatus,
                                      struct glist_head    * list)
{
  cache_lock_entry_t *found_entry;
  struct glist_head split_lock_list;
  struct glist_head *glist, *glistn;
  bool_t rc = FALSE;

  init_glist(&split_lock_list);

  glist_for_each_safe(glist, glistn, list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      if(powner != NULL && different_owners(found_entry->cle_owner, powner))
          continue;
      /*
       * We have matched owner.
       * Even though we are taking a reference to found_entry, we
       * don't inc the ref count because we want to drop the lock entry.
       */
      rc |= subtract_lock_from_entry(pentry, pcontext, found_entry, plock, &split_lock_list, pstatus);
      //TODO FSF: deal with pstatus
    }

  /* now add the split lock list */
  glist_add_list_tail(list, &split_lock_list);

  return rc;
}

static cache_inode_status_t subtract_list_from_list(cache_entry_t        * pentry,
                                                    fsal_op_context_t    * pcontext,
                                                    struct glist_head    * target,
                                                    struct glist_head    * source,
                                                    cache_inode_status_t * pstatus)
{
  cache_lock_entry_t *found_entry;
  struct glist_head *glist, *glistn;

  *pstatus = CACHE_INODE_SUCCESS;

  glist_for_each_safe(glist, glistn, source)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      subtract_lock_from_list(pentry,
                              pcontext,
                              NULL,
                              &found_entry->cle_lock,
                              pstatus,
                              target);
      if(*pstatus != CACHE_INODE_SUCCESS)
        break;
    }
  return *pstatus;
}

/******************************************************************************
 *
 * Implement hash table to hash blocked lock entries by cookie
 *
 ******************************************************************************/

#ifdef _USE_NLM
int display_lock_cookie_key(hash_buffer_t * pbuff, char *str)
{
  return display_lock_cookie((char *)pbuff->pdata, pbuff->len, str);
}

int display_lock_cookie_val(hash_buffer_t * pbuff, char *str)
{
  cache_cookie_entry_t *he = (cache_cookie_entry_t *)pbuff->pdata;
  char *tmp = str;
  uint64_t fileid_digest = 0;

  FSAL_DigestHandle(FSAL_GET_EXP_CTX(he->lce_pcontext),
                    FSAL_DIGEST_FILEID3,
                    &(he->lce_pentry->object.file.handle),
                    (caddr_t) &fileid_digest);

  tmp += sprintf(tmp, "Entry: %p fileid=%llu, owner=",
                 he->lce_pentry, (unsigned long long) fileid_digest);

  tmp += DisplayOwner(he->lce_lock_entry->cle_owner, tmp);

  tmp += sprintf(tmp, ", type=%s, start=0x%llx, end=0x%llx, blocked=%s",
                 str_lockt(he->lce_lock_entry->cle_lock.cld_type),
                 (unsigned long long) he->lce_lock_entry->cle_lock.cld_offset,
                 (unsigned long long) lock_end(&he->lce_lock_entry->cle_lock),
                 str_blocked(he->lce_lock_entry->cle_blocked));

  return tmp - str;
}

int compare_lock_cookie_key(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  return same_cookie((char *)buff1->pdata,
                     buff1->len,
                     (char *)buff2->pdata,
                     buff2->len) ? 0 : 1;
}

unsigned long lock_cookie_value_hash_func(hash_parameter_t * p_hparam,
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

  LogFullDebug(COMPONENT_NLM,
               "---> rbt_hash_val = %lu", res % p_hparam->index_size);

  return (unsigned long)(res % p_hparam->index_size);
}

unsigned long lock_cookie_rbt_hash_func(hash_parameter_t * p_hparam,
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

  LogFullDebug(COMPONENT_NLM, "---> rbt_hash_func = %lu", res);

  return res;
}

static int Hash_del_cookie_entry_ref(hash_buffer_t *buffval)
{
  int rc;
  cache_cookie_entry_t *p_cookie_entry = (cache_cookie_entry_t *)(buffval->pdata);

  P(p_cookie_entry->lce_mutex);

  p_cookie_entry->lce_refcount--;
  rc = p_cookie_entry->lce_refcount;

  V(p_cookie_entry->lce_mutex);  

  return rc;
}

static void Hash_inc_client_ref(hash_buffer_t *buffval)
{
  cache_cookie_entry_t *p_cookie_entry = (cache_cookie_entry_t *)(buffval->pdata);

  P(p_cookie_entry->lce_mutex);
  p_cookie_entry->lce_refcount++;
  V(p_cookie_entry->lce_mutex);  
}

void cookie_entry_inc_ref(cache_cookie_entry_t * p_cookie_entry)
{
  P(p_cookie_entry->lce_mutex);
  p_cookie_entry->lce_refcount++;
  V(p_cookie_entry->lce_mutex);
}

void cookie_entry_unhook(cache_cookie_entry_t * p_cookie_entry)
{
  cache_entry_t * pentry = p_cookie_entry->lce_pentry;

  P(pentry->object.file.lock_list_mutex);

  /* Don't need reference to lock entry any more */
  lock_entry_dec_ref(p_cookie_entry->lce_pentry,
                     p_cookie_entry->lce_pcontext,
                     p_cookie_entry->lce_lock_entry);

  V(pentry->object.file.lock_list_mutex);
}

void cookie_entry_dec_ref(cache_cookie_entry_t * p_cookie_entry)
{
  bool_t remove = FALSE;

  P(p_cookie_entry->lce_mutex);
  if(p_cookie_entry->lce_refcount > 1)
    p_cookie_entry->lce_refcount--;
  else
    remove = TRUE;
  V(p_cookie_entry->lce_mutex);
  if(remove)
    {
      hash_buffer_t buffkey, old_key, old_value;

      buffkey.pdata = (caddr_t) p_cookie_entry;
      buffkey.len = sizeof(*p_cookie_entry);

      switch(HashTable_DelRef(ht_lock_cookies, &buffkey, &old_key, &old_value, Hash_del_cookie_entry_ref))
        {
          case HASHTABLE_SUCCESS:
            /* Removed from hash table, now unlink from lock entry */
            cookie_entry_unhook((cache_cookie_entry_t *)old_value.pdata);
            Mem_Free(old_key.pdata);
            Mem_Free(old_value.pdata);
            break;

          case HASHTABLE_NOT_DELETED:
            /* ref count didn't end up at 0, don't free. */
            break;

          default:
            /* some problem occurred */
            LogFullDebug(COMPONENT_NLM,
                         "HashTable_Del failed");
            break;
        }
    }
}

int cache_inode_insert_block(cache_entry_t            * pentry,
                             fsal_op_context_t        * pcontext,
                             void                     * pcookie,
                             int                        cookie_size,
                             cache_lock_entry_t       * lock_entry,
                             cache_inode_status_t     * pstatus)
{
  hash_buffer_t buffkey, buffval;
  cache_cookie_entry_t *hash_entry;
  char str[HASHTABLE_DISPLAY_STRLEN];

  if(isFullDebug(COMPONENT_NLM))
    display_lock_cookie(pcookie, cookie_size, str);

  hash_entry = (cache_cookie_entry_t *) Mem_Alloc(sizeof(*hash_entry));
  if(hash_entry == NULL)
    {
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_insert_block => KEY {%s} NO MEMORY",
                   str);
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }

  lock_entry_inc_ref(lock_entry);

  if(pthread_mutex_init(&hash_entry->lce_mutex, NULL) == -1)
    {
      Mem_Free(hash_entry);
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_insert_block => KEY {%s} COULD NOT INIT MUTEX",
                   str);
      *pstatus = CACHE_INODE_POOL_MUTEX_INIT_ERROR;
      return *pstatus;
    }

  hash_entry->lce_refcount   = 1;
  hash_entry->lce_pentry     = pentry;
  hash_entry->lce_pcontext   = pcontext;
  hash_entry->lce_lock_entry = lock_entry;

  buffkey.pdata = pcookie;
  buffkey.len   = cookie_size;
  buffval.pdata = (void *)hash_entry;
  buffval.len   = sizeof(*hash_entry);

  if(HashTable_Test_And_Set
     (ht_lock_cookies, &buffkey, &buffval,
      HASHTABLE_SET_HOW_SET_NO_OVERWRITE) != HASHTABLE_SUCCESS)
    {
      Mem_Free(hash_entry);
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_insert_block => KEY {%s} HASH TABLE ERROR",
                   str);
      *pstatus = CACHE_INODE_HASH_TABLE_ERROR;
      return *pstatus;
    }

  /* Increment lock entry reference count and link to lock_entry */
  lock_entry_inc_ref(lock_entry);
  lock_entry->cle_blocked_cookie = hash_entry;

  LogFullDebug(COMPONENT_NLM,
               "cache_inode_insert_block => KEY {%s} SUCCESS",
               str);
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}

cache_inode_status_t cache_inode_find_block(void                  * pcookie,
                                            int                     cookie_size,
                                            cache_cookie_entry_t ** p_cookie_entry,
                                            cache_inode_status_t  * pstatus)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  char str[HASHTABLE_DISPLAY_STRLEN];

  buffkey.pdata = (caddr_t) pcookie;
  buffkey.len   = cookie_size;

  if(isFullDebug(COMPONENT_NLM))
    display_lock_cookie_key(&buffkey, str);

  LogFullDebug(COMPONENT_NLM,
               "cache_inode_find_block => KEY {%s}", str);

  if(HashTable_GetRef(ht_lock_cookies, &buffkey, &buffval, Hash_inc_client_ref) != HASHTABLE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_find_block => KEY {%s} NOTFOUND",
                   str);
      *pstatus = CACHE_INODE_NOT_FOUND;
      return *pstatus;
    }

  *p_cookie_entry = (cache_cookie_entry_t *) buffval.pdata;

  LogFullDebug(COMPONENT_NLM,
               "cache_inode_find_block => {%s} FOUND",
               str);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}
#endif

void grant_blocked_lock(cache_entry_t      *pentry,
                        fsal_op_context_t  *pcontext,
                        cache_lock_entry_t *lock_entry)
{
#ifdef _USE_NLM
  cache_cookie_entry_t *pcookie = lock_entry->cle_blocked_cookie;
#endif

  /* Mark lock as granted and detach cookie and granted call back */
  lock_entry->cle_blocked          = CACHE_NON_BLOCKING;
  lock_entry->cle_blocked_cookie   = NULL;
  lock_entry->cle_granted_callback = NULL;

#ifdef _USE_NLM
  /* Don't need reference to cookie entry any more */
  if(pcookie != NULL)
    cookie_entry_dec_ref(pcookie);
#endif

  /* Merge any touching or overlapping locks into this one. */
  merge_lock_entry(pentry, pcontext, lock_entry);
  LogEntry("grant_blocked_lock granted entry",
           pentry, pcontext, lock_entry);
}

#ifdef _USE_NLM
cache_inode_status_t cache_inode_grant_block(void                  * pcookie,
                                             int                     cookie_size,
                                             cache_inode_status_t  * pstatus)
{
  cache_cookie_entry_t * p_cookie_entry;
  cache_lock_entry_t   * lock_entry;
  cache_entry_t        * pentry;

  if(cache_inode_find_block(pcookie,
                            cookie_size,
                            &p_cookie_entry,
                            pstatus) != CACHE_INODE_SUCCESS)
    {
      return *pstatus;
    }

  lock_entry = p_cookie_entry->lce_lock_entry;
  pentry     = p_cookie_entry->lce_pentry;

  P(pentry->object.file.lock_list_mutex);

  /* We need to make sure lock is only "granted" once...
   * It's (remotely) possible that due to latency, we might end up processing
   * two GRANTED_RSP calls at the same time.
   */
  if(lock_entry->cle_blocked == CACHE_GRANTING)
    {
      /* Handle the actual guts of releasing the blocked lock */
      grant_blocked_lock(p_cookie_entry->lce_pentry,
                         p_cookie_entry->lce_pcontext,
                         p_cookie_entry->lce_lock_entry);
    }

  V(pentry->object.file.lock_list_mutex);

  cookie_entry_dec_ref(p_cookie_entry);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}
#endif

static void grant_blocked_locks(cache_entry_t        * pentry,
                                fsal_op_context_t    * pcontext,
                                cache_inode_client_t * pclient)
{
  cache_lock_entry_t *found_entry;
  struct glist_head *glist, *glistn;
  cache_inode_status_t status;

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      if(found_entry->cle_blocked != CACHE_NLM_BLOCKING &&
         found_entry->cle_blocked != CACHE_NFSV4_BLOCKING)
          continue;

      /* Found a blocked entry for this file, see if we can place the lock. */
      if(get_overlapping_entry(pentry,
                               pcontext,
                               found_entry->cle_owner,
                               &found_entry->cle_lock) != NULL)
        continue;

      if(found_entry->cle_granted_callback != NULL)
        {
          /*
           * Mark the found_entry as granting and make the granted call back.
           * The granted call back is responsible for acquiring a reference to
           * the lock entry if needed.
           */
          found_entry->cle_blocked = CACHE_GRANTING;

          status = found_entry->cle_granted_callback(pentry,
                                                     pcontext,
                                                     found_entry,
                                                     pclient,
                                                     &status);

          if(status == CACHE_INODE_SUCCESS)
            continue;
        }

      /* There was no granted call back or it failed, remove lock from list */
      remove_from_locklist(pentry, pcontext, found_entry);
    }
}

void cancel_blocked_lock(cache_entry_t        * pentry,
                         fsal_op_context_t    * pcontext,
                         cache_lock_entry_t   * lock_entry)
{
#ifdef _USE_NLM
  cache_cookie_entry_t *pcookie = lock_entry->cle_blocked_cookie;
#endif

  /* Remove the lock from the lock list*/
  LogEntry("cancel_blocked_lock Removing", pentry, pcontext, lock_entry);
  remove_from_locklist(pentry, pcontext, lock_entry);
  
  /* Mark lock as granted and detach cookie and granted call back */
  lock_entry->cle_blocked          = CACHE_CANCELED;
  lock_entry->cle_blocked_cookie   = NULL;
  lock_entry->cle_granted_callback = NULL;

#ifdef _USE_NLM
  /* Don't need reference to cookie entry any more */
  if(pcookie != NULL)
    cookie_entry_dec_ref(pcookie);
#endif
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
                                cache_lock_owner_t   * powner,
                                cache_lock_desc_t    * plock,
                                cache_inode_client_t * pclient)
{
  struct glist_head *glist;
  cache_lock_entry_t *found_entry = NULL;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      /* Skip locks not owned by owner */
      if(powner != NULL && different_owners(found_entry->cle_owner, powner))
          continue;

      /* Skip granted locks */
      if(found_entry->cle_blocked == CACHE_NON_BLOCKING)
          continue;

      LogEntry("cancel_blocked_locks_range Checking",
               pentry, pcontext, found_entry);

      found_entry_end = lock_end(&found_entry->cle_lock);

      if((found_entry_end >= plock->cld_offset) &&
         (found_entry->cle_lock.cld_offset <= plock_end))
        {
          /* lock overlaps, cancel it. */
          cancel_blocked_lock(pentry, pcontext, found_entry);
        }
    }
}

#ifdef _USE_NLM
cache_inode_status_t cache_inode_release_block(void                 * pcookie,
                                               int                    cookie_size,
                                               cache_inode_status_t * pstatus,
                                               cache_inode_client_t * pclient)
{ 
  cache_cookie_entry_t * p_cookie_entry; 
  cache_lock_entry_t   * lock_entry;
  cache_entry_t        * pentry;
  fsal_op_context_t    * pcontext;
  cache_lock_owner_t   * powner;
  cache_lock_desc_t      lock;

  if(cache_inode_find_block(pcookie,
                            cookie_size,
                            &p_cookie_entry,
                            pstatus) != CACHE_INODE_SUCCESS)
    {
      return *pstatus;
    }

  lock_entry = p_cookie_entry->lce_lock_entry;
  pentry     = p_cookie_entry->lce_pentry;
  pcontext   = p_cookie_entry->lce_pcontext;

  P(pentry->object.file.lock_list_mutex);

  /* We need to make sure lock is only "granted" once...
   * It's (remotely) possible that due to latency, we might end up processing
   * two GRANTED_RSP calls at the same time.
   */
  if(lock_entry->cle_blocked == CACHE_GRANTING)
    {
      /* Get a reference to the lock owner and duplicate the lock */
      powner = lock_entry->cle_owner;
      get_lock_owner(powner);
      lock = lock_entry->cle_lock;
      
      /* Handle the actual guts of canceling the blocked lock */
      cancel_blocked_lock(pentry, pcontext, lock_entry);

      /* We had acquired an FSAL lock, need to release it. */
      *pstatus = FSAL_LockOp(pentry,
                             pcontext,
                             FSAL_OP_UNLOCK,
                             powner,
                             &lock,
                             NULL,   /* no conflict expected */
                             NULL);

      /* Release the lock owner reference */
      release_lock_owner(powner);

      /* Check to see if we can grant any blocked locks. */
      grant_blocked_locks(pentry, pcontext, pclient);
    }

  V(pentry->object.file.lock_list_mutex);

  cookie_entry_dec_ref(p_cookie_entry);

  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}
#endif

/******************************************************************************
 *
 * Functions to interract with FSAL
 *
 ******************************************************************************/
inline fsal_lock_t fsal_lock_type(cache_lock_desc_t *lock)
{
  switch(lock->cld_type)
    {
      case CACHE_INODE_LOCK_R:  return FSAL_LOCK_R;
      case CACHE_INODE_LOCK_W:  return FSAL_LOCK_W;
      case CACHE_INODE_NO_LOCK: return FSAL_NO_LOCK;
    }

  return FSAL_NO_LOCK;
}

inline cache_lock_t cache_inode_lock_type(fsal_lock_t type)
{
  switch(type)
    {
      case FSAL_LOCK_R:  return CACHE_INODE_LOCK_R;
      case FSAL_LOCK_W:  return CACHE_INODE_LOCK_W;
      case FSAL_NO_LOCK: return CACHE_INODE_NO_LOCK;
    }

  return CACHE_INODE_NO_LOCK;
}

inline const char *fsal_lock_op_str(fsal_lock_op_t op)
{
  switch(op)
    {
      case FSAL_OP_LOCKT:  return "FSAL_OP_LOCKT ";
      case FSAL_OP_LOCK:   return "FSAL_OP_LOCK  ";
      case FSAL_OP_UNLOCK: return "FSAL_OP_UNLOCK";
    }
  return "unknown";
}

cache_inode_status_t convert_fsal_lock_status(fsal_status_t fsal_status)
{
  cache_inode_status_t status;

  /* TODO FSF: may need more details on status, currently will not convert
   * lock failure right.
   *
   * CACHE_INODE_FSAL_DELAY: EAGAIN, EBUSY
   * CACHE_INODE_FSAL_EACCESS: EACCESS
   * need to add EDEADLK -> CACHE_INODE_LOCK_DEADLOCK
   * need to add ENOLCK
   */
  status = cache_inode_error_convert(fsal_status);
  if(status == CACHE_INODE_FSAL_DELAY || status == CACHE_INODE_FSAL_EACCESS)
    status = CACHE_INODE_LOCK_CONFLICT;
  return status;
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
cache_inode_status_t FSAL_unlock_no_owner(cache_entry_t        * pentry,
                                          fsal_op_context_t    * pcontext,
                                          cache_lock_desc_t    * plock)
{
  cache_lock_entry_t *unlock_entry;
  struct glist_head fsal_unlock_list;
  struct glist_head *glist, *glistn;
  cache_lock_entry_t *found_entry;
  fsal_status_t fsal_status;
  cache_inode_status_t status = CACHE_INODE_SUCCESS, t_status;
  fsal_lock_param_t lock_params;

  unlock_entry = create_cache_lock_entry(pentry,
                                         CACHE_NON_BLOCKING,
                                         &unknown_owner, /* no real owner */
                                         plock,
                                         NULL);

  if(unlock_entry == NULL)
    return CACHE_INODE_MALLOC_ERROR;

  init_glist(&fsal_unlock_list);

  unlock_entry->cle_ref_count = 1;
  glist_add_tail(&fsal_unlock_list, &unlock_entry->cle_list);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("cache_inode_unlock Generating FSAL Unlock List", pentry, pcontext, NULL, plock);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  status = subtract_list_from_list(pentry,
                                   pcontext,
                                   &fsal_unlock_list,
                                   &pentry->object.file.lock_list,
                                   &status);
  //TODO FSF: deal with return

  glist_for_each_safe(glist, glistn, &fsal_unlock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      LogUnlock(pentry, pcontext, found_entry);

      lock_params.lock_type = fsal_lock_type(plock);
      lock_params.lock_start = plock->cld_offset;
      lock_params.lock_length = plock->cld_length;

      fsal_status = FSAL_lock_op_no_owner(cache_inode_fd(pentry),
                                          &pentry->object.file.handle,
                                          pcontext,
                                          FSAL_OP_UNLOCK,
					  lock_params,
					  NULL);

      t_status = convert_fsal_lock_status(fsal_status);
      if(t_status != CACHE_INODE_SUCCESS)
        status = t_status;

      remove_from_locklist(pentry, pcontext, found_entry);
    }

  return status;
}

cache_inode_status_t FSAL_LockOp(cache_entry_t        * pentry,
                                 fsal_op_context_t    * pcontext,
                                 fsal_lock_op_t         lock_op,
                                 cache_lock_owner_t   * powner,
                                 cache_lock_desc_t    * plock,
                                 cache_lock_owner_t  ** holder,   /* owner that holds conflicting lock */
                                 cache_lock_desc_t    * conflict) /* description of conflicting lock */
{
  fsal_status_t fsal_status;
  cache_inode_status_t status = CACHE_INODE_SUCCESS;
  fsal_lock_param_t lock_params;
  fsal_lock_param_t conflicting_lock;

  LogLock(fsal_lock_op_str(lock_op), pentry, pcontext, powner, plock);
  LogFullDebug(COMPONENT_NLM,
               "Lock type %d", (int) fsal_lock_type(plock));

  switch(pentry->object.file.fsal_lock_support)
    {
      case FSAL_NO_LOCKS:
        return CACHE_INODE_SUCCESS;

      case FSAL_LOCKS_NO_OWNER:
        if(lock_op == FSAL_OP_UNLOCK)
          {
            status = FSAL_unlock_no_owner(pentry, pcontext, plock);
          }
        else
          {
	    lock_params.lock_type   = fsal_lock_type(plock);
	    lock_params.lock_start  = plock->cld_offset;
	    lock_params.lock_length = plock->cld_length;

            fsal_status = FSAL_lock_op_no_owner(cache_inode_fd(pentry),
                                                &pentry->object.file.handle,
                                                pcontext,
                                                lock_op,
						lock_params,
					       &conflicting_lock);
            status = convert_fsal_lock_status(fsal_status);
          }
        break;

      case FSAL_LOCKS_OWNER:

	lock_params.lock_type   = fsal_lock_type(plock);
	lock_params.lock_start  = plock->cld_offset;
	lock_params.lock_length = plock->cld_length;

        /* TODO FSF: need a better owner to pass, will depend on what FSAL is capable of */
        fsal_status = FSAL_lock_op_owner(cache_inode_fd(pentry),
                                         &pentry->object.file.handle,
                                         pcontext,
                                         &powner,
                                         sizeof(powner),
                                         lock_op,
					 lock_params,
					 &conflicting_lock);
        status = convert_fsal_lock_status(fsal_status);
        break;
    }

  if(status == CACHE_INODE_LOCK_CONFLICT)
    {
      if(holder != NULL)
        {
	  //conflicting_lock.lock_owner is the pid of the owner holding the lock
          *holder = &unknown_owner;
          get_lock_owner(&unknown_owner);
        }
      if(conflict != NULL)
        {
          memset(conflict, 0, sizeof(*conflict));
          conflict->cld_type   = cache_inode_lock_type(conflicting_lock.lock_type);
	  conflict->cld_offset = conflicting_lock.lock_start;
	  conflict->cld_length = conflicting_lock.lock_length;
        }
    }

  return status;
}

void copy_conflict(cache_lock_entry_t   * found_entry,
                   cache_lock_owner_t  ** holder,   /* owner that holds conflicting lock */
                   cache_lock_desc_t    * conflict) /* description of conflicting lock */
{
  if(found_entry == NULL)
    return;

  if(holder != NULL)
    {
      *holder = found_entry->cle_owner;
      get_lock_owner(found_entry->cle_owner);
    }
  if(conflict != NULL)
    *conflict = found_entry->cle_lock;
}

/******************************************************************************
 *
 * Primary lock interface functions
 *
 ******************************************************************************/
cache_inode_status_t cache_inode_test(cache_entry_t        * pentry,
                                      fsal_op_context_t    * pcontext,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * plock,
                                      cache_lock_owner_t  ** holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      cache_inode_status_t * pstatus)
{
  cache_lock_entry_t *found_entry;

  if(cache_inode_open(pentry, pclient, FSAL_O_RDWR, pcontext, pstatus) != CACHE_INODE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_test could not close file");
      return *pstatus;
    }

  P(pentry->object.file.lock_list_mutex);

  found_entry = get_overlapping_entry(pentry, pcontext, powner, plock);

  if(found_entry != NULL)
    {
      /* found a conflicting lock, return it */
      LogEntry("cache_inode_test found conflict",
               pentry, pcontext, found_entry);
      copy_conflict(found_entry, holder, conflict);
      *pstatus = CACHE_INODE_LOCK_CONFLICT;
    }
  else
    *pstatus = CACHE_INODE_SUCCESS;

  V(pentry->object.file.lock_list_mutex);

  return *pstatus;
}

cache_inode_status_t cache_inode_lock(cache_entry_t        * pentry,
                                      fsal_op_context_t    * pcontext,
                                      cache_lock_owner_t   * powner,
                                      cache_blocking_t       blocking,
                                      granted_callback_t     granted_callback,
                                      cache_lock_desc_t    * plock,
                                      cache_lock_owner_t  ** holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      cache_inode_status_t * pstatus)
{
  int allow = 1, overlap = 0;
  struct glist_head *glist;
  cache_lock_entry_t *found_entry;
  cache_blocking_t blocked = blocking;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  if(cache_inode_open(pentry, pclient, FSAL_O_RDWR, pcontext, pstatus) != CACHE_INODE_SUCCESS)
    {
      LogFullDebug(COMPONENT_NLM,
                   "cache_inode_lock could not close file");
      return *pstatus;
    }

  P(pentry->object.file.lock_list_mutex);
  if(blocking != CACHE_NON_BLOCKING)
    {
      /*
       * First search for a blocked request. Client can ignore the blocked
       * request and keep sending us new lock request again and again. So if
       * we have a mapping blocked request return that
       */
      glist_for_each(glist, &pentry->object.file.lock_list)
        {
          found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      
          if(different_owners(found_entry->cle_owner, powner))
            continue;
      
          if(found_entry->cle_blocked != blocking)
            continue;
      
          if(different_lock(&found_entry->cle_lock, plock))
            continue;
      
          /*
           * We have matched all atribute of the existing lock.
           * Just return with blocked status. Client may be polling.
           */
          V(pentry->object.file.lock_list_mutex);
          LogEntry("cache_inode_lock Found blocked",
                   pentry, pcontext, found_entry);
          *pstatus = CACHE_INODE_LOCK_BLOCKED;
          return *pstatus;
        }
    }

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      // TODO FSF: should we skip blocked locks?
      // TODO FSF: if we do, we could grant for example: Want W: 4-7, when Granted W: 0-3 Blocked W: 0-7
      //           we wouldn't want to skip CACHE_GRANTING -
      //           they are effectively granted.

      found_entry_end = lock_end(&found_entry->cle_lock);

      if((found_entry_end >= plock->cld_offset) &&
         (found_entry->cle_lock.cld_offset <= plock_end))
        {
          /* lock overlaps see if we can allow 
           * allow if neither lock is exclusive or the owner is the same
           */
          if((found_entry->cle_lock.cld_type == CACHE_INODE_LOCK_W ||
              plock->cld_type == CACHE_INODE_LOCK_W) &&
             different_owners(found_entry->cle_owner, powner))
            {
              /* found a conflicting lock, break out of loop */
              allow = 0;
              break;
            }
        }

      if(found_entry_end >= plock_end &&
         found_entry->cle_lock.cld_offset <= plock->cld_offset &&
         found_entry->cle_lock.cld_type == plock->cld_type &&
         (found_entry->cle_blocked == CACHE_NON_BLOCKING ||
          found_entry->cle_blocked == CACHE_GRANTING))
        {
          /* Found an entry that entirely overlaps the new entry 
           * (and due to the preceding test does not prevent
           * granting this lock - therefore there can't be any
           * other locks that would prevent granting this lock
           */
          if(!different_owners(found_entry->cle_owner, powner))
            {
              /* The lock actually has the same owner, we're done */
              if(found_entry->cle_blocked == CACHE_GRANTING)
                {
                  /* Need to handle completion of granting of this lock */
                  grant_blocked_lock(pentry,
                                     pcontext,
                                     found_entry);
                }
              V(pentry->object.file.lock_list_mutex);
              LogEntry("cache_inode_lock Found existing",
                       pentry, pcontext, found_entry);
              *pstatus = CACHE_INODE_SUCCESS;
              return *pstatus;
            }

          if(pentry->object.file.fsal_lock_support == FSAL_LOCKS_NO_OWNER)
            {
              /* Found a compatible lock with a different lock owner that
               * fully overlaps and FSAL supports locks but without owners.
               * We won't need to request an FSAL lock in this case.
               */
              LogEntry("cache_inode_lock Found overlapping",
                       pentry, pcontext, found_entry);
              overlap = 1;
            }
        }
    }

  if(allow)
    {
      blocked = CACHE_NON_BLOCKING;
    }
  else if(blocking == CACHE_NON_BLOCKING ||
          blocking == CACHE_NFSV4_BLOCKING) /* TODO FSF: look into support of NFS v4 blocking locks */
    {
      V(pentry->object.file.lock_list_mutex);
      LogEntry("cache_inode_lock conflicts with",
               pentry, pcontext, found_entry);
      copy_conflict(found_entry, holder, conflict);
      *pstatus = CACHE_INODE_LOCK_CONFLICT;
      return *pstatus;
    }

  /* We have already returned if:
   * + we have found an identical blocking lock
   * + we have found an entirely overlapping lock with the same lock owner
   * + this was not a blocking lock and we found a conflict
   *
   * So at this point, we are either going to:
   *   allow == 1 grant a lock           (blocked == CACHE_NON_BLOCKING)
   *   allow == 0 insert a blocking lock (blocked == blocking)
   */

  /* Create the new lock entry */
  found_entry = create_cache_lock_entry(pentry,
                                        blocked,
                                        powner,
                                        plock,
                                        granted_callback);
  if(!found_entry)
    {
      V(pentry->object.file.lock_list_mutex);
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }

  if(allow)
    {
      if(!overlap && pentry->object.file.fsal_lock_support != FSAL_NO_LOCKS)
        {
          /* Need to call down to the FSAL for this lock */
          *pstatus = FSAL_LockOp(pentry,
                                 pcontext,
                                 FSAL_OP_LOCK,
                                 powner,
                                 plock,
                                 holder,
                                 conflict);
          if(*pstatus != CACHE_INODE_SUCCESS)
            {
              lock_entry_inc_ref(found_entry);
              remove_from_locklist(pentry, pcontext, found_entry);
              V(pentry->object.file.lock_list_mutex);
              return *pstatus;
            }
        }

      /* Merge any touching or overlapping locks into this one */
      merge_lock_entry(pentry, pcontext, found_entry);
    }

  LogEntry("cache_inode_lock new entry",
           pentry, pcontext, found_entry);

  glist_add_tail(&pentry->object.file.lock_list, &found_entry->cle_list);

  V(pentry->object.file.lock_list_mutex);
  if(blocked == CACHE_NON_BLOCKING)
    *pstatus = CACHE_INODE_SUCCESS;
  else
    *pstatus = CACHE_INODE_LOCK_BLOCKED;
  return *pstatus;
}

cache_inode_status_t cache_inode_unlock(cache_entry_t        * pentry,
                                        fsal_op_context_t    * pcontext,
                                        cache_lock_owner_t   * powner,
                                        cache_lock_desc_t    * plock,
                                        cache_inode_client_t * pclient,
                                        cache_inode_status_t * pstatus)
{
  bool_t gotsome;

  /* We need to iterate over the full lock list and remove
   * any mapping entry. And cle_lock.cld_offset = 0 and cle_lock.cld_length = 0 nlm_lock
   * implies remove all entries
   */
  P(pentry->object.file.lock_list_mutex);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("cache_inode_unlock Subtracting", pentry, pcontext, powner, plock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  /* First cancel any blocking locks that might overlap the unlocked range. */
  cancel_blocked_locks_range(pentry,
                             pcontext,
                             powner,
                             plock,
                             pclient);

  /* Release the lock from cache inode lock list for pentry */
  gotsome = subtract_lock_from_list(pentry,
                                    pcontext,
                                    powner,
                                    plock,
                                    pstatus,
                                    &pentry->object.file.lock_list);

  if(*pstatus != CACHE_INODE_SUCCESS)
    {
      // TODO FSF: do we really want to exit here?
      V(pentry->object.file.lock_list_mutex);
      return *pstatus;
    }

  /* Unlocking the entire region will remove any FSAL locks we held, whether
   * from fully granted locks, or from blocking locks that were in the process
   * of being granted.
   */
  *pstatus = FSAL_LockOp(pentry,
                         pcontext,
                         FSAL_OP_UNLOCK,
                         powner,
                         plock,
                         NULL,   /* no conflict expected */
                         NULL);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("cache_inode_unlock Done", pentry, pcontext, powner, plock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  V(pentry->object.file.lock_list_mutex);

  grant_blocked_locks(pentry, pcontext, pclient);

  return *pstatus;
}

cache_inode_status_t cache_inode_cancel(cache_entry_t        * pentry,
                                        fsal_op_context_t    * pcontext,
                                        cache_lock_owner_t   * powner,
                                        cache_lock_desc_t    * plock,
                                        cache_inode_client_t * pclient,
                                        cache_inode_status_t * pstatus)
{
  struct glist_head *glist;
  cache_lock_entry_t *found_entry;

  *pstatus = CACHE_INODE_NOT_FOUND;

  P(pentry->object.file.lock_list_mutex);

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      if(different_owners(found_entry->cle_owner, powner))
        continue;

      if(found_entry->cle_blocked == CACHE_NON_BLOCKING)
        continue;

      if(different_lock(&found_entry->cle_lock, plock))
        continue;

      /*
       * We have matched all atribute of the existing lock.
       * Remove it (even if we were granting it).
       */
      LogEntry("cache_inode_lock cancelling blocked",
               pentry, pcontext, found_entry);
      cancel_blocked_lock(pentry, pcontext, found_entry);

      /* Unlocking the entire region will remove any FSAL locks we held, whether
       * from fully granted locks, or from blocking locks that were in the process
       * of being granted.
       */
      *pstatus = FSAL_LockOp(pentry,
                             pcontext,
                             FSAL_OP_UNLOCK,
                             powner,
                             plock,
                             NULL,   /* no conflict expected */
                             NULL);

      /* Check to see if we can grant any blocked locks. */
      grant_blocked_locks(pentry, pcontext, pclient);

      break;
    }

  V(pentry->object.file.lock_list_mutex);

  return *pstatus;
}

#ifdef _USE_NLM
cache_inode_status_t cache_inode_nlm_notify(fsal_op_context_t        * pcontext,
                                            cache_inode_nlm_client_t * pnlmclient,
                                            cache_inode_client_t     * pclient,
                                            cache_inode_status_t     * pstatus)
{
  cache_lock_owner_t   owner;
  cache_lock_entry_t * found_entry;
  cache_lock_desc_t    lock;
  cache_entry_t      * pentry;

  while(1)
    {
      P(pnlmclient->clc_mutex);

      /* We just need to find any file this client has locks on.
       * We pick the first lock the client holds, and use it's file.
       */
      found_entry = glist_first_entry(&pnlmclient->clc_lock_list, cache_lock_entry_t, cle_client_locks);
      lock_entry_inc_ref(found_entry);

      V(pnlmclient->clc_mutex);

      /* If we don't find any entries, then we are done. */
      if(found_entry == NULL)
        break;

      /* Extract the cache inode entry from the lock entry and release the lock entry */
      pentry = found_entry->cle_pentry;
      lock_entry_dec_ref(pentry, pcontext, found_entry);

      /* Make lock that covers the whole file - type doesn't matter for unlock */
      lock.cld_type   = CACHE_INODE_LOCK_R;
      lock.cld_offset = 0;
      lock.cld_length = 0;

      /* Make special NLM Owner that matches all owners from NLM Client */
      make_nlm_special_owner(pnlmclient, &owner);

      /* Remove all locks held by this NLM Client on the file */
      if(cache_inode_unlock(pentry,
                            pcontext,
                            &owner,
                            &lock,
                            pclient,
                            pstatus) != CACHE_INODE_SUCCESS)
        {
          /* TODO FSF: what do we do now? */
        }
    }
  return *pstatus;
}
#endif

cache_inode_status_t cache_inode_owner_unlock_all(fsal_op_context_t        * pcontext,
                                                  cache_lock_owner_t       * powner,
                                                  cache_inode_client_t     * pclient,
                                                  cache_inode_status_t     * pstatus)
{
  cache_lock_entry_t * found_entry;
  cache_lock_desc_t    lock;
  cache_entry_t      * pentry;

  while(1)
    {
      P(powner->clo_mutex);

      /* We just need to find any file this client has locks on.
       * We pick the first lock the client holds, and use it's file.
       */
      found_entry = glist_first_entry(&powner->clo_lock_list, cache_lock_entry_t, cle_owner_locks);
      lock_entry_inc_ref(found_entry);

      V(powner->clo_mutex);

      /* If we don't find any entries, then we are done. */
      if(found_entry == NULL)
        break;

      /* Extract the cache inode entry from the lock entry and release the lock entry */
      pentry = found_entry->cle_pentry;
      lock_entry_dec_ref(pentry, pcontext, found_entry);

      /* Make lock that covers the whole file - type doesn't matter for unlock */
      lock.cld_type   = CACHE_INODE_LOCK_R;
      lock.cld_offset = 0;
      lock.cld_length = 0;

      /* Remove all locks held by this owner on the file */
      if(cache_inode_unlock(pentry,
                            pcontext,
                            powner,
                            &lock,
                            pclient,
                            pstatus) != CACHE_INODE_SUCCESS)
        {
          /* TODO FSF: what do we do now? */
        }
    }
  return *pstatus;
}

#ifdef BUGAZOMEU
static void cache_inode_lock_print(cache_entry_t * pentry)
{

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!! Plein de chose a faire dans cache_inode_lock_print !!!!!!");
    return;
    if((pentry->internal_md.type == REGULAR_FILE) &&
       (piter_state->state_type == CACHE_INODE_STATE_SHARE))
            LogFullDebug(COMPONENT_CACHE_INODE,
                         "piter_lock=%p next=%p prev=%p offset=%llu length=%llu",
                         piter_state, piter_state->next, piter_state->prev,
                         piter_state->data.lock.offset, piter_state->data.lock.length);
}
#endif

/**
 *
 * cache_inode_lock_check_conflicting_range: checks for conflicts in lock ranges.
 *
 * Checks for conflicts in lock ranges.
 *
 * @param pentry    [INOUT] cache entry for which the lock is to be created
 * @param offset    [IN]    offset where the lock range start
 * @param length    [IN]    length for the lock range ( do not use CACHE_INODE_LOCK_OFFSET_EOF)
 * @param pfilelock [OUT]   pointer to the conflicting lock if a conflit is found, NULL if no conflict
 * @param pstatus   [OUT]   returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t
cache_inode_lock_check_conflicting_range(cache_entry_t * pentry,
                                         uint64_t offset,
                                         uint64_t length,
                                         nfs_lock_type4 lock_type,
                                         cache_inode_status_t *
                                         pstatus)
{
    if(pstatus == NULL)
        return CACHE_INODE_INVALID_ARGUMENT;

    /* pentry should be there */
    if(pentry == NULL)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    /* pentry should be a file */
    if(pentry->internal_md.type != REGULAR_FILE)
        {
            *pstatus = CACHE_INODE_BAD_TYPE;
            return *pstatus;
        }
    /*
     * CACHE_INODE_LOCK_OFFSET_EOF should have been handled
     * sooner, use "absolute" length
     */
    if(length == CACHE_INODE_LOCK_OFFSET_EOF)
        {
            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!! Plein de chose a faire dans cache_inode_lock_print !!!!!");
    *pstatus = CACHE_INODE_INVALID_ARGUMENT;
    return *pstatus;

#ifdef BUGAZOMEU
    for(piter_state = pentry->object.file.state_v4; piter_state != NULL;
        piter_state = piter_state->next)
        {
            if(piter_state->state_type != CACHE_INODE_STATE_LOCK)
                continue;

            LogFullDebug(COMPONENT_CACHE_INODE,
                         "--- check_conflicting_range : offset=%llu length=%llu "
                         "piter_state->offset=%llu piter_state->length=%llu "
                         "lock_type=%u piter_state->lock_type=%u",
                         offset, length, piter_state->data.lock.offset,
                         piter_state->data.lock.length,
                         lock_type, piter_state->data.lock.lock_type);

            /* Check for a "conflict on the left" */
            if((offset <= piter_state->data.lock.offset) &&
               ((offset + length) >= piter_state->data.lock.offset))
                found = TRUE;

            /* Check for a "conflict on the right" */
            if((offset >= piter_state->data.lock.offset) &&
               (offset <= (piter_state->data.lock.offset +
                           piter_state->data.lock.length)))
                found = TRUE;

            if(found)
                {
                    /* No conflict on read lock */
                    if((piter_state->data.lock.lock_type == CACHE_INODE_WRITE_LT) ||
                       (lock_type == CACHE_INODE_WRITE_LT))
                        {
                            *ppfilelock = piter_state;
                            *pstatus = CACHE_INODE_LOCK_CONFLICT;
                            return *pstatus;
                        }
                }

            /*
             * Locks are supposed to be ordered by their range.
             * Stop search is lock is "on the right"
             * of the right extremity of the requested lock
             */
            if((offset + length) < piter_state->data.lock.offset)
                break;
        }

    /* If this line is reached, then no conflict were found */
    *ppfilelock = NULL;
    *pstatus = CACHE_INODE_SUCCESS;
    return *pstatus;
#endif    /* BUGAZOMEU */
}

cache_inode_status_t
cache_inode_lock_test(cache_entry_t * pentry,
                      uint64_t offset,
                      uint64_t length,
                      nfs_lock_type4 lock_type,
                      cache_inode_client_t * pclient,
                      cache_inode_status_t * pstatus)
{
    /* stat */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_LOCKT);

    P_r(&pentry->lock);
    cache_inode_lock_check_conflicting_range(pentry, offset,
                                             length, lock_type,
                                             pstatus);
    V_r(&pentry->lock);

    if(*pstatus == CACHE_INODE_SUCCESS)
        inc_func_err_unrecover(pclient, CACHE_INODE_LOCKT);
    else
        inc_func_success(pclient, CACHE_INODE_LOCKT);
    return *pstatus;
}

/**
 *
 * cache_inode_lock_insert: insert a lock into the lock list.
 *
 * Inserts a lock into the lock list.
 *
 * @param pentry          [INOUT] cache entry for which the lock is to be created
 * @param pfilelock       [IN]    file lock to be inserted
 *
 * @return nothing (void function)
 *
 */

void cache_inode_lock_insert(cache_entry_t * pentry, cache_inode_state_t * pfilelock)
{

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!! Plein de chose a faire dans cache_inode_lock_insert !!!!!!");

    if(pentry == NULL)
        return;

    if(pentry->internal_md.type != REGULAR_FILE)
        return;

#ifdef BUGAZOMEU
    for(piter_state = pentry->object.file.state_v4; piter_state != NULL;
        piter_state_prev = piter_state, piter_state = piter_state->next)
        {
            if(pfilelock->data.lock.offset > piter_state->data.lock.offset)
                break;
        }

    /* At this line, piter_lock is the position before the new lock should be inserted */
    if(piter_state == NULL)
        {
            /* Lock is to be inserted at the end of list */
            pfilelock->next = NULL;
            pfilelock->prev = piter_state_prev;
            piter_state_prev->next = pfilelock;
        }
    else
        {
            pfilelock->next = piter_state->next;
            pfilelock->prev = piter_state;
            piter_state->next = pfilelock;
        }
#endif                          /* BUGAZOMEU */
}                               /* cache_inode_insert_lock */

/**
 *
 * cache_inode_lock_remove: removes a lock from the lock list.
 *
 * Remove a lock from the lock list.
 *
 * @param pfilelock       [IN]    file lock to be removed
 *
 * @return nothing (void function)
 *
 */
void cache_inode_lock_remove(cache_entry_t * pentry, cache_inode_client_t * pclient)
{

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!");
    return;
}                               /* cache_inode_lock_remove */

/**
 *
 * cache_inode_lock_create: creates a new lock for a given entry.
 *
 * Creates a new lock for a given entry.
 *
 * @param pentry          [INOUT] cache entry for which the lock is to be created
 * @param offset          [IN]    offset where the lock range start
 * @param length          [IN]    length for the lock range (0xFFFFFFFFFFFFFFFF means "until the end of file")
 * @param clientid        [IN]    The client id for the lock owner 
 * @param client_inst_num [IN]    The client instance for the lock owner 
 * @param pclient         [INOUT] ressource allocated by the client for the nfs management.
 * @param pstatus         [OUT]   returned status.
 *
 * @return the same as *pstatus
 *
 */

cache_inode_status_t cache_inode_lock_create(cache_entry_t * pentry,
                                             uint64_t offset,
                                             uint64_t length,
                                             nfs_lock_type4 lock_type,
                                             open_owner4 * pstate_owner,
                                             unsigned int client_inst_num,
                                             cache_inode_client_t * pclient,
                                             cache_inode_status_t * pstatus)
{
    if(pstatus == NULL)
        return CACHE_INODE_INVALID_ARGUMENT;

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    LogFullDebug(COMPONENT_CACHE_INODE,
                 "!!!!!!! Plein de chose a faire dans cache_inode_lock_remove !!!!!!!");
    return CACHE_INODE_INVALID_ARGUMENT;

#ifdef BUGAZOMEU
    /* stat */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_LOCK_CREATE);

    /* pentry should be there */
    if(pentry == NULL)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);

            *pstatus = CACHE_INODE_INVALID_ARGUMENT;
            return *pstatus;
        }

    /* pentry should be a file */
    if(pentry->internal_md.type != REGULAR_FILE)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);

            *pstatus = CACHE_INODE_BAD_TYPE;
            return *pstatus;
        }

    /* Use absolute offset, manage CACHE_INODE_LOCK_OFFSET_EOF here */
    if(length == CACHE_INODE_LOCK_OFFSET_EOF)
        {
            if(offset > pentry->object.file.attributes.filesize)
                {
                    /* stat */
                    inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
 
                    *pstatus = CACHE_INODE_INVALID_ARGUMENT;
                    return *pstatus;
                }
            abslength = pentry->object.file.attributes.filesize - offset;
        }
    else
        {
            abslength = length;
        }

    /* Lock the entry */
    P_w(&pentry->lock);

    /* Check if lock is conflicting with an existing one */
    cache_inode_lock_check_conflicting_range(pentry,
                                             offset,
                                             abslength,
                                             lock_type,
                                             ppfilelock, pstatus);
    if(*pstatus != CACHE_INODE_SUCCESS)
        {
            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
            V_w(&pentry->lock);

            return *pstatus;
        }

    /* Get a new lock */
    GetFromPool(pfilelock, &pclient->pool_state_v4, cache_inode_state_t);

    if(pfilelock == NULL)
        {
            LogDebug(COMPONENT_CACHE_INODE,
                     "Can't allocate a new file lock from cache pool");
            *pstatus = CACHE_INODE_MALLOC_ERROR;

            /* stat */
            inc_func_err_unrecover(pclient, CACHE_INODE_LOCK_CREATE);
            V_w(&pentry->lock);

            return *pstatus;
        }

    /* Fills in the lock structure */
    memset(pfilelock, 0, sizeof(cache_inode_state_v4_t));
    pfilelock->data.lock.offset = offset;
    pfilelock->data.lock.length = abslength;
    pfilelock->data.lock.lock_type = lock_type;
    pfilelock->state_type = CACHE_INODE_STATE_LOCK;
    pfilelock->clientid4 = clientid;
    pfilelock->client_inst_num = client_inst_num;
    pfilelock->seqid = 0;
    pfilelock->pentry = pentry;

    /* Insert the lock into the list */
    cache_inode_lock_insert(pentry, pfilelock);

    /* Successful operation */
    inc_func_success(pclient, CACHE_INODE_LOCK_CREATE);

    V_w(&pentry->lock);
    cache_inode_lock_print(pentry);

    *ppnewlock = pfilelock;
    *pstatus = CACHE_INODE_SUCCESS;
    return *pstatus;
#endif
}                               /* cache_inode_lock_create */
