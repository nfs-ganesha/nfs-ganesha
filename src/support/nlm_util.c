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
#include <sys/time.h>
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
static pthread_mutex_t nlm_lock_list_mutex;

/* nlm grace time tracking */
static struct timeval nlm_grace_tv;
#define NLM4_GRACE_PERIOD 10
/*
 * Time after which we should retry the granted
 * message request again
 */
#define NLM4_CLIENT_GRACE_PERIOD 3

typedef struct nlm_grant_parms_t
{
  cache_entry_t        * nlm_grant_pentry;
  fsal_op_context_t    * nlm_grant_pcontext;
  cache_lock_entry_t   * nlm_grant_lock_entry;
  cache_inode_client_t * nlm_grant_pclient;
} nlm_grant_parms_t;

/* We manage our own cookie for GRANTED call backs
 * Cookie 
 */
typedef struct granted_cookie_t
{
  unsigned long gc_seconds;
  unsigned long gc_microseconds;
  unsigned long gc_cookie;
} granted_cookie_t;

granted_cookie_t granted_cookie;
pthread_mutex_t granted_mutex = PTHREAD_MUTEX_INITIALIZER;

void next_granted_cookie(granted_cookie_t *cookie)
{
  P(granted_mutex);
  granted_cookie.gc_cookie++;
  *cookie = granted_cookie;
  V(granted_mutex);
}

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

void nlm_remove_from_locklist(nlm_lock_entry_t * nlm_entry)
{
    pthread_mutex_lock(&nlm_lock_list_mutex);
    //do_nlm_remove_from_locklist(nlm_entry);
    pthread_mutex_unlock(&nlm_lock_list_mutex);
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
  nsm_unmonitor_all();

  /* start NLM grace period */
  gettimeofday(&nlm_grace_tv, NULL);

  /* also use this time to initialize granted_cookie */
  granted_cookie.gc_seconds      = (unsigned long) nlm_grace_tv.tv_sec;
  granted_cookie.gc_microseconds = (unsigned long) nlm_grace_tv.tv_usec;
  granted_cookie.gc_cookie       = 0;
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

#if 0
// TODO FSF: need to implement
static void nlm4_send_grant_msg(void *arg)
{
  nlm_grant_parms_t *parms = arg;
  struct nlm4_testargs inarg;
  nfs_fh3 fh3;
  fh3.data.data_val = Mem_Alloc(NFS3_FHSIZE);
  if(fh3.data.data_val == NULL)
    {
      // TODO FSF: handle error
    }
  if(nfs3_FSALToFhandle(&fh3, &(pentry->object.file.handle), 
  

  ***************
  ***************
  ** I AM HERE **
  ***************
  ***************

  if(nfs3_FSALToFhandle((nfs_fh3 *)
    int retval;
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
    //nlm_grant_blocked_locks(&nlm_entry->arg.alock.fh);
    //nlm_lock_entry_dec_ref(nlm_entry);
    return;
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
                           cache_lock_owner_t       ** ppowner,
                           cache_inode_block_data_t ** ppblock_data)
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

  if(ppblock_data != NULL)
    {
      *ppblock_data = (cache_inode_block_data_t *) Mem_Alloc_Label(sizeof(**ppblock_data),
                                                                   "NLM_Block_Data");
      /* Fill in the block data, if we don't get one, we will just proceed
       * without (which will mean the lock doesn't block.
       */
      if(*ppblock_data != NULL)
        {
          memset(*ppblock_data, 0, sizeof(**ppblock_data));
          (*ppblock_data)->cbd_granted_callback = nlm_granted_callback;
          (*ppblock_data)->cbd_block_data.cbd_nlm_block_data.cbd_nlm_fh_len = alock->fh.n_len;
          memcpy((*ppblock_data)->cbd_block_data.cbd_nlm_block_data.cbd_nlm_fh,
                 alock->fh.n_bytes,
                 alock->fh.n_len);
        }
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
                                          cache_lock_entry_t   * lock_entry,
                                          cache_inode_client_t * pclient,
                                          cache_inode_status_t * pstatus)
{
#if 0
// TODO FSF: need to implement
  nlm_grant_parms_t *parms;

  parms = (nlm_grant_parms_t *) Mem_Alloc_Label(sizeof(*parms), "nlm_grant_parms_t");
  if(parms == NULL)
    {
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }

  /* Get a reference to the lock entry */
  lock_entry_inc_ref(lock_entry);
  parms-> nlm_grant_pentry     = pentry;
  parms-> nlm_grant_pcontext   = pcontext;
  parms-> nlm_grant_lock_entry = lock_entry;
  parms-> nlm_grant_pclient    = pclient;
  if(nlm_async_callback(nlm4_send_grant_msg, parms) == -1)
    {
      Mem_Free(parms);
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }
  *pstatus = CACHE_INODE_SUCCESS;
#else
  *pstatus = CACHE_INODE_NOT_SUPPORTED;
#endif
  return *pstatus;
}
