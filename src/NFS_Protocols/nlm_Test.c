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
 * nlm4_Test: Test lock
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

int nlm4_Test(nfs_arg_t * parg /* IN     */ ,
              exportlist_t * pexport /* IN     */ ,
              fsal_op_context_t * pcontext /* IN     */ ,
              cache_inode_client_t * pclient /* INOUT  */ ,
              hash_table_t * ht /* INOUT  */ ,
              struct svc_req *preq /* IN     */ ,
              nfs_res_t * pres /* OUT    */ )
{
  nlm4_testargs            * arg = &parg->arg_nlm4_test;
  cache_entry_t            * pentry;
  cache_inode_status_t       cache_status;
  char                       buffer[MAXNETOBJ_SZ * 2];
  cache_inode_nlm_client_t * nlm_client;
  cache_lock_owner_t       * nlm_owner, * holder;
  cache_lock_desc_t          lock, conflict;
  int                        rc;

  netobj_to_string(&arg->cookie, buffer, 1024);
  LogDebug(COMPONENT_NLM,
           "REQUEST PROCESSING: Calling nlm4_Test svid=%d off=%llx len=%llx cookie=%s",
           (int) arg->alock.svid,
           (unsigned long long) arg->alock.l_offset,
           (unsigned long long) arg->alock.l_len,
           buffer);

  if(!copy_netobj(&pres->res_nlm4test.cookie, &arg->cookie))
    {
      pres->res_nlm4test.test_stat.stat = NLM4_FAILED;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Test %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  if(in_nlm_grace_period())
    {
      pres->res_nlm4test.test_stat.stat = NLM4_DENIED_GRACE_PERIOD;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Test %s",
               lock_result_str(pres->res_nlm4.stat.stat));
      return NFS_REQ_OK;
    }

  /* TODO FSF:
   *
   * TEST passes TRUE for care because we do need a non-NULL owner,  but
   * we could expand the options to allow for a "free" owner to be
   * returned, that doesn't need to be in the hash table, so if the
   * owner isn't found in the Hash table, don't add it, just return
   * the "free" owner.
   */
  rc = nlm_process_parameters(preq,
                              arg->exclusive,
                              &arg->alock,
                              &lock,
                              ht,
                              &pentry,
                              pcontext,
                              pclient,
                              TRUE,
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

  if(cache_inode_test(pentry,
                      pcontext,
                      nlm_owner,
                      &lock,
                      &holder,
                      &conflict,
                      pclient,
                      &cache_status) != CACHE_INODE_SUCCESS)
    {
      pres->res_nlm4test.test_stat.stat = nlm_convert_cache_inode_error(cache_status);

      if(cache_status == CACHE_INODE_LOCK_CONFLICT)
        {
          nlm_process_conflict(&pres->res_nlm4test.test_stat.nlm4_testrply_u.holder,
                               holder,
                               &conflict);
        }
    }
  else
    {
      pres->res_nlm4.stat.stat = NLM4_GRANTED;
    }

  /* Release the NLM Client and NLM Owner references we have */
  dec_nlm_client_ref(nlm_client);
  dec_nlm_owner_ref(nlm_owner);

  LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Test %s",
           lock_result_str(pres->res_nlm4.stat.stat));
  return NFS_REQ_OK;
}

static void nlm4_test_message_resp(void *arg)
{
  nlm_async_res_t *pres = arg;

  if(isFullDebug(COMPONENT_NLM))
    {
      char buffer[1024];
      netobj_to_string(&pres->pres.res_nlm4test.cookie, buffer, 1024);
      LogFullDebug(COMPONENT_NLM,
                   "nlm4_test_message_resp calling nlm_send_async cookie=%s status=%s",
                   buffer, lock_result_str(pres->pres.res_nlm4.stat.stat));
    }
  nlm_send_async(NLMPROC4_TEST_RES, pres->caller_name, &(pres->pres), NULL);
  nlm4_Test_Free(&pres->pres);
  Mem_Free(pres->caller_name);
  Mem_Free(pres);
}

/**
 * nlm4_Test_Message: Test lock Message
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

int nlm4_Test_Message(nfs_arg_t * parg /* IN     */ ,
                      exportlist_t * pexport /* IN     */ ,
                      fsal_op_context_t * pcontext /* IN     */ ,
                      cache_inode_client_t * pclient /* INOUT  */ ,
                      hash_table_t * ht /* INOUT  */ ,
                      struct svc_req *preq /* IN     */ ,
                      nfs_res_t * pres /* OUT    */ )
{
  struct nlm_async_res *arg;
  int rc;
  LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Test_Message");
  rc = nlm4_Test(parg, pexport, pcontext, pclient, ht, preq, pres);
  if(rc == NFS_REQ_OK)
    {
      arg = nlm_build_async_res_nlm4test(parg->arg_nlm4_test.alock.caller_name, pres);
      if(arg != NULL)
        nlm_async_callback(nlm4_test_message_resp, arg);
    }

  return NFS_REQ_DROP;
}

/**
 * nlm_Test_Free: Frees the result structure allocated for nlm4_Test
 *
 * Frees the result structure allocated for nlm_Null. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Test_Free(nfs_res_t * pres)
{
  netobj_free(&pres->res_nlm4test.cookie);
  if(pres->res_nlm4test.test_stat.stat == NLM4_DENIED)
    netobj_free(&pres->res_nlm4test.test_stat.nlm4_testrply_u.holder.oh);
  return;
}
