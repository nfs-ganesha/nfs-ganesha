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

typedef struct cache_lock_entry_t
{
  struct glist_head           cle_list;
  struct glist_head           cle_all_locks;
  int                         cle_ref_count;
  cache_entry_t             * cle_pentry;
  cache_blocking_t            cle_blocked;
  cache_lock_owner_t        * cle_owner;
  cache_lock_desc_t           cle_lock;
  void                      * cle_pcookie;
  int                         cle_cookie_size;
  granted_callback_t          cle_granted_callback;
  pthread_mutex_t             cle_mutex;
} cache_lock_entry_t;

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
#endif

#if 0
/* nlm grace time tracking */
static struct timeval nlm_grace_tv;
#define NLM4_GRACE_PERIOD 10
/*
 * Time after which we should retry the granted
 * message request again
 */
#define NLM4_CLIENT_GRACE_PERIOD 3

const char *lock_result_str(int rc)
{
  switch(rc)
    {
      case NLM4_GRANTED:             return "NLM4_GRANTED";
      case NLM4_DENIED:              return "NLM4_DENIED";
      case NLM4_DENIED_NOLOCKS:      return "NLM4_DENIED_NOLOCKS";
      case NLM4_BLOCKED:             return "NLM4_BLOCKED";
      case NLM4_DENIED_GRACE_PERIOD: return "NLM4_DENIED_GRACE_PERIOD";
      case NLM4_DEADLCK:             return "NLM4_DEADLCK";
      case NLM4_ROFS:                return "NLM4_ROFS";
      case NLM4_STALE_FH:            return "NLM4_STALE_FH";
      case NLM4_FBIG:                return "NLM4_FBIG";
      case NLM4_FAILED:              return "NLM4_FAILED";
      default: return "Unknown";
    }
}
#endif

inline uint64_t lock_end(cache_lock_desc_t *plock)
{
  if(plock->cld_length == 0)
    return UINT64_MAX;
  else
    return plock->cld_offset + plock->cld_length - 1;
}

void DisplayOwner(cache_lock_owner_t *powner, char *buf)
{
  char *tmp = buf;

  switch(powner->clo_owner.clo_type)
    {
#ifdef _USE_NLM
      case CACHE_LOCK_OWNER_NLM:
        tmp += sprintf(buf, "CACHE_LOCK_OWNER_NLM: ");
        display_nlm_owner((cache_inode_nlm_owner_t *)powner, tmp);
        break;
#endif

      case CACHE_LOCK_OWNER_NFSV4:
        tmp += sprintf(buf, "CACHE_LOCK_OWNER_NFSV4: ");
        sprintf(buf, "undecoded");
        break;

      default:
        sprintf(buf, "unknown owner");
        break;
    }
}

const char *str_lockt(cache_lock_t ltype)
{
  switch(ltype)
    {
      case CACHE_INODE_LOCK_R: return "READ ";
      case CACHE_INODE_LOCK_W: return "WRITE";
      default:                 return "?????";
    }
}

const char *str_blocking(cache_blocking_t blocking)
{
  switch(blocking)
    {
      case CACHE_NON_BLOCKING:   return "NON_BLOCKING  ";
      case CACHE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case CACHE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      default:                   return "unknown       ";
    }
}

const char *str_blocked(cache_blocking_t blocked)
{
  switch(blocked)
    {
      case CACHE_NON_BLOCKING:   return "GRANTED       ";
      case CACHE_NLM_BLOCKING:   return "NLM_BLOCKING  ";
      case CACHE_NFSV4_BLOCKING: return "NFSV4_BLOCKING";
      default:                   return "unknown       ";
    }
}

void LogEntry(const char         *reason, 
              cache_entry_t      *pentry,
              fsal_op_context_t  *pcontext,
              cache_lock_entry_t *ple)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char owner[1024];
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

#if 0
void LogLock(const char *reason, struct nlm4_lock *nlm_lock)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char fh_buff[1024], oh_buff[1024];
      netobj_to_string(&nlm_lock->oh, oh_buff, 1023);
      netobj_to_string(&nlm_lock->fh, fh_buff, 1023);
      LogFullDebug(COMPONENT_NLM,
                   "%s Lock: caller=%s, fh=%s, oh=%s, svid=%d, start=0x%llx, end=0x%llx",
                   reason, nlm_lock->caller_name, fh_buff,
                   oh_buff, nlm_lock->svid,
                   (unsigned long long) nlm_lock->cle_lock.cld_offset,
                   (unsigned long long) lock_end(nlm_lock->cle_lock.cld_offset, nlm_lock->cle_lock.cld_length));
    }
}
#endif

#if 0
void LogArgs(const char *reason, struct nlm4_lockargs *arg)
{
  struct nlm4_lock *nlm_lock = &arg->alock;

  if(isFullDebug(COMPONENT_NLM))
    {
      char fh_buff[1024], oh_buff[1024];
      netobj_to_string(&nlm_lock->oh, oh_buff, 1023);
      netobj_to_string(&nlm_lock->fh, fh_buff, 1023);
      LogFullDebug(COMPONENT_NLM,
                   "%s Lock: caller=%s, fh=%s, oh=%s, svid=%d, start=0x%llx, end=0x%llx, exclusive=%d",
                   reason, nlm_lock->caller_name, fh_buff,
                   oh_buff, nlm_lock->svid,
                   (unsigned long long) nlm_lock->cle_lock.cld_offset,
                   (unsigned long long) lock_end(nlm_lock->cle_lock.cld_offset, nlm_lock->cle_lock.cld_length),
                   arg->exclusive);
    }
}
#endif

#if 0
void LogKernel(struct nlm4_lockargs *arg, int exclusive)
{

  if(isDebug(COMPONENT_NLM))
    {
      struct nlm4_lock *nlm_lock = &arg->alock;
      char *type;
      switch(exclusive)
        {
          case 0:  type = "F_RDLCK";
                   break;
          case 1:  type = "F_WRLCK";
                   break;
          case 2:  type = "F_UNLCK";
                   break;
          default: type = "UNKNOWN";
        }
      LogDebug(COMPONENT_NLM,
               "%s(%d) start=0x%llx, end=0x%llx",
               type, exclusive,
               (unsigned long long) nlm_lock->cle_lock.cld_offset,
               (unsigned long long) lock_end(nlm_lock->cle_lock.cld_offset, nlm_lock->cle_lock.cld_length));
    }
}
#endif

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
#endif

#if 0
void dump_all_locks(void)
{
#ifdef _DEBUG_MEMLEAKS
  struct glist_head *glist;
  cache_lock_entry_t *entry = NULL;
  int count = 0;

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
#else
  return;
#endif
}
#endif

static cache_lock_entry_t *create_cache_lock_entry(cache_entry_t      * pentry,
                                                   cache_blocking_t     blocked,
                                                   cache_lock_owner_t * powner,
                                                   cache_lock_desc_t  * plock,
                                                   void               * pcookie,
                                                   int                  cookie_size,
                                                   granted_callback_t   granted_callback)
{
  cache_lock_entry_t *new_entry;

  new_entry = (cache_lock_entry_t *) Mem_Alloc_Label(sizeof(cache_lock_entry_t),
                                                     "cache_lock_entry_t");
  if(!new_entry)
      return NULL;

  memset(new_entry, 0, sizeof(*new_entry));

  if(pcookie != NULL && cookie_size != 0)
    {
      new_entry->cle_pcookie = Mem_Alloc_Label(cookie_size, "lock_cookie");
      if(new_entry->cle_pcookie == NULL)
        {
          Mem_Free(new_entry);
          return NULL;
        }
      memcpy(new_entry->cle_pcookie, pcookie, cookie_size);
      new_entry->cle_cookie_size = cookie_size;
    }

  new_entry->cle_ref_count        = 0;
  new_entry->cle_pentry           = pentry;
  new_entry->cle_blocked          = blocked;
  new_entry->cle_owner            = powner;
  new_entry->cle_granted_callback = granted_callback;
  memcpy(&new_entry->cle_lock, plock, sizeof(new_entry->cle_lock));
  if(pthread_mutex_init(&new_entry->cle_mutex, NULL) == -1)
    {
      if(new_entry->cle_pcookie)
        Mem_Free(new_entry->cle_pcookie);
      Mem_Free(new_entry);
      return NULL;
    }

#ifdef _DEBUG_MEMLEAKS
    glist_add_tail(&cache_inode_all_locks, &new_entry->cle_all_locks);
#endif

  return new_entry;
}

void free_cache_lock_entry(cache_lock_entry_t *ple)
{
  glist_del(&ple->cle_list);
  if(ple->cle_pcookie != NULL)
    Mem_Free(ple->cle_pcookie);
  Mem_Free(ple);
}

#if 0
static inline cache_lock_entry_t *nlm4_lock_to_nlm_lock_entry(struct nlm4_lock *nlm_lock, netobj *cookie)
{
  return create_nlm_lock_entry(nlm_lock, cookie, 0);
}

static inline cache_lock_entry_t *nlm4_lockargs_to_nlm_lock_entry(struct nlm4_lockargs *args)
{
  return create_nlm_lock_entry(&args->alock, &args->cookie, args->exclusive);
}

void nlm_lock_entry_to_nlm_holder(cache_lock_entry_t * lock_entry,
                                  struct nlm4_holder *holder)
{
  /*
   * Take the lock to make sure other threads don't update
   * lock_entry contents in parallel
   */
  P(lock_entry->cle_mutex);
  holder->exclusive = lock_entry->arg.exclusive;
  holder->oh = lock_entry->arg.alock.oh;
  holder->svid = lock_entry->arg.alock.svid;
  holder->cle_lock.cld_offset = lock_entry->cle_lock.cld_offset;
  holder->cle_lock.cld_length = lock_entry->arg.alock.cle_lock.cld_length;
  V(lock_entry->cle_mutex);
}

int nlm_lock_entry_get_state(cache_lock_entry_t * lock_entry)
{
  int lck_state;
  P(lock_entry->cle_mutex);
  lck_state = lock_entry->state;
  V(lock_entry->cle_mutex);
  return lck_state;
}
#endif

#if 0
static void lock_entry_inc_ref(cache_entry_t      *pentry,
                               fsal_op_context_t  *pcontext,
                               cache_lock_entry_t *lock_entry)
{
    P(lock_entry->cle_mutex);
    lock_entry->cle_ref_count++;
    V(lock_entry->cle_mutex);
}
#endif

void lock_entry_dec_ref(cache_entry_t      *pentry,
                        fsal_op_context_t  *pcontext,
                        cache_lock_entry_t *lock_entry) 
{
  int to_free = 0;
  P(lock_entry->cle_mutex);
  lock_entry->cle_ref_count--;
  if(!lock_entry->cle_ref_count)
    {
      /*
       * We should already be removed from the lock_list
       * So we can free the lock_entry without any locking
       */
      to_free = 1;
    }
  V(lock_entry->cle_mutex);
  if(to_free)
    {
      LogEntry("nlm_lock_entry_dec_ref Freeing",
               pentry, pcontext, lock_entry);
#ifdef _DEBUG_MEMLEAKS
      glist_del(&lock_entry->cle_all_locks);
#endif

      if(lock_entry->cle_pcookie)
        Mem_Free(lock_entry->cle_pcookie);
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
  glist_del(&lock_entry->cle_list);
  lock_entry_dec_ref(pentry, pcontext, lock_entry);
}

/**
 * cache_copy_owner: copy a lock owner structure
 *
 * pdest->clo_owner.clo_type should be initialized with the type of owner expected
 * or CACHE_LOCK_OWNER_UNKNOWN if any owner type is acceptable.
 */
void cache_copy_owner(cache_lock_owner_t *pdest, cache_lock_owner_t *psrc)
{
  /* If the destination is not unknown type, don't copy if the type is not
   * as expected.
   */
  if(pdest->clo_owner.clo_type != CACHE_LOCK_OWNER_UNKNOWN &&
      pdest->clo_owner.clo_type != psrc->clo_owner.clo_type)
    pdest->clo_owner.clo_type = CACHE_LOCK_OWNER_UNKNOWN;
  else
    switch(psrc->clo_owner.clo_type)
      {
        case CACHE_LOCK_OWNER_UNKNOWN:
          pdest->clo_owner.clo_type = psrc->clo_owner.clo_type;
          break;

#ifdef _USE_NLM
        case CACHE_LOCK_OWNER_NLM:
          memcpy(pdest, psrc, sizeof(cache_inode_nlm_owner_t));
          break;
#endif

        case CACHE_LOCK_OWNER_NFSV4:
          memcpy(pdest, psrc, sizeof(cache_inode_open_owner_t));
          break;
        }
}

/* This is not complete, it doesn't check the owner's IP address...*/
static inline int different_owners(cache_lock_owner_t *powner1, cache_lock_owner_t *powner2)
{
  if(powner1->clo_owner.clo_type != powner2->clo_owner.clo_type)
    return 1;

  switch(powner1->clo_owner.clo_type)
    {
#ifdef _USE_NLM
      case CACHE_LOCK_OWNER_NLM:
        return compare_nlm_owner((cache_inode_nlm_owner_t *)powner1,
                                 (cache_inode_nlm_owner_t *)powner2);
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

#if 0
static cache_lock_entry_t *get_overlapping_entry(cache_entry_t        * pentry,
                                                 cache_lock_owner_t   * powner,
                                                 cache_lock_desc_t    * plock,
                                                 fsal_op_context_t    * pcontext)
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
      if(found_entry->cle_blocked != CACHE_NON_BLOCKING)
          continue;

      found_entry_end = lock_end(&found_entry->cle_lock);

      if((found_entry_end >= plock->cld_offset) &&
         (found_entry->cle_lock.cld_offset <= plock_end))
        {
          /* lock overlaps see if we can allow 
           * allow if neither lock is exclusive or the owner is the same
           */
          if((found_entry->cle_lock.cld_type == CACHE_INODE_LOCK_W ||
              plock->cld_type == CACHE_INODE_LOCK_W)
              /* TODO: do we need to check owner... certainly should for TEST
              &&
             different_owners(found_entry->cle_owner, powner) */
             )
            {
              /* found a conflicting lock, return it */
              return found_entry;
            }
        }
    }

  return NULL;
}
#endif

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

  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      check_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      if(different_owners(check_entry->cle_owner, lock_entry->cle_owner))
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

#if 0
cache_lock_entry_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock,
                                      int exclusive, int state)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist;
  P(pentry->object.file.lock_list_mutex);
  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      LogEntry("nlm_find_lock_entry Checking", nlm_entry);

      if(strcmp(nlm_entry->arg.alock.caller_name, nlm_lock->caller_name))
        continue;
      if(netobj_compare(&nlm_entry->arg.alock.oh, &nlm_lock->oh))
        continue;
      if(nlm_entry->arg.alock.svid != nlm_lock->svid)
        continue;
      if(state == NLM4_GRANTED)
        {
          /*
           * We don't check the below flag when looking for
           * lock in the lock list with state granted. Lookup
           * with state granted happens for unlock operation
           * and RFC says it should only match caller_name, fh,oh
           * and svid
           */
          break;
        }
      if(nlm_entry->cle_lock.cld_offset != nlm_lock->cle_lock.cld_offset)
        continue;
      if(nlm_entry->arg.alock.cle_lock.cld_length != nlm_lock->cle_lock.cld_length)
        continue;
      if(nlm_entry->arg.exclusive != exclusive)
        continue;
      if(nlm_entry->state != state)
        continue;
      /* We have matched all atribute of the nlm4_lock */
      break;
      }
  if(glist == &pentry->object.file.lock_list)
    nlm_entry = NULL;
  else
    {
      nlm_lock_entry_inc_ref(nlm_entry);
      LogEntry("nlm_find_lock_entry Found", nlm_entry);
    }
  V(pentry->object.file.lock_list_mutex);

  return nlm_entry;
}

static cache_lock_entry_t *cache_lock_entry_t_dup(cache_lock_entry_t * orig_nlm_entry)
{
  cache_lock_entry_t *nlm_entry;
  nlm_entry = (cache_lock_entry_t *) Mem_Calloc_Label(1, sizeof(cache_lock_entry_t),
                                                    "cache_lock_entry_t");
  if(!nlm_entry)
    return NULL;
  nlm_entry->arg.alock.caller_name = Str_Dup(orig_nlm_entry->arg.alock.caller_name);
  if(!copy_netobj(&nlm_entry->arg.alock.fh, &orig_nlm_entry->arg.alock.fh))
    goto err_out;
  if(!copy_netobj(&nlm_entry->arg.alock.oh, &orig_nlm_entry->arg.alock.oh))
    goto err_out;
  if(!copy_netobj(&nlm_entry->arg.cookie, &orig_nlm_entry->arg.cookie))
    goto err_out;
  nlm_entry->arg.alock.svid = orig_nlm_entry->arg.alock.svid;
  nlm_entry->cle_lock.cld_offset = orig_nlm_entry->cle_lock.cld_offset;
  nlm_entry->arg.alock.cle_lock.cld_length = orig_nlm_entry->arg.alock.cle_lock.cld_length;
  nlm_entry->state = orig_nlm_entry->state;
  nlm_entry->arg.exclusive = orig_nlm_entry->arg.exclusive;
  nlm_entry->cle_ref_count = 0;
  pthread_mutex_init(&nlm_entry->lock, NULL);
  nlm_entry->pentry = orig_nlm_entry->pentry;
  nlm_entry->pclient = orig_nlm_entry->pclient;
  nlm_entry->ht = orig_nlm_entry->ht;
  //cache_inode_pin_pentry(nlm_entry->pentry, nlm_entry->pclient, NULL);
#ifdef _DEBUG_MEMLEAKS
  glist_add_tail(&cache_inode_all_locks, &nlm_entry->cle_all_locks);
#endif
  return nlm_entry;
err_out:
  Mem_Free(nlm_entry->arg.alock.caller_name);
  netobj_free(&nlm_entry->arg.alock.fh);
  netobj_free(&nlm_entry->arg.alock.oh);
  netobj_free(&nlm_entry->arg.cookie);
  Mem_Free(nlm_entry);
  return NULL;
}

/* Subtract a lock from a lock entry, placing any remaining bits into the split list. */
static void subtract_lock_from_entry(cache_lock_entry_t *nlm_entry,
                                     struct nlm4_lock *nlm_lock,
                                     struct glist_head *split_list)
{
  uint64_t nlm_entry_end = lock_end(nlm_entry->cle_lock.cld_offset, nlm_entry->arg.alock.cle_lock.cld_length);
  uint64_t nlm_lock_end = lock_end(nlm_lock->cle_lock.cld_offset, nlm_lock->cle_lock.cld_length);
  cache_lock_entry_t *nlm_entry_left = NULL;
  cache_lock_entry_t *nlm_entry_right = NULL;

  if(nlm_lock_end < nlm_entry->cle_lock.cld_offset)
    /* nothing to split */
    return;

  if(nlm_entry_end < nlm_lock->cle_lock.cld_offset)
    /* nothing to split */
    return;

  if((nlm_lock->cle_lock.cld_offset <= nlm_entry->cle_lock.cld_offset) &&
     nlm_lock_end >= nlm_entry_end)
    {
      /* Fully overlap */
      LogEntry("subtract_lock_from_entry Remove Complete", nlm_entry);
      goto complete_remove;
    }

  LogEntry("subtract_lock_from_entry Split", nlm_entry);

  /* Delete the old entry and add one or two new entries */
  if(nlm_lock->cle_lock.cld_offset > nlm_entry->cle_lock.cld_offset)
    {
      nlm_entry_left = cache_lock_entry_t_dup(nlm_entry);
      /* FIXME handle error */
      if(nlm_entry_left)
        {
          nlm_entry_left->arg.alock.cle_lock.cld_length = nlm_lock->cle_lock.cld_offset - nlm_entry->cle_lock.cld_offset;
          LogEntry("subtract_lock_from_entry left split", nlm_entry_left);
          nlm_lock_entry_inc_ref(nlm_entry_left);
          glist_add_tail(split_list, &(nlm_entry_left->lock_list));
        }
    }

  if(nlm_lock_end < nlm_entry_end)
    {
      nlm_entry_right = cache_lock_entry_t_dup(nlm_entry);
      /* FIXME handle error */
      if(nlm_entry_right)
        {
          nlm_entry_right->cle_lock.cld_offset = nlm_lock_end + 1;
          nlm_entry_right->arg.alock.cle_lock.cld_length = nlm_entry_end - nlm_lock_end;
          LogEntry("subtract_lock_from_entry right split", nlm_entry_right);
          nlm_lock_entry_inc_ref(nlm_entry_right);
          glist_add_tail(split_list, &(nlm_entry_right->lock_list));
        }
    }

complete_remove:
  remove_from_locklist(nlm_entry);
}

/* Subtract a lock from a list of locks, possibly splitting entries in the list. */
void subtract_lock_from_list(struct nlm4_lock *nlm_lock,
                             struct glist_head *list,
                             int care_about_owner)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head split_lock_list;
  struct glist_head *glist, *glistn;

  init_glist(&split_lock_list);

  glist_for_each_safe(glist, glistn, list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      if(care_about_owner && different_owners(&nlm_entry->arg.alock, nlm_lock))
          continue;
      /*
       * We have matched all atribute of the nlm4_lock
       * Even though we are taking a reference to nlm_entry, we
       * don't inc the ref count because we want to drop the lock entry.
       */
      subtract_lock_from_entry(nlm_entry, nlm_lock, &split_lock_list);
    }

  /* now add the split lock list */
  glist_add_list_tail(list, &split_lock_list);
}

void subtract_list_from_list(struct glist_head *target,
                             struct glist_head *source,
                             struct nlm4_lock *nlm_lock)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, source)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      subtract_lock_from_list(&nlm_entry->arg.alock, target, 0);
    }
}

void send_kernel_unlock(struct glist_head *list)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      LogKernel(&nlm_entry->arg, 2);

      remove_from_locklist(nlm_entry);
    }
}

/* We need to iterate over the full lock list and remove
 * any mapping entry. And cle_lock.cld_offset = 0 and cle_lock.cld_length = 0 nlm_lock
 * implies remove all entries
 */
void nlm_delete_lock_entry(struct nlm4_lock *nlm_lock)
{
  cache_lock_entry_t *nlm_entry;

  P(pentry->object.file.lock_list_mutex);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("nlm_delete_lock_entry Subtracting", nlm_lock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  subtract_lock_from_list(nlm_lock, &pentry->object.file.lock_list, 1);

  nlm_entry = nlm4_lock_to_nlm_lock_entry(nlm_lock, NULL);
  if(nlm_entry != NULL)
    {
      struct glist_head kernel_unlock_list;

      init_glist(&kernel_unlock_list);
      nlm_entry->cle_ref_count += 1;
      glist_add_tail(&kernel_unlock_list, &nlm_entry->lock_list);
      LogFullDebug(COMPONENT_NLM,
                   "----------------------------------------------------------------------");
      LogLock("nlm_delete_lock_entry Generating Kernel Unlock List", nlm_lock);
      LogFullDebug(COMPONENT_NLM,
                   "----------------------------------------------------------------------");
      subtract_list_from_list(&kernel_unlock_list, &pentry->object.file.lock_list, nlm_lock);
      send_kernel_unlock(&kernel_unlock_list);
    }

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("nlm_delete_lock_entry Done", nlm_lock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  V(pentry->object.file.lock_list_mutex);
}

cache_lock_entry_t *nlm_find_lock_entry_by_cookie(netobj * cookie)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist;
  char buffer[1024];

  netobj_to_string(cookie, buffer, 1024);
  LogFullDebug(COMPONENT_NLM,
               "nlm_find_lock_entry_by_cookie searching for %s", buffer);
  P(pentry->object.file.lock_list_mutex);
  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      netobj_to_string(&nlm_entry->arg.cookie, buffer, 1024);
      LogFullDebug(COMPONENT_NLM,
                   "nlm_find_lock_entry_by_cookie checking %s", buffer);
      if(!netobj_compare(&nlm_entry->arg.cookie, cookie))
          break;
    }
  if(glist == &pentry->object.file.lock_list)
    nlm_entry = NULL;
  else
    nlm_lock_entry_inc_ref(nlm_entry);
  V(pentry->object.file.lock_list_mutex);
  return nlm_entry;
}

int start_nlm_grace_period(void)
{
  return gettimeofday(&nlm_grace_tv, NULL);
}

int in_nlm_grace_period(void)
{
  struct timeval tv;
  if(nlm_grace_tv.tv_sec == 0)
    return 0;

  if(gettimeofday(&tv, NULL) == 0)
    {
      if(tv.tv_sec < (nlm_grace_tv.tv_sec + NLM4_GRACE_PERIOD))
        {
          return 1;
        }
      else
        {
          nlm_grace_tv.tv_sec = 0;
          return 0;
        }
    }
  return 0;
}

void nlm_init(void)
{
  nlm_async_callback_init();
  nlm_init_locklist();
  nsm_unmonitor_all();
  start_nlm_grace_period();
}

void nlm_node_recovery(char *name,
                       fsal_op_context_t * pcontext,
                       cache_inode_client_t * pclient, hash_table_t * ht)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;

  LogDebug(COMPONENT_NLM, "Recovery for host %s", name);

  P(pentry->object.file.lock_list_mutex);
  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      if(strcmp(nlm_entry->arg.alock.caller_name, name))
        continue;

      /*
       * inc ref so that we can remove entry from the list
       * and still use the lock entry values
       */
      nlm_lock_entry_inc_ref(nlm_entry);

      /*
       * now remove the from locklist
       */
      remove_from_locklist(nlm_entry);

      /*
       * We don't inc ref count because we want to drop the lock entry
       */
      if(nlm_entry->state == NLM4_GRANTED)
        {
          /*
           * Submit the async request to send granted response for
           * locks that can be granted
           */
          nlm_grant_blocked_locks(&nlm_entry->arg.alock.fh);
        }
      nlm_lock_entry_dec_ref(nlm_entry);
    }
  V(pentry->object.file.lock_list_mutex);
}

int nlm_monitor_host(char *name)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist;

  P(pentry->object.file.lock_list_mutex);
  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      if(!strcmp(nlm_entry->arg.alock.caller_name, name))
        {
           /* there is already a lock with the same
            * caller_name. So we should already be monitoring
            * the host
            */
           V(pentry->object.file.lock_list_mutex);
           return 0;
        }
    }
  V(pentry->object.file.lock_list_mutex);
  /* There is nobody monitoring the host */
  LogFullDebug(COMPONENT_NLM, "Monitoring host %s", name);
  return nsm_monitor(name);
}

int nlm_unmonitor_host(char *name)
{
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist;

  P(pentry->object.file.lock_list_mutex);
  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      if(!strcmp(nlm_entry->arg.alock.caller_name, name))
        {
          /* We have locks tracking the same caller_name
           * we cannot unmonitor the host now. We will do
           * it for the last unlock from the host
           */
          V(pentry->object.file.lock_list_mutex);
          return 0;
        }
    }
  V(pentry->object.file.lock_list_mutex);
  /* There is nobody holding a lock with host */
  LogFullDebug(COMPONENT_NLM, "Unmonitoring host %s", name);
  return nsm_unmonitor(name);
}

static void nlm4_send_grant_msg(void *arg)
{
    int retval;
  struct nlm4_testargs inarg;
  cache_lock_entry_t *nlm_entry = (cache_lock_entry_t *) arg;
  char buffer[1024];

  /* If we fail allocation the best is to delete the block entry
   * so that client can try again and get the lock. May be
   * by then we are able to allocate objects
   */
  if(!copy_netobj(&inarg.alock.fh, &nlm_entry->arg.alock.fh))
    {
      goto free_nlm_lock_entry;
    }
  if(!copy_netobj(&inarg.alock.oh, &nlm_entry->arg.alock.oh))
    {
      netobj_free(&inarg.alock.fh);
      goto free_nlm_lock_entry;
    }

  if(!copy_netobj(&inarg.cookie, &nlm_entry->arg.cookie))
    {
      netobj_free(&inarg.alock.oh);
      netobj_free(&inarg.alock.fh);
      goto free_nlm_lock_entry;
    }
  inarg.alock.caller_name = Str_Dup(nlm_entry->arg.alock.caller_name);
  if(!inarg.alock.caller_name)
    {
      netobj_free(&inarg.cookie);
      netobj_free(&inarg.alock.oh);
      netobj_free(&inarg.alock.fh);
      goto free_nlm_lock_entry;
    }
  inarg.exclusive = nlm_entry->arg.exclusive;
  inarg.alock.svid = nlm_entry->arg.alock.svid;
  inarg.alock.cle_lock.cld_offset = nlm_entry->cle_lock.cld_offset;
  inarg.alock.cle_lock.cld_length = nlm_entry->arg.alock.cle_lock.cld_length;
  netobj_to_string(&inarg.cookie, buffer, 1024);
  LogDebug(COMPONENT_NLM,
           "nlm4_send_grant_msg Sending GRANTED for %p svid=%d start=%llx len=%llx cookie=%s",
           nlm_entry, nlm_entry->arg.alock.svid,
           (unsigned long long) nlm_entry->cle_lock.cld_offset, (unsigned long long) nlm_entry->arg.alock.cle_lock.cld_length,
           buffer);

  retval = nlm_send_async(NLMPROC4_GRANTED_MSG, nlm_entry->arg.alock.caller_name, &inarg, nlm_entry);
  Mem_Free(inarg.alock.caller_name);
  netobj_free(&inarg.alock.fh);
  netobj_free(&inarg.alock.oh);
  netobj_free(&inarg.cookie);

  /* If success, we are done. */
  if(retval == RPC_SUCCESS)
    return;

  /*
   * We are not able call granted callback. Some client may retry
   * the lock again. So remove the existing blocked nlm entry
   */
  LogMajor(COMPONENT_NLM,
           "%s: GRANTED_MSG RPC call failed with return code %d. Removing the blocking lock",
           __func__, retval);

free_nlm_lock_entry:
  P(pentry->object.file.lock_list_mutex);
  remove_from_locklist(pentry, pcontext, nlm_entry);
  V(pentry->object.file.lock_list_mutex);
  /*
   * Submit the async request to send granted response for
   * locks that can be granted, because of this removal
   * from the lock list. If the client is lucky. It
   * will send the lock request again and before the
   * block locks are granted it gets the lock.
   */
  nlm_grant_blocked_locks(&nlm_entry->arg.alock.fh);
  nlm_lock_entry_dec_ref(nlm_entry);
  return;
}

static void do_nlm_grant_blocked_locks(void *arg)
{
  netobj *fh;
  struct nlm4_lock nlm_lock;
  cache_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;
  cache_lock_entry_t *nlm_entry_overlap;

  fh = (netobj *) arg;
  P(pentry->object.file.lock_list_mutex);
  glist_for_each_safe(glist, glistn, &pentry->object.file.lock_list)
    {
      nlm_entry = glist_entry(glist, cache_lock_entry_t, cle_list);
      if(netobj_compare(&nlm_entry->arg.alock.fh, fh))
          continue;
      if(nlm_entry->state != NLM4_BLOCKED)
          continue;
      /*
       * found a blocked entry for this file handle
       * See if we can place the lock
       */
      /* dummy nlm4_lock */
      if(!copy_netobj(&nlm_lock.fh, &nlm_entry->arg.alock.fh))
        {
          /* If we fail the best is to delete the block entry
           * so that client can try again and get the lock. May be
           * by then we are able to allocate objects
           */
          remove_from_locklist(nlm_entry);
          continue;
        }
      nlm_lock.cle_lock.cld_offset = nlm_entry->cle_lock.cld_offset;
      nlm_lock.cle_lock.cld_length = nlm_entry->arg.alock.cle_lock.cld_length;
      nlm_entry_overlap = get_overlapping_entry(&nlm_lock, nlm_entry->arg.exclusive);
      netobj_free(&nlm_lock.fh);
      if(nlm_entry_overlap)
        {
          nlm_lock_entry_dec_ref(nlm_entry_overlap);
          continue;
        }

      P(nlm_entry->cle_mutex);
      /*
       * Mark the nlm_entry as granted and send a grant msg rpc
       * Some os only support grant msg rpc
       */

      nlm_entry->state = NLM4_GRANTED;
      nlm_entry->cle_ref_count++;
      V(nlm_entry->cle_mutex);
      /*
       * We don't want to send the granted_msg rpc holding
       * pentry->object.file.lock_list_mutex. That will prevent other lock operation
       * at the server. We have incremented nlm_entry cle_ref_count.
       */
      nlm_async_callback(nlm4_send_grant_msg, (void *)nlm_entry);
    }
  V(pentry->object.file.lock_list_mutex);
  netobj_free(fh);
  Mem_Free(fh);
}

void nlm_grant_blocked_locks(netobj * orig_fh)
{
  netobj *fh;
  fh = (netobj *) Mem_Alloc(sizeof(netobj));
  if(copy_netobj(fh, orig_fh) != NULL)
    {
      /*
       * We don't want to block the unlock request to wait
       * for us to grant lock to other host. So create an async
       * task
       */
      nlm_async_callback(do_nlm_grant_blocked_locks, (void *)fh);
    }
}

/*
 * called when server get a response from client
 * saying the grant message is denied due to grace period
 */
void nlm_resend_grant_msg(void *arg)
{
  /*
   * We should wait for client grace period
   */
  sleep(NLM4_CLIENT_GRACE_PERIOD);

  nlm4_send_grant_msg(arg);
}
#endif

inline fsal_lock_t fsal_lock_type(cache_lock_desc_t *lock)
{

  fsal_lock_t lt;

  switch(lock->cld_type)
    {
      case CACHE_INODE_LOCK_R: lt = FSAL_LOCK_R;
      case CACHE_INODE_LOCK_W: lt = FSAL_LOCK_W;
    }

  return lt;
}

inline cache_lock_t cache_inode_lock_type(fsal_lock_t type)
{
  cache_lock_t lt;

  switch(type)
    {
      case FSAL_LOCK_R: lt = CACHE_INODE_LOCK_R;
      case FSAL_LOCK_W: lt = CACHE_INODE_LOCK_W;
    }

  return lt;
}

cache_inode_status_t KernelLockOp(cache_entry_t        * pentry,
                                  fsal_op_context_t    * pcontext,
                                  cache_lock_owner_t   * powner,
                                  fsal_lock_op_t         lock_op,
                                  cache_lock_desc_t    * lock,
                                  cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                                  cache_lock_desc_t    * conflict) /* description of conflicting lock */
{
  fsal_status_t fsal_status;
  cache_inode_status_t status;

  switch(pentry->object.file.fsal_lock_support)
    {
      case FSAL_NO_LOCKS:
        return CACHE_INODE_SUCCESS;

      case FSAL_LOCKS_NO_OWNER:
        fsal_status = FSAL_lock_op_no_owner(cache_inode_fd(pentry),
                                            &pentry->object.file.handle,
                                            pcontext,
                                            lock_op,
                                            fsal_lock_type(lock),
                                            lock->cld_offset,
                                            lock->cld_length);
        break;

      case FSAL_LOCKS_OWNER:
        /* TODO: need a better owner to pass, will depend on what FSAL is capable of */
        fsal_status = FSAL_lock_op_owner(cache_inode_fd(pentry),
                                         &pentry->object.file.handle,
                                         pcontext,
                                         &powner,
                                         sizeof(cache_lock_owner_t *),
                                         lock_op,
                                         fsal_lock_type(lock),
                                         lock->cld_offset,
                                         lock->cld_length);
        break;
    }

  /* TODO: may need more details on status, currently will not convert
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
  if(status == CACHE_INODE_LOCK_CONFLICT)
    {
      if(holder != NULL)
        holder->clo_owner.clo_type = CACHE_LOCK_OWNER_UNKNOWN;
      if(conflict != NULL)
        {
          memset(conflict, 0, sizeof(*conflict));
        }
    }
  return status;
}

void copy_conflict(cache_lock_entry_t   * found_entry,
                   cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                   cache_lock_desc_t    * conflict) /* description of conflicting lock */
{
  if(found_entry == NULL)
    return;

  if(holder != NULL)
    cache_copy_owner(holder, found_entry->cle_owner);
  if(conflict != NULL)
    *conflict = found_entry->cle_lock;
}

cache_inode_status_t cache_inode_test(cache_entry_t        * pentry,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * plock,
                                      cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t    * pcontext,
                                      cache_inode_status_t * pstatus)
{
  struct glist_head *glist;
  cache_lock_entry_t *found_entry;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  *pstatus = CACHE_INODE_SUCCESS;

  P(pentry->object.file.lock_list_mutex);

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      LogEntry("get_overlapping_entry Checking",
               pentry, pcontext, found_entry);

      /* Skip blocked locks */
      if(found_entry->cle_blocked != CACHE_NON_BLOCKING)
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
              LogEntry("cache_inode_test found conflict",
                       pentry, pcontext, found_entry);
              copy_conflict(found_entry, holder, conflict);
              *pstatus = CACHE_INODE_LOCK_CONFLICT;
              break;
            }
        }
    }

  V(pentry->object.file.lock_list_mutex);

  return *pstatus;
}

cache_inode_status_t cache_inode_lock(cache_entry_t        * pentry,
                                      void                 * pcookie,
                                      int                    cookie_size,
                                      cache_blocking_t       blocking,
                                      granted_callback_t     granted_callback,
                                      bool_t                 reclaim,
                                      cache_lock_owner_t   * powner,
                                      cache_lock_desc_t    * plock,
                                      cache_lock_owner_t   * holder,   /* owner that holds conflicting lock */
                                      cache_lock_desc_t    * conflict, /* description of conflicting lock */
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t    * pcontext,
                                      cache_inode_status_t * pstatus)
{
  int allow = 1, overlap = 0;
  struct glist_head *glist;
  cache_lock_entry_t *found_entry;
  cache_blocking_t blocked = blocking;
  uint64_t found_entry_end, plock_end = lock_end(plock);

  P(pentry->object.file.lock_list_mutex);
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

      if(found_entry->cle_blocked == CACHE_NON_BLOCKING)
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

  glist_for_each(glist, &pentry->object.file.lock_list)
    {
      found_entry = glist_entry(glist, cache_lock_entry_t, cle_list);

      // TODO: should we skip blocked locks?

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
         found_entry->cle_blocked == CACHE_NON_BLOCKING)
        {
          /* Found an entry that entirely overlaps the new entry 
           * (and due to the preceding test does not prevent
           * granting this lock - therefore there can't be any
           * other locks that would prevent granting this lock
           */
          if(!different_owners(found_entry->cle_owner, powner))
            {
              /* The lock actually has the same owner, we're done */
              V(pentry->object.file.lock_list_mutex);
              LogEntry("cache_inode_lock Found existing",
                       pentry, pcontext, found_entry);
              *pstatus = CACHE_INODE_SUCCESS;
              return *pstatus;
            }

          
          if(pentry->object.file.fsal_lock_support == FSAL_LOCKS_NO_OWNER)
            {
              /* Found a compatoible lock with a different lock owner that
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
          blocking == CACHE_NFSV4_BLOCKING) /* TODO: look into support of NFS v4 blocking locks */
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
                                        pcookie,
                                        cookie_size,
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
          *pstatus = KernelLockOp(pentry,
                                  pcontext,
                                  powner,
                                  FSAL_OP_LOCK,
                                  plock,
                                  holder,
                                  conflict);
          if(*pstatus != CACHE_INODE_SUCCESS)
            {
              free_cache_lock_entry(found_entry);
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
                                        void                 * pcookie,
                                        int                    cookie_size,
                                        cache_lock_owner_t   * powner,
                                        cache_lock_desc_t    * plock,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
                                        cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
  return *pstatus;
}

cache_inode_status_t cache_inode_cancel(cache_entry_t        * pentry,
                                        cache_lock_owner_t   * powner,
                                        void                 * pcookie,
                                        int                    cookie_size,
                                        cache_lock_desc_t    * plock,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
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
       * Just return with blocked status. Client may be polling.
       */
      LogEntry("cache_inode_lock cancelling blocked",
               pentry, pcontext, found_entry);
      remove_from_locklist(pentry, pcontext, found_entry);
      break;
    }

  V(pentry->object.file.lock_list_mutex);

  return *pstatus;
}

cache_inode_status_t cache_inode_notify(cache_entry_t        * pentry,
                                        cache_lock_owner_t   * powner,
                                        cache_inode_client_t * pclient,
                                        fsal_op_context_t    * pcontext,
                                        cache_inode_status_t * pstatus)
{
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
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
