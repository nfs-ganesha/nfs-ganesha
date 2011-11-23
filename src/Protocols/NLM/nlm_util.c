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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nsm.h"
#include "nlm_async.h"
#include "nfs_core.h"
#include "nfs_tcb.h"

nfs_tcb_t  nlmtcb;
/* nlm grace time tracking */
static struct timeval nlm_grace_tv;
#define NLM4_GRACE_PERIOD 10
/*
 * Time after which we should retry the granted
 * message request again
 */
#define NLM4_CLIENT_GRACE_PERIOD 3

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

bool_t fill_netobj(netobj * dst, char *data, int len)
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
      else
        return FALSE;
    }
  return TRUE;
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
      buf += sprintf(buf, "%02x", (unsigned char) obj->n_bytes[pos]);
      left -= 2;
      pos++;
    }
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
  /* start NLM grace period */
  gettimeofday(&nlm_grace_tv, NULL);

  /* also use this time to initialize granted_cookie */
  granted_cookie.gc_seconds      = (unsigned long) nlm_grace_tv.tv_sec;
  granted_cookie.gc_microseconds = (unsigned long) nlm_grace_tv.tv_usec;
  granted_cookie.gc_cookie       = 0;
  tcb_new(&nlmtcb, "NLM async thread");
}

void nlm_startup(void)
{
  if(nlm_async_callback_init() == -1)
    LogFatal(COMPONENT_INIT,
             "Could not start NLM async thread");
}

void free_grant_arg(nlm_async_queue_t *arg)
{
  netobj_free(&arg->nlm_async_args.nlm_async_grant.cookie);
  netobj_free(&arg->nlm_async_args.nlm_async_grant.alock.oh);
  netobj_free(&arg->nlm_async_args.nlm_async_grant.alock.fh);
  if(arg->nlm_async_args.nlm_async_grant.alock.caller_name != NULL)
    Mem_Free(arg->nlm_async_args.nlm_async_grant.alock.caller_name);
  Mem_Free(arg);
}

/**
 *
 * nlm4_send_grant_msg: Send NLMPROC4_GRANTED_MSG
 *
 * This runs in the nlm_asyn_thread context.
 */
static void nlm4_send_grant_msg(nlm_async_queue_t *arg)
{
  int                    retval;
  char                   buffer[1024];
  state_status_t         state_status = STATE_SUCCESS;
  state_cookie_entry_t * cookie_entry;
  fsal_op_context_t      context, * pcontext = &context;

  if(isDebug(COMPONENT_NLM))
    {
      netobj_to_string(&arg->nlm_async_args.nlm_async_grant.cookie,
                       buffer, sizeof(buffer));

      LogDebug(COMPONENT_NLM,
               "Sending GRANTED for arg=%p svid=%d start=%llx len=%llx cookie=%s",
               arg, arg->nlm_async_args.nlm_async_grant.alock.svid,
               (unsigned long long) arg->nlm_async_args.nlm_async_grant.alock.l_offset,
               (unsigned long long) arg->nlm_async_args.nlm_async_grant.alock.l_len,
               buffer);
    }

  retval = nlm_send_async(NLMPROC4_GRANTED_MSG,
                          arg->nlm_async_host,
                          &(arg->nlm_async_args.nlm_async_grant),
                          arg->nlm_async_key);

  free_grant_arg(arg);

  /* If success, we are done. */
  if(retval == RPC_SUCCESS)
    return;

  /*
   * We are not able call granted callback. Some client may retry
   * the lock again. So remove the existing blocked nlm entry
   */
  LogMajor(COMPONENT_NLM,
           "GRANTED_MSG RPC call failed with return code %d. Removing the blocking lock",
           retval);

  if(state_find_grant(arg->nlm_async_args.nlm_async_grant.cookie.n_bytes,
                      arg->nlm_async_args.nlm_async_grant.cookie.n_len,
                      &cookie_entry,
                      &nlm_async_cache_inode_client,
                      &state_status) != STATE_SUCCESS)
    {
      /* This must be an old NLM_GRANTED_RES */
      LogFullDebug(COMPONENT_NLM,
                   "Could not find cookie=%s status=%s",
                   buffer, state_err_str(state_status));
      return;
    }

  P(cookie_entry->sce_pentry->object.file.lock_list_mutex);

  if(cookie_entry->sce_lock_entry->sle_block_data == NULL ||
     !nlm_block_data_to_fsal_context(&cookie_entry->sce_lock_entry->sle_block_data->sbd_block_data.sbd_nlm_block_data,
                                     pcontext))
    {
      /* Wow, we're not doing well... */
      V(cookie_entry->sce_pentry->object.file.lock_list_mutex);
      LogFullDebug(COMPONENT_NLM,
                   "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
                   buffer);
      return;
    }

  V(cookie_entry->sce_pentry->object.file.lock_list_mutex);

  if(state_release_grant(pcontext,
                         cookie_entry,
                         &nlm_async_cache_inode_client,
                         &state_status) != STATE_SUCCESS)
    {
      /* Huh? */
      LogFullDebug(COMPONENT_NLM,
                   "Could not release cookie=%s status=%s",
                   buffer, state_err_str(state_status));
    }
}

int nlm_process_parameters(struct svc_req        * preq,
                           bool_t                  exclusive,
                           nlm4_lock             * alock,
                           state_lock_desc_t     * plock,
                           hash_table_t          * ht,
                           cache_entry_t        ** ppentry,
                           fsal_op_context_t     * pcontext,
                           cache_inode_client_t  * pclient,
                           care_t                  care,
                           state_nsm_client_t   ** ppnsm_client,
                           state_nlm_client_t   ** ppnlm_client,
                           state_owner_t        ** ppowner,
                           state_block_data_t   ** ppblock_data)
{
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t      attr;
  cache_inode_status_t    cache_status;
  SVCXPRT                *ptr_svc = preq->rq_xprt;

  *ppnsm_client = NULL;
  *ppnlm_client = NULL;
  *ppowner      = NULL;

  /* Convert file handle into a cache entry */
  if(alock->fh.n_len > MAX_NETOBJ_SZ ||
     !nfs3_FhandleToFSAL((nfs_fh3 *) &alock->fh, &fsal_data.handle, pcontext))
    {
      /* handle is not valid */
      return NLM4_STALE_FH;
    }

  /* Now get the cached inode attributes */
  fsal_data.cookie = DIR_START;
  *ppentry = cache_inode_get(&fsal_data,
                             CACHE_INODE_JOKER_POLICY,
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

  *ppnsm_client = get_nsm_client(care, ptr_svc, alock->caller_name);

  if(*ppnsm_client == NULL)
    {
      /* If NSM Client is not found, and we don't care (such as unlock),
       * just return GRANTED (the unlock must succeed, there can't be
       * any locks).
       */
      if(care != CARE_NOT)
        return NLM4_DENIED_NOLOCKS;
      else
        return NLM4_GRANTED;
    }

  *ppnlm_client = get_nlm_client(care, ptr_svc, *ppnsm_client, alock->caller_name);

  if(*ppnlm_client == NULL)
    {
      /* If NLM Client is not found, and we don't care (such as unlock),
       * just return GRANTED (the unlock must succeed, there can't be
       * any locks).
       */
      dec_nsm_client_ref(*ppnsm_client);

      if(care != CARE_NOT)
        return NLM4_DENIED_NOLOCKS;
      else
        return NLM4_GRANTED;
    }

  *ppowner = get_nlm_owner(care, *ppnlm_client, &alock->oh, alock->svid);

  if(*ppowner == NULL)
    {
      LogDebug(COMPONENT_NLM,
               "Could not get NLM Owner");
      dec_nsm_client_ref(*ppnsm_client);
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
      *ppblock_data = (state_block_data_t *) Mem_Alloc_Label(sizeof(**ppblock_data),
                                                             "NLM_Block_Data");
      /* Fill in the block data, if we don't get one, we will just proceed
       * without (which will mean the lock doesn't block.
       */
      if(*ppblock_data != NULL)
        {
          memset(*ppblock_data, 0, sizeof(**ppblock_data));
          if(copy_xprt_addr(&(*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_nlm_hostaddr, ptr_svc) == 0)
            {
              LogFullDebug(COMPONENT_NLM,
                           "copy_xprt_addr failed for Program %d, Version %d, Function %d",
                           (int)preq->rq_prog, (int)preq->rq_vers, (int)preq->rq_proc);
              Mem_Free(*ppblock_data);
              *ppblock_data = NULL;
              return NLM4_FAILED;
            }
          (*ppblock_data)->sbd_granted_callback = nlm_granted_callback;
          (*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_nlm_fh.n_bytes =
            (*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_nlm_fh_buf;
          (*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_nlm_fh.n_len = alock->fh.n_len;
          memcpy((*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_nlm_fh_buf,
                 alock->fh.n_bytes,
                 alock->fh.n_len);
          /* FSF TODO: Ultimately I think the following will go away, we won't need the context, just the export */
          /* Copy credentials from pcontext */
          (*ppblock_data)->sbd_block_data.sbd_nlm_block_data.sbd_credential = pcontext->credential;
        }
    }
  /* Fill in plock */
  plock->sld_type   = exclusive ? STATE_LOCK_W : STATE_LOCK_R;
  plock->sld_offset = alock->l_offset;
  plock->sld_length = alock->l_len;

  LogFullDebug(COMPONENT_NLM,
               "Parameters Processed");

  return -1;
}

void nlm_process_conflict(nlm4_holder          * nlm_holder,
                          state_owner_t        * holder,
                          state_lock_desc_t    * conflict,
                          cache_inode_client_t * pclient)
{
  if(conflict != NULL)
    {
      nlm_holder->exclusive = conflict->sld_type == STATE_LOCK_W;
      nlm_holder->l_offset  = conflict->sld_offset;
      nlm_holder->l_len     = conflict->sld_length;
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

  if(holder != NULL)
    {
      if(holder->so_type == STATE_LOCK_OWNER_NLM)
        nlm_holder->svid = holder->so_owner.so_nlm_owner.so_nlm_svid;
      else
        nlm_holder->svid = 0;
      fill_netobj(&nlm_holder->oh,
                  holder->so_owner_val,
                  holder->so_owner_len);
    }
  else
    {
      /* If we don't have an NLM owner, not much we can do. */
      nlm_holder->svid       = 0;
      fill_netobj(&nlm_holder->oh,
                  unknown_owner.so_owner_val,
                  unknown_owner.so_owner_len);
    }

  /* Release any lock owner reference passed back from SAL */
  if(holder != NULL)
    dec_state_owner_ref(holder, pclient);
}

nlm4_stats nlm_convert_state_error(state_status_t status)
{
  switch(status)
    {
      case STATE_SUCCESS:       return NLM4_GRANTED;
      case STATE_LOCK_CONFLICT: return NLM4_DENIED;
      case STATE_MALLOC_ERROR:  return NLM4_DENIED_NOLOCKS;
      case STATE_LOCK_BLOCKED:  return NLM4_BLOCKED;
      case STATE_GRACE_PERIOD:  return NLM4_DENIED_GRACE_PERIOD;
      case STATE_LOCK_DEADLOCK: return NLM4_DEADLCK;
      case STATE_READ_ONLY_FS:  return NLM4_ROFS;
      case STATE_NOT_FOUND:     return NLM4_STALE_FH;
      case STATE_FSAL_ESTALE:   return NLM4_STALE_FH;
      case STATE_FILE_BIG:      return NLM4_FBIG;
      default:                  return NLM4_FAILED;
    }
}

bool_t nlm_block_data_to_fsal_context(state_nlm_block_data_t * nlm_block_data,
                                      fsal_op_context_t      * fsal_context)
{
  exportlist_t                 * pexport = NULL;
  short                          exportid;
  fsal_status_t                  fsal_status;

  /* Get export ID from handle */
  exportid = nlm4_FhandleToExportId(&nlm_block_data->sbd_nlm_fh);

  /* Get export matching export ID */
  if(exportid < 0 ||
     (pexport = nfs_Get_export_by_id(nfs_param.pexportlist, exportid)) == NULL ||
     (pexport->options & EXPORT_OPTION_NFSV3) == 0)
    {
      /* Reject the request for authentication reason (incompatible file handle) */
      if(isInfo(COMPONENT_NLM))
        {
          char dumpfh[1024];
          char *reason;
          char addrbuf[SOCK_NAME_MAX];
          sprint_sockaddr(&nlm_block_data->sbd_nlm_hostaddr,
                          addrbuf,
                          sizeof(addrbuf));
          if(exportid < 0)
            reason = "has badly formed handle";
          else if(pexport == NULL)
            reason = "has invalid export";
          else
            reason = "V3 not allowed on this export";
          sprint_fhandle_nlm(dumpfh, &nlm_block_data->sbd_nlm_fh);
          LogMajor(COMPONENT_NLM,
                   "NLM4 granted lock from host %s %s, FH=%s",
                   addrbuf, reason, dumpfh);
        }

      return FALSE;
    }

  LogFullDebug(COMPONENT_NLM,
               "Found export entry for dirname=%s as exportid=%d",
               pexport->dirname, pexport->id);
  /* Build the credentials */
  fsal_status = FSAL_GetClientContext(fsal_context,
                                      &pexport->FS_export_context,
                                      nlm_block_data->sbd_credential.user,
                                      nlm_block_data->sbd_credential.group,
                                      nlm_block_data->sbd_credential.alt_groups,
                                      nlm_block_data->sbd_credential.nbgroups);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogEvent(COMPONENT_NLM,
               "Could not get credentials for (uid=%d,gid=%d), fsal error=(%d,%d)",
               nlm_block_data->sbd_credential.user,
               nlm_block_data->sbd_credential.group,
               fsal_status.major, fsal_status.minor);
      return FALSE;
    }
  else
    LogDebug(COMPONENT_NLM,
             "FSAL Cred acquired for (uid=%d,gid=%d)",
             nlm_block_data->sbd_credential.user,
             nlm_block_data->sbd_credential.group);

  return TRUE;
}

state_status_t nlm_granted_callback(cache_entry_t        * pentry,
                                    state_lock_entry_t   * lock_entry,
                                    cache_inode_client_t * pclient,
                                    state_status_t       * pstatus)
{
  fsal_op_context_t        fsal_context, *pcontext = &fsal_context;
  state_block_data_t     * block_data     = lock_entry->sle_block_data;
  state_nlm_block_data_t * nlm_block_data = &block_data->sbd_block_data.sbd_nlm_block_data;
  state_cookie_entry_t   * cookie_entry;
  nlm_async_queue_t      * arg;
  nlm4_testargs          * inarg;
  state_nlm_owner_t      * nlm_grant_owner  = &lock_entry->sle_owner->so_owner.so_nlm_owner;
  state_nlm_client_t     * nlm_grant_client = nlm_grant_owner->so_client;
  granted_cookie_t         nlm_grant_cookie;

  if(nlm_block_data_to_fsal_context(nlm_block_data, &fsal_context) != TRUE)
    {
      *pstatus = STATE_INCONSISTENT_ENTRY;
      return *pstatus;
    }

  arg = (nlm_async_queue_t *) Mem_Alloc(sizeof(*arg));
  if(arg == NULL)
    {
      /* If we fail allocation the best is to delete the block entry
      * so that client can try again and get the lock. May be
      * by then we are able to allocate objects
      */
      *pstatus = STATE_MALLOC_ERROR;
      return *pstatus;
   }

  memset(arg, 0, sizeof(*arg));

  /* Get a cookie to use for this grant */
  next_granted_cookie(&nlm_grant_cookie);

  /* Add a cookie to the blocked lock pending grant.
   * It will also request lock from FSAL.
   * Could return STATE_LOCK_BLOCKED because FSAL would have had to block.
   */
  if(state_add_grant_cookie(pentry,
                            pcontext,
                            &nlm_grant_cookie,
                            sizeof(nlm_grant_cookie),
                            lock_entry,
                            &cookie_entry,
                            pclient,
                            pstatus) != STATE_SUCCESS)
    {
      free_grant_arg(arg);
      return *pstatus;
    }

  /* Fill in the arguments for the NLMPROC4_GRANTED_MSG call */
  arg->nlm_async_func = nlm4_send_grant_msg;
  arg->nlm_async_host = nlm_grant_client;
  arg->nlm_async_key  = cookie_entry;
  inarg = &arg->nlm_async_args.nlm_async_grant;

  if(!copy_netobj(&inarg->alock.fh, &nlm_block_data->sbd_nlm_fh))
    goto grant_fail;

  if(!fill_netobj(&inarg->alock.oh,
                  lock_entry->sle_owner->so_owner_val,
                  lock_entry->sle_owner->so_owner_len))
    goto grant_fail;

  if(!fill_netobj(&inarg->cookie,
                  (char *) &nlm_grant_cookie,
                  sizeof(nlm_grant_cookie)))
    goto grant_fail;

  inarg->alock.caller_name = Str_Dup(nlm_grant_client->slc_nlm_caller_name);
  if(!inarg->alock.caller_name)
    goto grant_fail;

  inarg->exclusive      = lock_entry->sle_lock.sld_type == STATE_LOCK_W;
  inarg->alock.svid     = nlm_grant_owner->so_nlm_svid;
  inarg->alock.l_offset = lock_entry->sle_lock.sld_offset;
  inarg->alock.l_len    = lock_entry->sle_lock.sld_length;
  if(isDebug(COMPONENT_NLM))
    {
      char buffer[1024];

      netobj_to_string(&inarg->cookie, buffer, sizeof(buffer));

      LogDebug(COMPONENT_NLM,
               "Sending GRANTED for arg=%p svid=%d start=%llx len=%llx cookie=%s",
               arg, inarg->alock.svid,
               (unsigned long long) inarg->alock.l_offset, (unsigned long long) inarg->alock.l_len,
               buffer);
    }

  /* Now try to schedule NLMPROC4_GRANTED_MSG call */
  if(nlm_async_callback(arg) == -1)
    goto grant_fail;

  *pstatus = STATE_SUCCESS;
  return *pstatus;

 grant_fail:

  /* Something went wrong after we added a grant cookie, need to clean up */

  /* Clean up NLMPROC4_GRANTED_MSG arguments */
  free_grant_arg(arg);

  /* Cancel the pending grant to release the cookie */
  if(state_cancel_grant(pcontext,
                        cookie_entry,
                        pclient,
                        pstatus) != STATE_SUCCESS)
    {
      /* Not much we can do other than log that something bad happened. */
      LogCrit(COMPONENT_NLM,
              "Unable to clean up GRANTED lock after error");
    }

  *pstatus = STATE_MALLOC_ERROR;
  return *pstatus;
}
