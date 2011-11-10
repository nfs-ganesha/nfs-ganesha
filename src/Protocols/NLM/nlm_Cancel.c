/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
#include <string.h>
#include <pthread.h>
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * nlm4_Cancel: Cancel a blocked range lock
 *
 *  @param parg        [IN]
 *  @param pexportlist [IN]
 *  @param pcontextp   [IN]
 *  @param pclient     [INOUT]
 *  @param ht          [INOUT]
 *  @param preq        [IN]
 *  @param pres        [OUT]
 *
 */

int nlm4_Cancel(nfs_arg_t * parg /* IN     */ ,
                exportlist_t * pexport /* IN     */ ,
                fsal_op_context_t * pcontext /* IN     */ ,
                cache_inode_client_t * pclient /* INOUT  */ ,
                hash_table_t * ht /* INOUT  */ ,
                struct svc_req *preq /* IN     */ ,
                nfs_res_t * pres /* OUT    */ )
{
  nlm4_cancargs      * arg = &parg->arg_nlm4_cancel;
  cache_entry_t      * pentry;
  state_status_t       state_status = STATE_SUCCESS;
  char                 buffer[MAXNETOBJ_SZ * 2];
  state_nsm_client_t * nsm_client;
  state_nlm_client_t * nlm_client;
  state_owner_t      * nlm_owner;
  state_lock_desc_t    lock;
  int                  rc;

  netobj_to_string(&arg->cookie, buffer, 1024);
  LogDebug(COMPONENT_NLM,
           "REQUEST PROCESSING: Calling nlm4_Cancel svid=%d off=%llx len=%llx cookie=%s",
           (int) arg->alock.svid,
           (unsigned long long) arg->alock.l_offset,
           (unsigned long long) arg->alock.l_len,
           buffer);

  if(!copy_netobj(&pres->res_nlm4test.cookie, &arg->cookie))
    {
      pres->res_nlm4.stat.stat = NLM4_FAILED;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Test %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  if(in_nlm_grace_period())
    {
      pres->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Cancel %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  rc = nlm_process_parameters(preq,
                              arg->exclusive,
                              &arg->alock,
                              &lock,
                              ht,
                              &pentry,
                              pcontext,
                              pclient,
                              CARE_NOT, /* cancel doesn't care if owner is found */
                              &nsm_client,
                              &nlm_client,
                              &nlm_owner,
                              NULL);

  if(rc >= 0)
    {
      /* Present the error back to the client */
      pres->res_nlm4.stat.stat = (nlm4_stats)rc;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Unlock %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  if(state_cancel(pentry,
                  pcontext,
                  nlm_owner,
                  &lock,
                  pclient,
                  &state_status) != STATE_SUCCESS)
    {
      /* Cancel could fail in the FSAL and make a bit of a mess, especially if
       * we are in out of memory situation. Such an error is logged by
       * Cache Inode.
       */
      pres->res_nlm4test.test_stat.stat = nlm_convert_state_error(state_status);
    }
  else
    {
      pres->res_nlm4.stat.stat = NLM4_GRANTED;
    }

  /* Release the NLM Client and NLM Owner references we have */
  dec_nsm_client_ref(nsm_client);
  dec_nlm_client_ref(nlm_client);
  dec_state_owner_ref(nlm_owner, pclient);

  LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Cancel %s",
           lock_result_str(pres->res_nlm4.stat.stat));
  return NFS_REQ_OK;
}                               /* nlm4_Cancel */

static void nlm4_cancel_message_resp(nlm_async_queue_t *arg)
{
  if(isFullDebug(COMPONENT_NLM))
    {
      char buffer[1024];
      netobj_to_string(&arg->nlm_async_args.nlm_async_res.res_nlm4test.cookie, buffer, 1024);
      LogFullDebug(COMPONENT_NLM,
                   "Calling nlm_send_async cookie=%s status=%s",
                   buffer, lock_result_str(arg->nlm_async_args.nlm_async_res.res_nlm4.stat.stat));
    }
  nlm_send_async(NLMPROC4_CANCEL_RES,
                 arg->nlm_async_host,
                 &(arg->nlm_async_args.nlm_async_res),
                 NULL);
  nlm4_Cancel_Free(&arg->nlm_async_args.nlm_async_res);
  dec_nsm_client_ref(arg->nlm_async_host->slc_nsm_client);
  dec_nlm_client_ref(arg->nlm_async_host);
  Mem_Free(arg);
}

/* Asynchronous Message Entry Point */

/**
 * nlm4_Cancel_Message: Cancel Lock Message
 *
 *  @param parg        [IN]
 *  @param pexportlist [IN]
 *  @param pcontextp   [IN]
 *  @param pclient     [INOUT]
 *  @param ht          [INOUT]
 *  @param preq        [IN]
 *  @param pres        [OUT]
 *
 */
int nlm4_Cancel_Message(nfs_arg_t            * parg     /* IN     */ ,
                        exportlist_t         * pexport  /* IN     */ ,
                        fsal_op_context_t    * pcontext /* IN     */ ,
                        cache_inode_client_t * pclient  /* INOUT  */ ,
                        hash_table_t         * ht       /* INOUT  */ ,
                        struct svc_req       * preq     /* IN     */ ,
                        nfs_res_t            * pres     /* OUT    */ )
{
  state_nlm_client_t * nlm_client = NULL;
  state_nsm_client_t * nsm_client;
  nlm4_cancargs      * arg = &parg->arg_nlm4_cancel;
  int                  rc = NFS_REQ_OK;

  LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Cancel_Message");

  nsm_client = get_nsm_client(CARE_NO_MONITOR, preq->rq_xprt, arg->alock.caller_name);

  if(nsm_client != NULL)
    nlm_client = get_nlm_client(CARE_NO_MONITOR, preq->rq_xprt, nsm_client, arg->alock.caller_name);

  if(nlm_client == NULL)
    rc = NFS_REQ_DROP;
  else
    rc = nlm4_Cancel(parg, pexport, pcontext, pclient, ht, preq, pres);

  if(rc == NFS_REQ_OK)
    rc = nlm_send_async_res_nlm4(nlm_client, nlm4_cancel_message_resp, pres);

  if(rc == NFS_REQ_DROP)
    {
      if(nsm_client != NULL)
        dec_nsm_client_ref(nsm_client);
      if(nlm_client != NULL)
        dec_nlm_client_ref(nlm_client);
      LogCrit(COMPONENT_NLM,
            "Could not send async response for nlm_Cancel_Message");
    }

  return NFS_REQ_DROP;
}

/**
 * nlm4_Lock_Free: Frees the result structure allocated for nlm4_Lock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Cancel_Free(nfs_res_t * pres)
{
  netobj_free(&pres->res_nlm4test.cookie);
  return;
}                               /* nlm4_Cancel_Free */
