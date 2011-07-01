/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 * * --------------------------
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include <sys/time.h>
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"
#include "nsm.h"
#include "nlm_async.h"

/*
 * nlm_lock_entry_t locking rule:
 * The value is always updated/read with nlm_lock_entry->lock held
 * If we have nlm_lock_list mutex held we can read it safely, because the
 * value is always updated while walking the list with nlm_lock_list_mutex held.
 * The updation happens as below:
 *  pthread_mutex_lock(nlm_lock_list_mutex)
 *  pthread_mutex_lock(nlm_entry->lock)
 *    update the nlm_entry value
 *  ........
 * The value is ref counted with nlm_lock_entry->ref_count so that a
 * parallel cancel/unlock won't endup freeing the datastructure. The last
 * release on the data structure ensure that it is freed.
 */
static struct glist_head nlm_lock_list;
#ifdef _DEBUG_MEMLEAKS
static struct glist_head nlm_all_locks;
#endif
static pthread_mutex_t nlm_lock_list_mutex;

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

inline uint64_t lock_end(uint64_t start, uint64_t len)
{
  if(len == 0)
    return UINT64_MAX;
  else
    return start + len - 1;
}

void LogEntry(const char *reason, nlm_lock_entry_t *entry)
{
  struct nlm4_lockargs *arg = &entry->arg;
  struct nlm4_lock *nlm_lock = &arg->alock;

  if(isFullDebug(COMPONENT_NLM))
    {
      char fh_buff[1024], oh_buff[1024];
      netobj_to_string(&nlm_lock->oh, oh_buff, 1023);
      netobj_to_string(&nlm_lock->fh, fh_buff, 1023);
      LogFullDebug(COMPONENT_NLM,
                   "%s Entry: %p caller=%s, fh=%s, oh=%s, svid=%d, start=0x%llx, end=0x%llx, exclusive=%d, state=%s, ref_count=%d",
                   reason, entry, nlm_lock->caller_name, fh_buff,
                   oh_buff, nlm_lock->svid,
                   (unsigned long long) nlm_lock->l_offset,
                   (unsigned long long) lock_end(nlm_lock->l_offset, nlm_lock->l_len),
                   arg->exclusive, lock_result_str(entry->state), entry->ref_count);
    }
}

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
                   (unsigned long long) nlm_lock->l_offset,
                   (unsigned long long) lock_end(nlm_lock->l_offset, nlm_lock->l_len));
    }
}

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
                   (unsigned long long) nlm_lock->l_offset,
                   (unsigned long long) lock_end(nlm_lock->l_offset, nlm_lock->l_len),
                   arg->exclusive);
    }
}

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
               (unsigned long long) nlm_lock->l_offset,
               (unsigned long long) lock_end(nlm_lock->l_offset, nlm_lock->l_len));
    }
}

void dump_entry(nlm_lock_entry_t *entry)
{
  char fh_buff[1024], oh_buff[1024];
  struct nlm4_lockargs *arg = &entry->arg;
  struct nlm4_lock *nlm_lock = &arg->alock;

  netobj_to_string(&nlm_lock->oh, oh_buff, 1023);
  netobj_to_string(&nlm_lock->fh, fh_buff, 1023);
  LogTest("Entry: %p caller=%s, fh=%s, oh=%s, svid=%d, start=0x%llx, end=0x%llx, exclusive=%d, state=%s, ref_count=%d",
          entry, nlm_lock->caller_name, fh_buff,
          oh_buff, nlm_lock->svid,
          (unsigned long long) nlm_lock->l_offset,
          (unsigned long long) lock_end(nlm_lock->l_offset, nlm_lock->l_len),
          arg->exclusive, lock_result_str(entry->state), entry->ref_count);
}

void dump_lock_list(void)
{
  struct glist_head *glist;
  nlm_lock_entry_t *entry = NULL;
  int count = 0;

  LogTest("\nLOCK LIST");
  glist_for_each(glist, &nlm_lock_list)
    {
      entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

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
  nlm_lock_entry_t *entry = NULL;
  int count = 0;

  LogTest("\nALL LOCKS");
  glist_for_each(glist, &nlm_all_locks)
    {
      entry = glist_entry(glist, nlm_lock_entry_t, all_locks);

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

void fill_netobj(netobj * dst, char *data, int len)
{
  dst->n_len   = 0;
  dst->n_bytes = NULL;
  if(len != 0)
    {
      dst->n_bytes = (char *)Mem_Alloc(len);
      if(dst->n_bytes != NULL)
        {
          dst->n_len = len;
          memcpy(dst->n_bytes, data, len);
        }
    }
}

netobj *copy_netobj(netobj * dst, netobj * src)
{
    if(dst == NULL)
      return NULL;
    dst->n_len = 0;
    if(src->n_len != 0)
      {
        dst->n_bytes = (char *)Mem_Alloc(src->n_len);
        if(!dst->n_bytes)
          return NULL;
        memcpy(dst->n_bytes, src->n_bytes, src->n_len);
      }
    else
      dst->n_bytes = NULL;

    dst->n_len = src->n_len;
    return dst;
}

void netobj_free(netobj * obj)
{
    if(obj->n_bytes)
        Mem_Free(obj->n_bytes);
}

static int netobj_compare(netobj * obj1, netobj * obj2)
{
    if(obj1->n_len != obj2->n_len)
        return 1;
    if(memcmp(obj1->n_bytes, obj2->n_bytes, obj1->n_len))
        return 1;
    return 0;
}

void netobj_to_string(netobj *obj, char *buffer, int maxlen)
{
  int left = maxlen, pos = 0;
  char *buf = buffer;
  buffer[0] = '\0';
  if (left < 10)
    return;
  buf += sprintf(buf, "%08x:", obj->n_len);
  left -= 9;
  while(left > 2 && pos < obj->n_len)
    {
      buf += sprintf(buf, "%02x", obj->n_bytes[pos]);
      left -= 2;
      pos++;
    }
}

static nlm_lock_entry_t *create_nlm_lock_entry(struct nlm4_lock *nlm_lock, netobj *cookie, int exclusive)
{
    nlm_lock_entry_t *nlm_entry;
    netobj empty_cookie;
    char *data = "";

    nlm_entry = (nlm_lock_entry_t *) Mem_Calloc_Label(1, sizeof(nlm_lock_entry_t),
                                                      "nlm_lock_entry_t");
    if(!nlm_entry)
        return NULL;

    nlm_entry->arg.alock.caller_name = Str_Dup(nlm_lock->caller_name);

    if(!copy_netobj(&nlm_entry->arg.alock.fh, &nlm_lock->fh))
        goto err_out;

    if(!copy_netobj(&nlm_entry->arg.alock.oh, &nlm_lock->oh))
        goto err_out;

    if(cookie == NULL)
      {
        cookie = &empty_cookie;
        empty_cookie.n_len = 0;
        empty_cookie.n_bytes = data;
      }

    if(!copy_netobj(&nlm_entry->arg.cookie, cookie))
      goto err_out;

    nlm_entry->arg.alock.svid = nlm_lock->svid;
    nlm_entry->arg.alock.l_offset = nlm_lock->l_offset;
    nlm_entry->arg.alock.l_len = nlm_lock->l_len;
    nlm_entry->arg.exclusive = exclusive;
    nlm_entry->ref_count = 0;
    pthread_mutex_init(&nlm_entry->lock, NULL);
#ifdef _DEBUG_MEMLEAKS
    glist_add_tail(&nlm_all_locks, &nlm_entry->all_locks);
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

static inline nlm_lock_entry_t *nlm4_lock_to_nlm_lock_entry(struct nlm4_lock *nlm_lock, netobj *cookie)
{
  return create_nlm_lock_entry(nlm_lock, cookie, 0);
}

static inline nlm_lock_entry_t *nlm4_lockargs_to_nlm_lock_entry(struct nlm4_lockargs *args)
{
  return create_nlm_lock_entry(&args->alock, &args->cookie, args->exclusive);
}

void nlm_lock_entry_to_nlm_holder(nlm_lock_entry_t * nlm_entry,
                                  struct nlm4_holder *holder)
{
    /*
     * Take the lock to make sure other threads don't update
     * nlm_entry contents in parallel
     */
    pthread_mutex_lock(&nlm_entry->lock);
    holder->exclusive = nlm_entry->arg.exclusive;
    holder->oh = nlm_entry->arg.alock.oh;
    holder->svid = nlm_entry->arg.alock.svid;
    holder->l_offset = nlm_entry->arg.alock.l_offset;
    holder->l_len = nlm_entry->arg.alock.l_len;
    pthread_mutex_unlock(&nlm_entry->lock);
}

int nlm_lock_entry_get_state(nlm_lock_entry_t * nlm_entry)
{
    int lck_state;
    pthread_mutex_lock(&nlm_entry->lock);
    lck_state = nlm_entry->state;
    pthread_mutex_unlock(&nlm_entry->lock);
    return lck_state;
}

static void nlm_lock_entry_inc_ref(nlm_lock_entry_t * nlm_entry)
{
    pthread_mutex_lock(&nlm_entry->lock);
    nlm_entry->ref_count++;
    pthread_mutex_unlock(&nlm_entry->lock);
}

void nlm_lock_entry_dec_ref(nlm_lock_entry_t *nlm_entry) 
{
    int to_free = 0;
    pthread_mutex_lock(&nlm_entry->lock);
    nlm_entry->ref_count--;
    if(!nlm_entry->ref_count)
        {
            /*
             * We should already be removed from the lock_list
             * So we can free the lock_entry without any locking
             */
            to_free = 1;
        }
    pthread_mutex_unlock(&nlm_entry->lock);
    if(to_free)
        {
            LogEntry("nlm_lock_entry_dec_ref Freeing", nlm_entry);
#ifdef _DEBUG_MEMLEAKS
            glist_del(&nlm_entry->all_locks);
#endif

	    /** @todo : Use SAL to manage state */
            //cache_inode_unpin_pentry(nlm_entry->pentry, nlm_entry->pclient, nlm_entry->ht);
            Mem_Free(nlm_entry->arg.alock.caller_name);
            netobj_free(&nlm_entry->arg.alock.fh);
            netobj_free(&nlm_entry->arg.alock.oh);
            netobj_free(&nlm_entry->arg.cookie);
            Mem_Free(nlm_entry);
        }
}

static void do_nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry)
{
    /*
     * If some other thread is holding a reference to this nlm_lock_entry
     * don't free the structure. But drop from the lock list
     */
    glist_del(&nlm_entry->lock_list);
    nlm_lock_entry_dec_ref(nlm_entry);
}

void nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry)
{
    pthread_mutex_lock(&nlm_lock_list_mutex);
    do_nlm_remove_from_locklist(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
}

static inline int different_files(struct nlm4_lock *lock1, struct nlm4_lock *lock2)
{
  return netobj_compare(&lock1->fh, &lock2->fh);
}

/* This is not complete, it doesn't check the owner's IP address...*/
static inline int different_owners(struct nlm4_lock *lock1, struct nlm4_lock *lock2)
{
  if(lock1->svid != lock2->svid)
    return 1;
  if(netobj_compare(&lock1->oh, &lock2->oh))
    return 1;
  return strcmp(lock1->caller_name, lock2->caller_name);
}

static inline int different_lock(struct nlm4_lockargs *lock1, struct nlm4_lockargs *lock2)
{
  return (lock1->alock.l_offset != lock2->alock.l_offset) ||
         (lock1->alock.l_len    != lock2->alock.l_len) ||
         (lock1->exclusive      != lock2->exclusive);
}

static nlm_lock_entry_t *get_nlm_overlapping_entry(struct nlm4_lock *nlm_lock,
                                                   int exclusive)
{
    int overlap = 0;
    struct glist_head *glist;
    nlm_lock_entry_t *nlm_entry = NULL;
    uint64_t nlm_entry_end, nlm_lock_end;

    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            LogEntry("get_nlm_overlapping_entry Checking", nlm_entry);

            if(different_files(&nlm_entry->arg.alock, nlm_lock))
                continue;

            if(nlm_entry->state != NLM4_GRANTED)
                continue;

            if(nlm_entry->arg.alock.l_len)
                nlm_entry_end = nlm_entry->arg.alock.l_offset + nlm_entry->arg.alock.l_len;
            else
                nlm_entry_end = UINT64_MAX;

            if(nlm_lock->l_len)
                nlm_lock_end = nlm_lock->l_offset + nlm_lock->l_len;
            else
                nlm_lock_end = UINT64_MAX;

            if((nlm_entry_end > nlm_lock->l_offset) &&
               (nlm_lock_end > nlm_entry->arg.alock.l_offset))
                {
                    /* lock overlaps see if we can allow */
                    if(nlm_entry->arg.exclusive || exclusive)
                        {
                            overlap = 1;
                            break;
                        }
                }
        }

    if(!overlap)
        return NULL;

    nlm_lock_entry_inc_ref(nlm_entry);
    return nlm_entry;
}

nlm_lock_entry_t *nlm_overlapping_entry(struct nlm4_lock * nlm_lock, int exclusive)
{
    nlm_lock_entry_t *nlm_entry;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    nlm_entry = get_nlm_overlapping_entry(nlm_lock, exclusive);
    pthread_mutex_unlock(&nlm_lock_list_mutex);

    return nlm_entry;
}

/* We need to iterate over the full lock list and remove
 * any mapping entry. And l_offset = 0 and l_len = 0 nlm_lock
 * implies remove all entries
 */
static void nlm_merge_lock_entry(struct nlm4_lock *nlm_lock)
{
    nlm_lock_entry_t *nlm_entry;
    uint64_t nlm_entry_end;
    uint64_t nlm_lock_end;
    struct glist_head *glist, *glistn;

    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(different_files(&nlm_entry->arg.alock, nlm_lock))
                continue;

            if(different_owners(&nlm_entry->arg.alock, nlm_lock))
                continue;

            nlm_entry_end = lock_end(nlm_entry->arg.alock.l_offset, nlm_entry->arg.alock.l_len);
            nlm_lock_end = lock_end(nlm_lock->l_offset, nlm_lock->l_len);

            if((nlm_lock_end + 1) < nlm_entry->arg.alock.l_offset)
                /* nothing to merge */
                continue;

            if((nlm_entry_end + 1) < nlm_lock->l_offset)
                /* nothing to merge */
                continue;

            /* nlm_entry touches or overlaps nlm_lock, expand nlm_lock */
            if(nlm_lock_end < nlm_entry_end)
                /* Expand end of nlm_lock */
                nlm_lock_end = nlm_entry_end;

            if(nlm_entry->arg.alock.l_offset < nlm_lock->l_offset)
                /* Expand start of nlm_lock */
                nlm_entry->arg.alock.l_offset = nlm_lock->l_offset;

            /* Compute new lock length */
            nlm_lock->l_len = nlm_lock_end - nlm_entry->arg.alock.l_offset + 1;

            /* Remove merged entry */
            LogEntry("nlm_merge_lock_entry Merging", nlm_entry);
            do_nlm_remove_from_locklist(nlm_entry);
        }
}

nlm_lock_entry_t *nlm_add_to_locklist(struct nlm4_lockargs * arg,
                cache_entry_t * pentry, cache_inode_client_t * pclient,
                fsal_op_context_t * pcontext)
{
    int allow = 1, overlap = 0;
    int exclusive = arg->exclusive;
    struct glist_head *glist;
    struct nlm4_lock *nlm_lock = &arg->alock;
    nlm_lock_entry_t *nlm_entry;
    uint64_t nlm_entry_end, nlm_lock_end = lock_end(nlm_lock->l_offset, nlm_lock->l_len);
#ifdef PIN_CACHE_ENTRIES
    cache_inode_status_t pstatus;
#endif

    pthread_mutex_lock(&nlm_lock_list_mutex);
    /*
     * First search for a blocked request. Client can ignore the blocked
     * request and keep sending us new lock request again and again. So if
     * we have a mapping blocked request return that
     */
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(different_files(&nlm_entry->arg.alock, nlm_lock))
                continue;

            if(different_owners(&nlm_entry->arg.alock, nlm_lock))
                continue;

            if(nlm_entry->state != NLM4_BLOCKED)
                continue;

            if(different_lock(&nlm_entry->arg, arg))
                continue;
            /*
             * We have matched all atribute of the nlm4_lock.
             * Just return the nlm_entry with ref count inc
             */
            nlm_lock_entry_inc_ref(nlm_entry);
            pthread_mutex_unlock(&nlm_lock_list_mutex);
            LogEntry("nlm_add_to_locklist Found blocked", nlm_entry);
            return nlm_entry;
        }

    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(different_files(&nlm_entry->arg.alock, nlm_lock))
                continue;

            // TODO: should we skip blocked locks?

            nlm_entry_end = lock_end(nlm_entry->arg.alock.l_offset, nlm_entry->arg.alock.l_len);

            if((nlm_entry_end >= nlm_lock->l_offset) &&
               (nlm_entry->arg.alock.l_offset <= nlm_lock_end))
                {
                    /* lock overlaps see if we can allow 
                     * allow if neither lock is exclusive or the owner is the same
                     */
                    if((nlm_entry->arg.exclusive || exclusive) &&
                       different_owners(&nlm_entry->arg.alock, nlm_lock))
                        {
                            allow = 0;
                            break;
                        }
                }
            if(nlm_entry_end >= nlm_lock_end &&
               nlm_entry->arg.alock.l_offset <= nlm_lock->l_offset &&
               nlm_entry->arg.exclusive == exclusive &&
               nlm_entry->state != NLM4_BLOCKED)
                {
                    /* Found an entry that entirely overlaps the new entry 
                     * (and due to the preceding test does not prevent
                     * granting this lock - therefore there can't be any
                     * other locks that would prevent granting this lock
                     */
                    if(!different_owners(&nlm_entry->arg.alock, nlm_lock))
                        {
                            /* The lock actually has the same owner, we're done */
                            nlm_lock_entry_inc_ref(nlm_entry);
                            pthread_mutex_unlock(&nlm_lock_list_mutex);
                            LogEntry("nlm_add_to_locklist Found existing", nlm_entry);
                            return nlm_entry;
                        }

                    LogEntry("nlm_add_to_locklist Found overlapping", nlm_entry);
                    overlap = 1;
                }
        }

    if(allow)
        {
            if(!overlap)
                {
                    /* Need to call down to the FSAL for this lock */
                    LogKernel(arg, arg->exclusive);
                }
            /* Merge any touching or overlapping locks into this one */
            nlm_merge_lock_entry(nlm_lock);
        }

    nlm_entry = nlm4_lockargs_to_nlm_lock_entry(arg);
    if(!nlm_entry)
        goto error_out;

#ifdef PIN_CACHE_ENTRIES
    /* Pin the cache entry */
    pstatus = cache_inode_pin_pentry(pentry, pclient, pcontext);
    if(pstatus != CACHE_INODE_SUCCESS)
        goto free_nlm_entry;
#endif

    /* Store pentry and pclient for using during removal */
    nlm_entry->pentry = pentry;
    nlm_entry->pclient = pclient;

    /*
     * Add nlm_entry to the lock list with
     * granted or blocked state. Since we haven't yet added
     * nlm_lock_entry to the lock list, no other threads can
     * find this lock entry. So no need to take the lock.
     */
    if(allow)
        nlm_entry->state = NLM4_GRANTED;
    else
        nlm_entry->state = NLM4_BLOCKED;
 
    LogEntry("nlm_add_to_locklist new entry", nlm_entry);

    /*
     * +1 for being on the list
     * +1 for the refcount returned
     */
    nlm_entry->ref_count += 2;
    glist_add_tail(&nlm_lock_list, &nlm_entry->lock_list);

error_out:
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    return nlm_entry;

#ifdef PIN_CACHE_ENTRIES
free_nlm_entry:
    nlm_lock_entry_dec_ref(nlm_entry);
    nlm_entry = NULL;
    goto error_out;
#endif
}

void nlm_init_locklist(void)
{
    init_glist(&nlm_lock_list);
#ifdef _DEBUG_MEMLEAKS
    init_glist(&nlm_all_locks);
#endif
    pthread_mutex_init(&nlm_lock_list_mutex, NULL);
}

nlm_lock_entry_t *nlm_find_lock_entry(struct nlm4_lock *nlm_lock,
                                      int exclusive, int state)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            LogEntry("nlm_find_lock_entry Checking", nlm_entry);

            if(strcmp(nlm_entry->arg.alock.caller_name, nlm_lock->caller_name))
                continue;
            if(different_files(&nlm_entry->arg.alock, nlm_lock))
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
            if(nlm_entry->arg.alock.l_offset != nlm_lock->l_offset)
                continue;
            if(nlm_entry->arg.alock.l_len != nlm_lock->l_len)
                continue;
            if(nlm_entry->arg.exclusive != exclusive)
                continue;
            if(nlm_entry->state != state)
                continue;
            /* We have matched all atribute of the nlm4_lock */
            break;
        }
    if(glist == &nlm_lock_list)
        nlm_entry = NULL;
    else
      {
        nlm_lock_entry_inc_ref(nlm_entry);
        LogEntry("nlm_find_lock_entry Found", nlm_entry);
      }
    pthread_mutex_unlock(&nlm_lock_list_mutex);

    return nlm_entry;
}

static nlm_lock_entry_t *nlm_lock_entry_t_dup(nlm_lock_entry_t * orig_nlm_entry)
{
    nlm_lock_entry_t *nlm_entry;
    nlm_entry = (nlm_lock_entry_t *) Mem_Calloc_Label(1, sizeof(nlm_lock_entry_t),
                                                      "nlm_lock_entry_t");
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
    nlm_entry->arg.alock.l_offset = orig_nlm_entry->arg.alock.l_offset;
    nlm_entry->arg.alock.l_len = orig_nlm_entry->arg.alock.l_len;
    nlm_entry->state = orig_nlm_entry->state;
    nlm_entry->arg.exclusive = orig_nlm_entry->arg.exclusive;
    nlm_entry->ref_count = 0;
    pthread_mutex_init(&nlm_entry->lock, NULL);
    nlm_entry->pentry = orig_nlm_entry->pentry;
    nlm_entry->pclient = orig_nlm_entry->pclient;
    nlm_entry->ht = orig_nlm_entry->ht;
    //cache_inode_pin_pentry(nlm_entry->pentry, nlm_entry->pclient, NULL);
#ifdef _DEBUG_MEMLEAKS
    glist_add_tail(&nlm_all_locks, &nlm_entry->all_locks);
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
static void subtract_lock_from_entry(nlm_lock_entry_t *nlm_entry,
                                     struct nlm4_lock *nlm_lock,
                                     struct glist_head *split_list)
{
    uint64_t nlm_entry_end = lock_end(nlm_entry->arg.alock.l_offset, nlm_entry->arg.alock.l_len);
    uint64_t nlm_lock_end = lock_end(nlm_lock->l_offset, nlm_lock->l_len);
    nlm_lock_entry_t *nlm_entry_left = NULL;
    nlm_lock_entry_t *nlm_entry_right = NULL;

    if(nlm_lock_end < nlm_entry->arg.alock.l_offset)
        /* nothing to split */
        return;

    if(nlm_entry_end < nlm_lock->l_offset)
        /* nothing to split */
        return;

    if((nlm_lock->l_offset <= nlm_entry->arg.alock.l_offset) &&
       nlm_lock_end >= nlm_entry_end)
      {
        /* Fully overlap */
        LogEntry("subtract_lock_from_entry Remove Complete", nlm_entry);
        goto complete_remove;
      }

    LogEntry("subtract_lock_from_entry Split", nlm_entry);

    /* Delete the old entry and add one or two new entries */
    if(nlm_lock->l_offset > nlm_entry->arg.alock.l_offset)
        {
            nlm_entry_left = nlm_lock_entry_t_dup(nlm_entry);
            /* FIXME handle error */
            if(nlm_entry_left)
              {
                nlm_entry_left->arg.alock.l_len = nlm_lock->l_offset - nlm_entry->arg.alock.l_offset;
                LogEntry("subtract_lock_from_entry left split", nlm_entry_left);
                nlm_lock_entry_inc_ref(nlm_entry_left);
                glist_add_tail(split_list, &(nlm_entry_left->lock_list));
              }
        }

    if(nlm_lock_end < nlm_entry_end)
        {
            nlm_entry_right = nlm_lock_entry_t_dup(nlm_entry);
            /* FIXME handle error */
            if(nlm_entry_right)
              {
                nlm_entry_right->arg.alock.l_offset = nlm_lock_end + 1;
                nlm_entry_right->arg.alock.l_len = nlm_entry_end - nlm_lock_end;
                LogEntry("subtract_lock_from_entry right split", nlm_entry_right);
                nlm_lock_entry_inc_ref(nlm_entry_right);
                glist_add_tail(split_list, &(nlm_entry_right->lock_list));
              }
        }

complete_remove:
    do_nlm_remove_from_locklist(nlm_entry);
}

/* Subtract a lock from a list of locks, possibly splitting entries in the list. */
void subtract_lock_from_list(struct nlm4_lock *nlm_lock,
                             struct glist_head *list,
                             int care_about_owner)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head split_lock_list;
    struct glist_head *glist, *glistn;

    init_glist(&split_lock_list);

    glist_for_each_safe(glist, glistn, list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

            if(different_files(&nlm_entry->arg.alock, nlm_lock))
                continue;

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
  nlm_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, source)
      {
          nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

          if(different_files(&nlm_entry->arg.alock, nlm_lock))
              continue;

          subtract_lock_from_list(&nlm_entry->arg.alock, target, 0);
      }
}

void send_kernel_unlock(struct glist_head *list)
{
  nlm_lock_entry_t *nlm_entry;
  struct glist_head *glist, *glistn;

  glist_for_each_safe(glist, glistn, list)
      {
          nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);

          LogKernel(&nlm_entry->arg, 2);

          do_nlm_remove_from_locklist(nlm_entry);
      }
}

/* We need to iterate over the full lock list and remove
 * any mapping entry. And l_offset = 0 and l_len = 0 nlm_lock
 * implies remove all entries
 */
void nlm_delete_lock_entry(struct nlm4_lock *nlm_lock)
{
  nlm_lock_entry_t *nlm_entry;

  pthread_mutex_lock(&nlm_lock_list_mutex);

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("nlm_delete_lock_entry Subtracting", nlm_lock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  subtract_lock_from_list(nlm_lock, &nlm_lock_list, 1);

  nlm_entry = nlm4_lock_to_nlm_lock_entry(nlm_lock, NULL);
  if(nlm_entry != NULL)
    {
      struct glist_head kernel_unlock_list;

      init_glist(&kernel_unlock_list);
      nlm_entry->ref_count += 1;
      glist_add_tail(&kernel_unlock_list, &nlm_entry->lock_list);
      LogFullDebug(COMPONENT_NLM,
                   "----------------------------------------------------------------------");
      LogLock("nlm_delete_lock_entry Generating Kernel Unlock List", nlm_lock);
      LogFullDebug(COMPONENT_NLM,
                   "----------------------------------------------------------------------");
      subtract_list_from_list(&kernel_unlock_list, &nlm_lock_list, nlm_lock);
      send_kernel_unlock(&kernel_unlock_list);
    }

  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");
  LogLock("nlm_delete_lock_entry Done", nlm_lock);
  LogFullDebug(COMPONENT_NLM,
               "----------------------------------------------------------------------");

  pthread_mutex_unlock(&nlm_lock_list_mutex);
}

nlm_lock_entry_t *nlm_find_lock_entry_by_cookie(netobj * cookie)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;
    char buffer[1024];

    netobj_to_string(cookie, buffer, 1024);
    LogFullDebug(COMPONENT_NLM,
                 "nlm_find_lock_entry_by_cookie searching for %s", buffer);
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            netobj_to_string(&nlm_entry->arg.cookie, buffer, 1024);
            LogFullDebug(COMPONENT_NLM,
                         "nlm_find_lock_entry_by_cookie checking %s", buffer);
            if(!netobj_compare(&nlm_entry->arg.cookie, cookie))
                break;
        }
    if(glist == &nlm_lock_list)
        nlm_entry = NULL;
    else
        nlm_lock_entry_inc_ref(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
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
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist, *glistn;

    LogDebug(COMPONENT_NLM, "Recovery for host %s", name);

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
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
            do_nlm_remove_from_locklist(nlm_entry);

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
    pthread_mutex_unlock(&nlm_lock_list_mutex);
}

int nlm_monitor_host(char *name)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(!strcmp(nlm_entry->arg.alock.caller_name, name))
                {
                    /* there is already a lock with the same
                     * caller_name. So we should already be monitoring
                     * the host
                     */
                    pthread_mutex_unlock(&nlm_lock_list_mutex);
                    return 0;
                }
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    /* There is nobody monitoring the host */
    LogFullDebug(COMPONENT_NLM, "Monitoring host %s", name);
    return nsm_monitor(name);
}

int nlm_unmonitor_host(char *name)
{
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist;

    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each(glist, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
            if(!strcmp(nlm_entry->arg.alock.caller_name, name))
                {
                    /* We have locks tracking the same caller_name
                     * we cannot unmonitor the host now. We will do
                     * it for the last unlock from the host
                     */
                    pthread_mutex_unlock(&nlm_lock_list_mutex);
                    return 0;
                }
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
    /* There is nobody holding a lock with host */
    LogFullDebug(COMPONENT_NLM, "Unmonitoring host %s", name);
    return nsm_unmonitor(name);
}

static void nlm4_send_grant_msg(void *arg)
{
    int retval;
    struct nlm4_testargs inarg;
    nlm_lock_entry_t *nlm_entry = (nlm_lock_entry_t *) arg;
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
    inarg.alock.l_offset = nlm_entry->arg.alock.l_offset;
    inarg.alock.l_len = nlm_entry->arg.alock.l_len;
    netobj_to_string(&inarg.cookie, buffer, 1024);
    LogDebug(COMPONENT_NLM,
             "nlm4_send_grant_msg Sending GRANTED for %p svid=%d start=%llx len=%llx cookie=%s",
             nlm_entry, nlm_entry->arg.alock.svid,
             (unsigned long long) nlm_entry->arg.alock.l_offset, (unsigned long long) nlm_entry->arg.alock.l_len,
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
    nlm_remove_from_locklist(nlm_entry);
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
    nlm_lock_entry_t *nlm_entry;
    struct glist_head *glist, *glistn;
    nlm_lock_entry_t *nlm_entry_overlap;

    fh = (netobj *) arg;
    pthread_mutex_lock(&nlm_lock_list_mutex);
    glist_for_each_safe(glist, glistn, &nlm_lock_list)
        {
            nlm_entry = glist_entry(glist, nlm_lock_entry_t, lock_list);
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
                    do_nlm_remove_from_locklist(nlm_entry);
                    continue;
                }
            nlm_lock.l_offset = nlm_entry->arg.alock.l_offset;
            nlm_lock.l_len = nlm_entry->arg.alock.l_len;
            nlm_entry_overlap = get_nlm_overlapping_entry(&nlm_lock, nlm_entry->arg.exclusive);
            netobj_free(&nlm_lock.fh);
            if(nlm_entry_overlap)
                {
                    nlm_lock_entry_dec_ref(nlm_entry_overlap);
                    continue;
                }

            pthread_mutex_lock(&nlm_entry->lock);
            /*
             * Mark the nlm_entry as granted and send a grant msg rpc
             * Some os only support grant msg rpc
             */

            nlm_entry->state = NLM4_GRANTED;
            nlm_entry->ref_count++;
            pthread_mutex_unlock(&nlm_entry->lock);
            /*
             * We don't want to send the granted_msg rpc holding
             * nlm_lock_list_mutex. That will prevent other lock operation
             * at the server. We have incremented nlm_entry ref_count.
             */
            nlm_async_callback(nlm4_send_grant_msg, (void *)nlm_entry);
        }
    pthread_mutex_unlock(&nlm_lock_list_mutex);
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

int nlm_process_parameters(struct svc_req            * preq,
                           bool_t                      exclusive,
                           nlm4_lock                 * alock,
                           cache_lock_desc_t         * plock,
                           hash_table_t              * ht,
                           cache_entry_t            ** ppentry,
                           fsal_op_context_t         * pcontext,
                           cache_inode_client_t      * pclient,
                           bool_t                      care,
                           cache_inode_nlm_client_t ** ppnlm_client,
                           cache_lock_owner_t       ** ppowner)
{
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t      attr;
  cache_inode_status_t    cache_status;

  *ppnlm_client = NULL;
  *ppowner      = NULL;

  /* Convert file handle into a cache entry */
  if(!nfs3_FhandleToFSAL((nfs_fh3 *) &alock->fh, &fsal_data.handle, pcontext))
    {
      /* handle is not valid */
      return NLM4_STALE_FH;
    }

  /* Now get the cached inode attributes */
  fsal_data.cookie = DIR_START;
  *ppentry = cache_inode_get(&fsal_data,
                             &attr,
                             ht,
                             pclient,
                             pcontext,
                             &cache_status);
  if(*ppentry == NULL)
    {
      /* handle is not valid */
      return NLM4_STALE_FH;
    }

  *ppnlm_client = get_nlm_client(care, alock->caller_name);
  if(*ppnlm_client == NULL)
    {
      /* If client is not found, and we don't care (such as unlock),
       * just return GRANTED (the unlock must succeed, there can't be
       * any locks).
       */
      if(care)
        return NLM4_DENIED_NOLOCKS;
      else
        return NLM4_GRANTED;
    }

  *ppowner = get_nlm_owner(care, *ppnlm_client, &alock->oh, alock->svid);
  if(*ppowner == NULL)
    {
      dec_nlm_client_ref(*ppnlm_client);
      *ppnlm_client = NULL;

      /* If owner is not found, and we don't care (such as unlock),
       * just return GRANTED (the unlock must succeed, there can't be
       * any locks).
       */
      if(care)
        return NLM4_DENIED_NOLOCKS;
      else
        return NLM4_GRANTED;
    }

  /* Fill in plock */
  plock->cld_type   = exclusive ? CACHE_INODE_LOCK_W : CACHE_INODE_LOCK_R;
  plock->cld_offset = alock->l_offset;
  plock->cld_length = alock->l_len;

  return -1;
}

void nlm_process_conflict(nlm4_holder        * nlm_holder,
                          cache_lock_owner_t * holder,
                          cache_lock_desc_t  * conflict)
{
  if(conflict != NULL)
    {
      nlm_holder->exclusive = conflict->cld_type == CACHE_INODE_LOCK_W;
      nlm_holder->l_offset  = conflict->cld_offset;
      nlm_holder->l_len     = conflict->cld_length;
    }
  else
    {
      /* For some reason, don't have an actual conflict,
       * just make it exclusive over the whole file
       * (which would conflict with any lock requested).
       */
      nlm_holder->exclusive = TRUE;
      nlm_holder->l_offset  = 0;
      nlm_holder->l_len     = 0;
    }

  if(holder != NULL || holder->clo_type == CACHE_LOCK_OWNER_NLM)
    {
      nlm_holder->svid = holder->clo_owner.clo_nlm_owner.clo_nlm_svid;
      fill_netobj(&nlm_holder->oh,
                  holder->clo_owner.clo_nlm_owner.clo_nlm_oh,
                  holder->clo_owner.clo_nlm_owner.clo_nlm_oh_len);
    }
  else
    {
      /* If we don't have an NLM owner, not much we can do. */
      nlm_holder->svid       = 0;
      nlm_holder->oh.n_len   = 0;
      nlm_holder->oh.n_bytes = NULL;
    }
}

nlm4_stats nlm_convert_cache_inode_error(cache_inode_status_t status)
{
  switch(status)
    {
      case CACHE_INODE_SUCCESS:       return NLM4_GRANTED;
      case CACHE_INODE_LOCK_CONFLICT: return NLM4_DENIED;
      case CACHE_INODE_LOCK_BLOCKED:  return NLM4_BLOCKED;
      case CACHE_INODE_LOCK_DEADLOCK: return NLM4_DEADLCK;
      case CACHE_INODE_MALLOC_ERROR:  return NLM4_DENIED_NOLOCKS;
      case CACHE_INODE_NOT_FOUND:     return NLM4_STALE_FH;
      default:                        return NLM4_FAILED;
    }
}

cache_inode_status_t nlm_granted_callback(cache_entry_t        * pentry,
                                          fsal_op_context_t    * pcontext,
                                          cache_lock_entry_p     plock_entry,
                                          cache_inode_client_t * pclient,
                                          cache_inode_status_t * pstatus)
{
  return CACHE_INODE_NOT_SUPPORTED;
}
