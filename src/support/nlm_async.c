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
#include <sys/time.h>
#include <strings.h>

#include "stuff_alloc.h"
#include "nfs_proto_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "nlm4.h"

static pthread_t nlm_async_thread;
static struct glist_head nlm_async_queue;
static pthread_mutex_t nlm_async_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  nlm_async_queue_cond  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t nlm_async_resp_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  nlm_async_resp_cond   = PTHREAD_COND_INITIALIZER;
cache_inode_client_parameter_t nlm_async_cache_inode_client_param;
cache_inode_client_t nlm_async_cache_inode_client;

typedef struct
{
  nlm_callback_func *func;
  void *arg;
  struct glist_head glist;
} nlm_queue_t;

nlm_async_res_t *nlm_build_async_res_nlm4(char *caller_name, nfs_res_t * pres)
{
  nlm_async_res_t *arg;
  arg = (nlm_async_res_t *) Mem_Alloc(sizeof(nlm_async_res_t));
  if(arg != NULL)
    {
      arg->caller_name = Str_Dup(caller_name);
      if(arg->caller_name == NULL)
        {
          Mem_Free(arg);
          return NULL;
        }
      memcpy(&(arg->pres), pres, sizeof(nfs_res_t));
      if(!copy_netobj(&arg->pres.res_nlm4.cookie, &pres->res_nlm4.cookie))
        {
          Mem_Free(arg);
          return NULL;
        }
   }
  return arg;
}

nlm_async_res_t *nlm_build_async_res_nlm4test(char *caller_name, nfs_res_t * pres)
{
  nlm_async_res_t *arg;
  arg = (nlm_async_res_t *) Mem_Alloc(sizeof(nlm_async_res_t));
  if(arg != NULL)
    {
      arg->caller_name = Str_Dup(caller_name);
      if(arg->caller_name == NULL)
        {
          Mem_Free(arg);
          return NULL;
        }
      memcpy(&(arg->pres), pres, sizeof(nfs_res_t));
      if(!copy_netobj(&arg->pres.res_nlm4test.cookie, &pres->res_nlm4test.cookie))
        {
          Mem_Free(arg);
          return NULL;
        }
      else if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
        {
          if(!copy_netobj(&arg->pres.res_nlm4test.test_stat.nlm4_testrply_u.holder.oh, &pres->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh))
            {
              netobj_free(&arg->pres.res_nlm4test.cookie);
              Mem_Free(arg);
              return NULL;
            }
        }
   }
  return arg;
}

/* Execute a func from the async queue */
void *nlm_async_func(void *argp)
{
  int rc;
  nlm_queue_t *entry;
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
  LogFullDebug(COMPONENT_NLM,
               "NLM async thread: my pthread id is %p",
               (caddr_t) pthread_self());

  while(1)
    {
      pthread_mutex_lock(&nlm_async_queue_mutex);
      while(glist_empty(&nlm_async_queue))
        {
          gettimeofday(&now, NULL);
          timeout.tv_sec = 10 + now.tv_sec;
          timeout.tv_nsec = 0;
          LogFullDebug(COMPONENT_NLM, "nlm_async_thread waiting...");
          pthread_cond_timedwait(&nlm_async_queue_cond, &nlm_async_queue_mutex, &timeout);
          LogFullDebug(COMPONENT_NLM, "nlm_async_thread woke up");
        }
      init_glist(&nlm_async_tmp_queue);
      /* Collect all the work items and add it to the temp
       * list. Later we iterate over tmp list without holding
       * the nlm_async_queue_mutex
       */
      glist_for_each_safe(glist, glistn, &nlm_async_queue)
      {
        entry = glist_entry(glist, nlm_queue_t, glist);
        glist_del(glist);
        glist_add(&nlm_async_tmp_queue, glist);

      }
      pthread_mutex_unlock(&nlm_async_queue_mutex);
      glist_for_each_safe(glist, glistn, &nlm_async_tmp_queue)
      {
        entry = glist_entry(glist, nlm_queue_t, glist);
        glist_del(&entry->glist);
        /* FIXME should we handle error here ? */
        (*(entry->func)) (entry->arg);
        Mem_Free(entry);
      }
    }

}

/* Insert 'func' to async queue */
int nlm_async_callback(nlm_callback_func * func, void *arg)
{
  int rc;
  nlm_queue_t *q;

  q = (nlm_queue_t *) Mem_Alloc(sizeof(nlm_queue_t));
  if(q == NULL)
    return -1;

  q->func = func;
  q->arg  = arg;

  LogFullDebug(COMPONENT_NLM, "nlm_async_callback %p:%p", func, arg);

  P(nlm_async_queue_mutex);
  glist_add_tail(&nlm_async_queue, &q->glist);
  rc = pthread_cond_signal(&nlm_async_queue_cond);
  V(nlm_async_queue_mutex);

  if(rc == -1)
    Mem_Free(q);

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
  nlm_async_cache_inode_client_param.nb_pre_dir_data = 0;
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
  
  return pthread_create(&nlm_async_thread, NULL, nlm_async_func, NULL);
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
int nlm_send_async(int proc, char *host, void *inarg, void *key)
{
  CLIENT *clnt;
  struct timeval tout = { 0, 10 };
  xdrproc_t inproc = NULL, outproc = NULL;
  int retval;
  struct timeval start, now;
  struct timespec timeout;

  clnt = Clnt_create(host, NLMPROG, NLM4_VERS, "tcp");
  if(!clnt)
    {
      LogMajor(COMPONENT_NLM, "%s: Cannot create connection to %s client",
               __func__, host);
      return -1;
    }
  inproc = nlm_reply_proc[proc].inproc;
  outproc = nlm_reply_proc[proc].outproc;

  pthread_mutex_lock(&nlm_async_resp_mutex);
  resp_key = key;
  pthread_mutex_unlock(&nlm_async_resp_mutex);

  LogFullDebug(COMPONENT_NLM, "nlm_send_async about to make clnt_call");
  retval = clnt_call(clnt, proc, inproc, inarg, outproc, NULL, tout);
  LogFullDebug(COMPONENT_NLM, "nlm_send_async done with clnt_call");

  if(retval == RPC_TIMEDOUT)
    retval = RPC_SUCCESS;
  else if(retval != RPC_SUCCESS)
    {
      LogMajor(COMPONENT_NLM,
               "%s: Client procedure call %d failed with return code %d",
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
                   "nlm_send_async about to wait for signal for key %p",
                   resp_key);
      while(resp_key != NULL && now.tv_sec < (start.tv_sec + 5))
        {
          int rc = pthread_cond_timedwait(&nlm_async_resp_cond, &nlm_async_resp_mutex, &timeout);
          LogFullDebug(COMPONENT_NLM,
                       "pthread_cond_timedwait returned %d", rc);
          gettimeofday(&now, NULL);
        }
      LogFullDebug(COMPONENT_NLM, "nlm_send_async done waiting");
    }
  pthread_mutex_unlock(&nlm_async_resp_mutex);

  Clnt_destroy(clnt);
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
                   "nlm_signal_async_resp signaled condition variable");
    }
  else
    {
      LogFullDebug(COMPONENT_NLM,
                   "nlm_signal_async_resp didn't signal condition variable");
    }
  pthread_mutex_unlock(&nlm_async_resp_mutex);
}
