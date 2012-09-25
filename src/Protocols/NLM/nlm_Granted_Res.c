/*
 * Copyright IBM Corporation, 2010
 *  Contributor: M. Mohan Kumar <mohan@in.ibm.com>
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nlm4.h"
#include "sal_data.h"
#include "cache_inode.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * nlm4_Granted_Res: Lock Granted Result Handler
 *
 * @param[in]  parg
 * @param[in]  pexport
 * @param[in]  dummy_pcontext
 * @param[in]  pworker
 * @param[in]  preq
 * @param[out] pres
 *
 */
int nlm4_Granted_Res(nfs_arg_t *parg,
                     exportlist_t *pexport,
                     fsal_op_context_t *dummy_pcontext,
                     nfs_worker_data_t *pworker,
                     struct svc_req *preq,
                     nfs_res_t *pres)
{
  nlm4_res             * arg = &parg->arg_nlm4_res;
  char                   buffer[1024];
  state_status_t         state_status = STATE_SUCCESS;
  state_cookie_entry_t * cookie_entry;
  fsal_op_context_t      context, * pcontext = &context;

  netobj_to_string(&arg->cookie, buffer, 1024);
  LogDebug(COMPONENT_NLM,
           "REQUEST PROCESSING: Calling nlm_Granted_Res cookie=%s",
           buffer);

  if(state_find_grant(arg->cookie.n_bytes,
                      arg->cookie.n_len,
                      &cookie_entry,
                      &state_status) != STATE_SUCCESS)
    {
      /* This must be an old NLM_GRANTED_RES */
      LogFullDebug(COMPONENT_NLM,
                   "Could not find cookie=%s (must be an old NLM_GRANTED_RES)",
                   buffer);
      return NFS_REQ_OK;
    }

  PTHREAD_RWLOCK_WRLOCK(&cookie_entry->sce_pentry->state_lock);

  if(cookie_entry->sce_lock_entry == NULL ||
     cookie_entry->sce_lock_entry->sle_block_data == NULL ||
     !nlm_block_data_to_fsal_context(cookie_entry->sce_lock_entry->sle_block_data,
                                     pcontext))
    {
      /* This must be an old NLM_GRANTED_RES */
      PTHREAD_RWLOCK_UNLOCK(&cookie_entry->sce_pentry->state_lock);
      LogFullDebug(COMPONENT_NLM,
                   "Could not find block data for cookie=%s (must be an old NLM_GRANTED_RES)",
                   buffer);
      return NFS_REQ_OK;
    }

  PTHREAD_RWLOCK_UNLOCK(&cookie_entry->sce_pentry->state_lock);

  if(arg->stat.stat != NLM4_GRANTED)
    {
      LogMajor(COMPONENT_NLM,
               "Granted call failed due to client error, releasing lock");
      if(state_release_grant(pcontext,
                             cookie_entry,
                             &state_status) != STATE_SUCCESS)
        {
          LogDebug(COMPONENT_NLM,
                   "cache_inode_release_grant failed");
        }
    }
  else
    {
      state_complete_grant(pcontext, cookie_entry);
      nlm_signal_async_resp(cookie_entry);
    }

  return NFS_REQ_OK;
}

/**
 * nlm4_Granted_Res_Free: Frees the result structure allocated for
 * nlm4_Granted_Res
 *
 * Frees the result structure allocated for nlm4_Granted_Res. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Granted_Res_Free(nfs_res_t * pres)
{
  return;
}
