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
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <assert.h>
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
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
#include "nlm_async.h"

/**
 * nlm4_Unlock: Set a range lock
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

int nlm4_Unlock(nfs_arg_t * parg /* IN     */ ,
                exportlist_t * pexport /* IN     */ ,
                fsal_op_context_t * pcontext /* IN     */ ,
                cache_inode_client_t * pclient /* INOUT  */ ,
                hash_table_t * ht /* INOUT  */ ,
                struct svc_req *preq /* IN     */ ,
                nfs_res_t * pres /* OUT    */ )
{
  nlm4_unlockargs          * arg = &parg->arg_nlm4_unlock;
  cache_entry_t            * pentry;
  cache_inode_status_t       cache_status;
  char                       buffer[MAXNETOBJ_SZ * 2];
  cache_inode_nlm_client_t * nlm_client;
  cache_lock_owner_t       * nlm_owner;
  cache_lock_desc_t          lock;
  int                        rc;

  netobj_to_string(&arg->cookie, buffer, sizeof(buffer));
  LogDebug(COMPONENT_NLM,
           "REQUEST PROCESSING: Calling nlm4_Unlock svid=%d off=%llx len=%llx cookie=%s",
           (int) arg->alock.svid,
           (unsigned long long) arg->alock.l_offset,
           (unsigned long long) arg->alock.l_len,
           buffer);

  copy_netobj(&pres->res_nlm4test.cookie, &arg->cookie);
  if(in_nlm_grace_period())
    {
      pres->res_nlm4.stat.stat = NLM4_DENIED_GRACE_PERIOD;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Unlock %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  rc = process_nlm_parameters(preq,
                              FALSE, /* exlcusive doesn't matter */
                              &arg->alock,
                              &lock,
                              ht,
                              &pentry,
                              pcontext,
                              pclient,
                              FALSE, /* unlock doesn't care if owner is found */
                              &nlm_client,
                              &nlm_owner);

  if(rc >= 0)
    {
      /* Present the error back to the client */
      pres->res_nlm4.stat.stat = (nlm4_stats)rc;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Unlock %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  if(cache_inode_unlock(pentry,
                        pcontext,
                        nlm_owner,
                        &lock,
                        pclient,
                        &cache_status) != CACHE_INODE_SUCCESS)
    {
      /* Deal with failure to unlock */
    }
  else
    {
      pres->res_nlm4.stat.stat = NLM4_GRANTED;
    }

  /* Release the NLM Client and NLM Owner references we have */
  dec_nlm_client_ref(nlm_client);
  dec_nlm_owner_ref(nlm_owner);

  LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Unlock %s",
           lock_result_str(pres->res_nlm4.stat.stat));
  return NFS_REQ_OK;
}

static void nlm4_unlock_message_resp(void *arg)
{
  nlm_async_res_t *pres = arg;

  if(isFullDebug(COMPONENT_NLM))
    {
      char buffer[1024];
      netobj_to_string(&pres->pres.res_nlm4test.cookie, buffer, 1024);
      LogFullDebug(COMPONENT_NLM,
                   "nlm4_unlock_message_resp calling nlm_send_async cookie=%s status=%s",
                   buffer, lock_result_str(pres->pres.res_nlm4.stat.stat));
    }
  nlm_send_async(NLMPROC4_UNLOCK_RES, pres->caller_name, &(pres->pres), NULL);
  nlm4_Unlock_Free(&pres->pres);
  Mem_Free(pres->caller_name);
  Mem_Free(pres);
}

/**
 * nlm4_Unlock_Message: Unlock Message
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
int nlm4_Unlock_Message(nfs_arg_t * parg /* IN     */ ,
                        exportlist_t * pexport /* IN     */ ,
                        fsal_op_context_t * pcontext /* IN     */ ,
                        cache_inode_client_t * pclient /* INOUT  */ ,
                        hash_table_t * ht /* INOUT  */ ,
                        struct svc_req *preq /* IN     */ ,
                        nfs_res_t * pres /* OUT    */ )
{
  nlm_async_res_t *arg;
  LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Unlock_Message");
  nlm4_Unlock(parg, pexport, pcontext, pclient, ht, preq, pres);

  arg = nlm_build_async_res_nlm4(parg->arg_nlm4_unlock.alock.caller_name, pres);
  if(arg != NULL)
    nlm_async_callback(nlm4_unlock_message_resp, arg);
  return NFS_REQ_DROP;
}

/**
 * nlm4_Unlock_Free: Frees the result structure allocated for nlm4_Unlock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Unlock_Free(nfs_res_t * pres)
{
  netobj_free(&pres->res_nlm4test.cookie);
  return;
}
