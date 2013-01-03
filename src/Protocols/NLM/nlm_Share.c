/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
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
#include "ganesha_rpc.h"
#include "log.h"
#include "nlm4.h"
#include "sal_functions.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * nlm4_Share: Set a share reservation
 *
 *  @param parg        [IN]
 *  @param pexportlist [IN]
 *  @param pcontextp   [IN]
 *  @param ht          [INOUT]
 *  @param preq        [IN]
 *  @param pres        [OUT]
 *
 */

int nlm4_Share(nfs_arg_t            * parg,
               exportlist_t         * pexport,
               fsal_op_context_t    * pcontext,
               nfs_worker_data_t    * pworker,
               struct svc_req       * preq,
               nfs_res_t            * pres)
{
  nlm4_shareargs     * arg = &parg->arg_nlm4_share;
  cache_entry_t      * pentry;
  state_status_t       state_status = STATE_SUCCESS;
  state_nsm_client_t * nsm_client;
  state_nlm_client_t * nlm_client;
  state_owner_t      * nlm_owner;
  int                  rc;
  int                  grace = nfs_in_grace();

  if(pexport == NULL)
    {
      pres->res_nlm4share.stat = NLM4_STALE_FH;
      LogInfo(COMPONENT_NLM, "INVALID HANDLE: nlm4_Share");
      return NFS_REQ_OK;
    }

  pres->res_nlm4share.sequence = 0;

  if(isDebug(COMPONENT_NLM))
    {
      char                    buffer[NETOBJ_MAX_STRING];
      struct display_buffer   dspbuf = {sizeof(buffer), buffer, buffer};

      (void) display_netobj(&dspbuf, &arg->cookie);

      LogDebug(COMPONENT_NLM,
               "REQUEST PROCESSING: Calling nlm4_Share cookie=%s reclaim=%s",
               buffer,
               arg->reclaim ? "yes" : "no");
    }

  if(!copy_netobj(&pres->res_nlm4share.cookie, &arg->cookie))
    {
      pres->res_nlm4share.stat = NLM4_FAILED;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Share %s",
               lock_result_str(pres->res_nlm4share.stat));
      return NFS_REQ_OK;
    }

  /* Allow only reclaim share request during recovery and visa versa.
   * Note: NLM_SHARE is indicated to be non-monitored, however, it does
   * have a reclaim flag, so we will honor the reclaim flag if used.
   */
  if((grace && !arg->reclaim) ||
     (!grace && arg->reclaim))
    {
      pres->res_nlm4share.stat = NLM4_DENIED_GRACE_PERIOD;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Share %s",
               lock_result_str(pres->res_nlm4share.stat));
      return NFS_REQ_OK;
    }

  rc = nlm_process_share_parms(preq,
                               &arg->share,
                               &pentry,
                               pcontext,
                               CARE_NO_MONITOR,
                               &nsm_client,
                               &nlm_client,
                               &nlm_owner);

  if(rc >= 0)
    {
      /* Present the error back to the client */
      pres->res_nlm4share.stat = (nlm4_stats)rc;
      LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Share %s",
               lock_result_str(pres->res_nlm4share.stat));
      return NFS_REQ_OK;
    }

  if(state_nlm_share(pentry,
                     pcontext,
                     pexport,
                     arg->share.access,
                     arg->share.mode,
                     nlm_owner,
                     &state_status) != STATE_SUCCESS)
    {
      pres->res_nlm4share.stat = nlm_convert_state_error(state_status);
    }
  else
    {
      pres->res_nlm4share.stat = NLM4_GRANTED;
    }

  /* Release the NLM Client and NLM Owner references we have */
  dec_nsm_client_ref(nsm_client);
  dec_nlm_client_ref(nlm_client);
  dec_state_owner_ref(nlm_owner);
  cache_inode_put(pentry);

  LogDebug(COMPONENT_NLM, "REQUEST RESULT: nlm4_Share %s",
           lock_result_str(pres->res_nlm4share.stat));
  return NFS_REQ_OK;
}

/**
 * nlm4_Share_Free: Frees the result structure allocated for nlm4_Lock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Share_Free(nfs_res_t * pres)
{
  netobj_free(&pres->res_nlm4share.cookie);
  return;
}
