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

#include "sal_functions.h"
#include "nlm4.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "nfs_tcb.h"

pthread_mutex_t nlm_async_resp_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  nlm_async_resp_cond   = PTHREAD_COND_INITIALIZER;

int nlm_send_async_res_nlm4(state_nlm_client_t * host,
                            state_async_func_t   func,
                            nfs_res_t          * pres)
{
  state_async_queue_t    * arg = gsh_malloc(sizeof(*arg));
  state_nlm_async_data_t * nlm_arg;
  state_status_t           status;

  if(arg != NULL)
    {
      nlm_arg = &arg->state_async_data.state_nlm_async_data;
      memset(arg, 0, sizeof(*arg));
      arg->state_async_func                 = func;
      nlm_arg->nlm_async_host               = host;
      nlm_arg->nlm_async_args.nlm_async_res = *pres;
      if(!copy_netobj(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4.cookie,
                      &pres->res_nlm4.cookie))
        {
          LogCrit(COMPONENT_NLM,
                  "Unable to copy async response file handle");
          gsh_free(arg);
          return NFS_REQ_DROP;
        }
   }
 else
   {
      LogCrit(COMPONENT_NLM,
              "Unable to allocate async response");
      return NFS_REQ_DROP;
   }

  status = state_async_schedule(arg);

  if(status != STATE_SUCCESS)
    {
      gsh_free(arg);
      return NFS_REQ_DROP;
    }

  return NFS_REQ_OK;
}

int nlm_send_async_res_nlm4test(state_nlm_client_t * host,
                                state_async_func_t   func,
                                nfs_res_t          * pres)
{
  state_async_queue_t    * arg = gsh_malloc(sizeof(*arg));
  state_nlm_async_data_t * nlm_arg;
  state_status_t           status;

  if(arg != NULL)
    {
      nlm_arg = &arg->state_async_data.state_nlm_async_data;
      memset(arg, 0, sizeof(*arg));
      arg->state_async_func               = func;
      nlm_arg->nlm_async_host               = host;
      nlm_arg->nlm_async_args.nlm_async_res = *pres;
      if(!copy_netobj(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie,
                      &pres->res_nlm4test.cookie))
        {
          LogCrit(COMPONENT_NLM,
                  "Unable to copy async response file handle");
          gsh_free(arg);
          return NFS_REQ_DROP;
        }
      else if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
        {
          if(!copy_netobj(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.test_stat.nlm4_testrply_u.holder.oh,
                          &pres->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh))
            {
              LogCrit(COMPONENT_NLM,
                      "Unable to copy async response oh");
              netobj_free(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie);
              gsh_free(arg);
              return NFS_REQ_DROP;
            }
        }
   }
 else
   {
      LogCrit(COMPONENT_NLM,
              "Unable to allocate async response");
      return NFS_REQ_DROP;
   }


  status = state_async_schedule(arg);

  if(status != STATE_SUCCESS)
    {
      netobj_free(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie);
      if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
        netobj_free(&nlm_arg->nlm_async_args.nlm_async_res.res_nlm4test.test_stat.nlm4_testrply_u.holder.oh);
      gsh_free(arg);
      return NFS_REQ_DROP;
    }

  return NFS_REQ_OK;
}

xdrproc_t nlm_reply_proc[] =
{
  [NLMPROC4_GRANTED_MSG] = (xdrproc_t) xdr_nlm4_testargs,
  [NLMPROC4_TEST_RES]    = (xdrproc_t) xdr_nlm4_testres,
  [NLMPROC4_LOCK_RES]    = (xdrproc_t) xdr_nlm4_res,
  [NLMPROC4_CANCEL_RES]  = (xdrproc_t) xdr_nlm4_res,
  [NLMPROC4_UNLOCK_RES]  = (xdrproc_t) xdr_nlm4_res,
};

static void *resp_key;

#define MAX_ASYNC_RETRY 2

/* Client routine  to send the asynchrnous response, key is used to wait for a response */
int nlm_send_async(int                  proc,
                   state_nlm_client_t * host,
                   void               * inarg,
                   void               * key)
{
  struct timeval  tout = { 0, 10 };
  int             retval, retry;
  struct timeval  start, now;
  struct timespec timeout;

  for(retry = 1; retry <= MAX_ASYNC_RETRY; retry++)
    {
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

      pthread_mutex_lock(&nlm_async_resp_mutex);
      resp_key = key;
      pthread_mutex_unlock(&nlm_async_resp_mutex);

      LogFullDebug(COMPONENT_NLM, "About to make clnt_call");
      retval = clnt_call(host->slc_callback_clnt,
                         proc,
                         nlm_reply_proc[proc],
                         inarg,
                         (xdrproc_t) xdr_void,
                         NULL,
                         tout);
      LogFullDebug(COMPONENT_NLM, "Done with clnt_call");

      if(retval == RPC_TIMEDOUT || retval == RPC_SUCCESS)
        {
          retval = RPC_SUCCESS;
          break;
        }

      LogCrit(COMPONENT_NLM,
              "NLM async Client procedure call %d failed with return code %d %s",
              proc, retval, clnt_sperror(host->slc_callback_clnt, ""));

      Clnt_destroy(host->slc_callback_clnt);
      host->slc_callback_clnt = NULL;

      if(retry == MAX_ASYNC_RETRY)
        {
          LogMajor(COMPONENT_NLM,
                   "NLM async Client exceeded retry count %d",
                   MAX_ASYNC_RETRY);
          pthread_mutex_lock(&nlm_async_resp_mutex);
          resp_key = NULL;
          pthread_mutex_unlock(&nlm_async_resp_mutex);
          return retval;
        }
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
