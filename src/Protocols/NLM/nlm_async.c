/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * --------------------------
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
#include <pthread.h>

#include "stuff_alloc.h"
#include "sal_functions.h"
#include "nlm4.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "nfs_tcb.h"

static pthread_t               nlm_async_thread_id;
static struct glist_head       nlm_async_queue;
pthread_mutex_t                nlm_async_resp_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t                 nlm_async_resp_cond   = PTHREAD_COND_INITIALIZER;
cache_inode_client_parameter_t nlm_async_cache_inode_client_param;
cache_inode_client_t           nlm_async_cache_inode_client;
extern nfs_tcb_t               nlmtcb;

int nlm_send_async_res_nlm4(state_nlm_client_t * host,
                            nlm_callback_func    func,
                            nfs_res_t          * pres)
{
  nlm_async_queue_t *arg = (nlm_async_queue_t *) Mem_Alloc(sizeof(*arg));
  if(arg != NULL)
    {
      memset(arg, 0, sizeof(*arg));
      arg->nlm_async_host               = host;
      arg->nlm_async_func               = func;
      arg->nlm_async_args.nlm_async_res = *pres;
      if(!copy_netobj(&arg->nlm_async_args.nlm_async_res.res_nlm4.cookie, &pres->res_nlm4.cookie))
        {
          LogFullDebug(COMPONENT_NLM,
                       "Unable to copy async response file handle");
          Mem_Free(arg);
          return NFS_REQ_DROP;
        }
   }
 else
   {
      LogFullDebug(COMPONENT_NLM,
                   "Unable to allocate async response");
      return NFS_REQ_DROP;
   }

  P(nlmtcb.tcb_mutex);
  glist_add_tail(&nlm_async_queue, &arg->nlm_async_glist);
  if(pthread_cond_signal(&nlmtcb.tcb_condvar) == -1)
    {
      LogFullDebug(COMPONENT_NLM,
                   "Unable to signal nlm_asyn_thread");
      glist_del(&arg->nlm_async_glist);
      netobj_free(&arg->nlm_async_args.nlm_async_res.res_nlm4.cookie);
      Mem_Free(arg);
      arg = NULL;
    }
  V(nlmtcb.tcb_mutex);

  return arg != NULL ? NFS_REQ_OK : NFS_REQ_DROP;
}

int nlm_send_async_res_nlm4test(state_nlm_client_t * host,
                                nlm_callback_func    func,
                                nfs_res_t          * pres)
{
  nlm_async_queue_t *arg = (nlm_async_queue_t *) Mem_Alloc(sizeof(*arg));
  if(arg != NULL)
    {
      memset(arg, 0, sizeof(*arg));
      arg->nlm_async_host               = host;
      arg->nlm_async_func               = func;
      arg->nlm_async_args.nlm_async_res = *pres;
      if(!copy_netobj(&arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie, &pres->res_nlm4test.cookie))
        {
          LogFullDebug(COMPONENT_NLM,
                       "Unable to copy async response file handle");
          Mem_Free(arg);
          return NFS_REQ_DROP;
        }
      else if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
        {
          if(!copy_netobj(&arg->nlm_async_args.nlm_async_res.res_nlm4test.test_stat.nlm4_testrply_u.holder.oh,
                          &pres->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh))
            {
              LogFullDebug(COMPONENT_NLM,
                           "Unable to copy async response oh");
              netobj_free(&arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie);
              Mem_Free(arg);
              return NFS_REQ_DROP;
            }
        }
   }
 else
   {
      LogFullDebug(COMPONENT_NLM,
                   "Unable to allocate async response");
      return NFS_REQ_DROP;
   }

  P(nlmtcb.tcb_mutex);
  glist_add_tail(&nlm_async_queue, &arg->nlm_async_glist);
  if(pthread_cond_signal(&nlmtcb.tcb_condvar) == -1)
    {
      LogFullDebug(COMPONENT_NLM,
                   "Unable to signal nlm_asyn_thread");
      glist_del(&arg->nlm_async_glist);
      netobj_free(&arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie);
      if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
        netobj_free(&arg->nlm_async_args.nlm_async_res.res_nlm4test.test_stat.nlm4_testrply_u.holder.oh);
      Mem_Free(arg);
      arg = NULL;
    }
  V(nlmtcb.tcb_mutex);

  return arg != NULL ? NFS_REQ_OK : NFS_REQ_DROP;
}

/* Execute a func from the async queue */
void *nlm_async_thread(void *argp)
{
#ifndef _NO_BUDDY_SYSTEM
  int rc;
#endif
  nlm_async_queue_t *entry;
  struct timeval now;
  struct timespec timeout;
  struct glist_head nlm_async_tmp_queue;
  struct glist_head *glist, *glistn;

  SetNameFunction("nlm_async_thread");

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_NLM,
               "NLM async thread: Memory manager could not be initialized");
    }
  LogInfo(COMPONENT_NLM,
          "NLM async thread: Memory manager successfully initialized");
#endif
  if(mark_thread_existing(&nlmtcb) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&nlmtcb);
      LogDebug(COMPONENT_NLM,
               "NLM async thread: exiting before initialization");
      return NULL;
    }
  LogFullDebug(COMPONENT_NLM,
               "NLM async thread: my pthread id is %p",
               (caddr_t) pthread_self());

  while(1)
    {
      /* Check without tcb lock*/
      if((nlmtcb.tcb_state != STATE_AWAKE) || glist_empty(&nlm_async_queue))
        {
          while(1)
            {
              P(nlmtcb.tcb_mutex);
              if((nlmtcb.tcb_state == STATE_AWAKE) &&
                  !glist_empty(&nlm_async_queue))
                {
                  V(nlmtcb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&nlmtcb))
                {
                  case THREAD_SM_RECHECK:
                    V(nlmtcb.tcb_mutex);
                    continue;

                  case THREAD_SM_BREAK:
                    if(glist_empty(&nlm_async_queue))
                      {
                        gettimeofday(&now, NULL);
                        timeout.tv_sec = 10 + now.tv_sec;
                        timeout.tv_nsec = 0;
                        pthread_cond_timedwait(&nlmtcb.tcb_condvar,
                                               &nlmtcb.tcb_mutex,
                                               &timeout);
                      }
                    V(nlmtcb.tcb_mutex);
                    continue;

                  case THREAD_SM_EXIT:
                    V(nlmtcb.tcb_mutex);
                    return NULL;
                }
            }
        }

      P(nlmtcb.tcb_mutex);
      init_glist(&nlm_async_tmp_queue);
      /* Collect all the work items and add it to the temp
       * list. Later we iterate over tmp list without holding
       * the nlmtcb.tcb_mutex.
       */
      glist_for_each_safe(glist, glistn, &nlm_async_queue)
      {
        entry = glist_entry(glist, nlm_async_queue_t, nlm_async_glist);
        glist_del(glist);
        glist_add(&nlm_async_tmp_queue, glist);

      }
      V(nlmtcb.tcb_mutex);
      glist_for_each_safe(glist, glistn, &nlm_async_tmp_queue)
      {
        entry = glist_entry(glist, nlm_async_queue_t, nlm_async_glist);
        glist_del(&entry->nlm_async_glist);
        entry->nlm_async_func(entry);
      }
    }
  tcb_remove(&nlmtcb);
}

/* Insert 'func' to async queue */
int nlm_async_callback(nlm_async_queue_t *arg)
{
  int rc;

  LogFullDebug(COMPONENT_NLM, "Callback %p", arg);

  P(nlmtcb.tcb_mutex);
  glist_add_tail(&nlm_async_queue, &arg->nlm_async_glist);
  rc = pthread_cond_signal(&nlmtcb.tcb_condvar);
  if(rc == -1)
    glist_del(&arg->nlm_async_glist);
  V(nlmtcb.tcb_mutex);

  return rc;
}

static int local_lru_inode_entry_to_str(LRU_data_t data, char *str)
{
  return sprintf(str, "N/A ");
}                               /* local_lru_inode_entry_to_str */

static int local_lru_inode_clean_entry(LRU_entry_t * entry, void *adddata)
{
  return 0;
}                               /* lru_clean_entry */

int nlm_async_callback_init()
{
  init_glist(&nlm_async_queue);

  /* setting the 'nlm_async_cache_inode_client_param' structure */
  nlm_async_cache_inode_client_param.lru_param.nb_entry_prealloc = 10;
  nlm_async_cache_inode_client_param.lru_param.entry_to_str = local_lru_inode_entry_to_str;
  nlm_async_cache_inode_client_param.lru_param.clean_entry = local_lru_inode_clean_entry;
  nlm_async_cache_inode_client_param.nb_prealloc_entry = 0;
  nlm_async_cache_inode_client_param.nb_pre_parent = 0;
  nlm_async_cache_inode_client_param.nb_pre_state_v4 = 0;
  nlm_async_cache_inode_client_param.grace_period_link = 0;
  nlm_async_cache_inode_client_param.grace_period_attr = 0;
  nlm_async_cache_inode_client_param.grace_period_dirent = 0;
  nlm_async_cache_inode_client_param.grace_period_attr   = 0;
  nlm_async_cache_inode_client_param.grace_period_link   = 0;
  nlm_async_cache_inode_client_param.grace_period_dirent = 0;
  nlm_async_cache_inode_client_param.expire_type_attr    = CACHE_INODE_EXPIRE_NEVER;
  nlm_async_cache_inode_client_param.expire_type_link    = CACHE_INODE_EXPIRE_NEVER;
  nlm_async_cache_inode_client_param.expire_type_dirent  = CACHE_INODE_EXPIRE_NEVER;
  nlm_async_cache_inode_client_param.use_test_access = 1;
  nlm_async_cache_inode_client_param.attrmask = 0;

  if(cache_inode_client_init(&nlm_async_cache_inode_client,
                             nlm_async_cache_inode_client_param,
                             NLM_THREAD_INDEX, NULL))
    {
      LogCrit(COMPONENT_NLM,
              "Could not initialize cache inode client for NLM Async Thread");
      return -1;
    }
  
  return pthread_create(&nlm_async_thread_id, NULL, nlm_async_thread, NULL);
}

nlm_reply_proc_t nlm_reply_proc[] = {

  [NLMPROC4_GRANTED_MSG] = {
                            .inproc = (xdrproc_t) xdr_nlm4_testargs,
                            .outproc = (xdrproc_t) xdr_void,
                            }
  ,
  [NLMPROC4_TEST_RES] = {
                         .inproc = (xdrproc_t) xdr_nlm4_testres,
                         .outproc = (xdrproc_t) xdr_void,
                         }
  ,
  [NLMPROC4_LOCK_RES] = {
                         .inproc = (xdrproc_t) xdr_nlm4_res,
                         .outproc = (xdrproc_t) xdr_void,
                         }
  ,
  [NLMPROC4_CANCEL_RES] = {
                           .inproc = (xdrproc_t) xdr_nlm4_res,
                           .outproc = (xdrproc_t) xdr_void,
                           }
  ,
  [NLMPROC4_UNLOCK_RES] = {
                           .inproc = (xdrproc_t) xdr_nlm4_res,
                           .outproc = (xdrproc_t) xdr_void,
                           }
  ,
};

static void *resp_key;

/* Client routine  to send the asynchrnous response, key is used to wait for a response */
int nlm_send_async(int                  proc,
                   state_nlm_client_t * host,
                   void               * inarg,
                   void               * key)
{
  struct timeval tout = { 0, 10 };
  xdrproc_t inproc = NULL, outproc = NULL;
  int retval;
  struct timeval start, now;
  struct timespec timeout;

  if(host->slc_callback_clnt == NULL)
    {
      LogFullDebug(COMPONENT_NLM,
                   "Clnt_create %s",
                   host->slc_nsm_client->ssc_nlm_caller_name);

      host->slc_callback_clnt = Clnt_create(host->slc_nsm_client->ssc_nlm_caller_name,
                                            NLMPROG,
                                            NLM4_VERS,
                                            (char *)xprt_type_to_str(host->slc_client_type));

      if(host->slc_callback_clnt == NULL)
        {
          LogMajor(COMPONENT_NLM,
                   "Cannot create NLM async %s connection to client %s",
                   xprt_type_to_str(host->slc_client_type),
                   host->slc_nsm_client->ssc_nlm_caller_name);
          return -1;
        }
    }

  inproc = nlm_reply_proc[proc].inproc;
  outproc = nlm_reply_proc[proc].outproc;

  pthread_mutex_lock(&nlm_async_resp_mutex);
  resp_key = key;
  pthread_mutex_unlock(&nlm_async_resp_mutex);

  LogFullDebug(COMPONENT_NLM, "About to make clnt_call");
  retval = clnt_call(host->slc_callback_clnt, proc, inproc, inarg, outproc, NULL, tout);
  LogFullDebug(COMPONENT_NLM, "Done with clnt_call");

  if(retval == RPC_TIMEDOUT)
    retval = RPC_SUCCESS;
  else if(retval != RPC_SUCCESS)
    {
      LogMajor(COMPONENT_NLM,
               "%s: NLM async Client procedure call %d failed with return code %d",
               __func__, proc, retval);
      pthread_mutex_lock(&nlm_async_resp_mutex);
      resp_key = NULL;
      pthread_mutex_unlock(&nlm_async_resp_mutex);
    }

  pthread_mutex_lock(&nlm_async_resp_mutex);
  if(resp_key != NULL)
    {
      /* Wait for 5 seconds or a signal */
      gettimeofday(&start, NULL);
      gettimeofday(&now, NULL);
      timeout.tv_sec = 5 + start.tv_sec;
      timeout.tv_nsec = 0;
      LogFullDebug(COMPONENT_NLM,
                   "About to wait for signal for key %p",
                   resp_key);
      while(resp_key != NULL && now.tv_sec < (start.tv_sec + 5))
        {
          int rc = pthread_cond_timedwait(&nlm_async_resp_cond, &nlm_async_resp_mutex, &timeout);
          LogFullDebug(COMPONENT_NLM,
                       "pthread_cond_timedwait returned %d", rc);
          gettimeofday(&now, NULL);
        }
      LogFullDebug(COMPONENT_NLM, "Done waiting");
    }
  pthread_mutex_unlock(&nlm_async_resp_mutex);

  return retval;
}

void nlm_signal_async_resp(void *key)
{
  pthread_mutex_lock(&nlm_async_resp_mutex);
  if(resp_key == key)
    {
      resp_key = NULL;
      pthread_cond_signal(&nlm_async_resp_cond);
      LogFullDebug(COMPONENT_NLM,
                   "Signaled condition variable");
    }
  else
    {
      LogFullDebug(COMPONENT_NLM,
                   "Didn't signal condition variable");
    }
  pthread_mutex_unlock(&nlm_async_resp_mutex);
}
