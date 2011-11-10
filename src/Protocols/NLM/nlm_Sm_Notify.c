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
#include "nfs_proto_functions.h"
#include "nlm_util.h"

/**
 * nlm4_Sm_Notify: NSM notification
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

int nlm4_Sm_Notify(nfs_arg_t * parg /* IN     */ ,
                   exportlist_t * pexport /* IN     */ ,
                   fsal_op_context_t * pcontext /* IN     */ ,
                   cache_inode_client_t * pclient /* INOUT  */ ,
                   hash_table_t * ht /* INOUT  */ ,
                   struct svc_req *preq /* IN     */ ,
                   nfs_res_t * pres /* OUT    */ )
{
  nlm4_sm_notifyargs * arg = &parg->arg_nlm4_sm_notify;
  state_status_t       state_status = CACHE_INODE_SUCCESS;
  state_nsm_client_t * nsm_client;

  LogDebug(COMPONENT_NLM,
           "REQUEST PROCESSING: Calling nlm4_sm_notify for %s",
           arg->name);

  nsm_client = get_nsm_client(TRUE, preq->rq_xprt, arg->name);
  if(nsm_client != NULL)
    {
      /* Cast the state number into a state pointer to protect
       * locks from a client that has rebooted from being released
       * by this SM_NOTIFY.
       */
      if(state_nlm_notify(pcontext,
                          nsm_client,
                          (void *) (ptrdiff_t) arg->state,
                          pclient,
                          &state_status) != STATE_SUCCESS)
        {
          /* TODO FSF: Deal with error */
        }
    }

  dec_nsm_client_ref(nsm_client);

  return NFS_REQ_OK;
}

/**
 * nlm4_Sm_Notify_Free: Frees the result structure allocated for nlm4_Sm_Notify
 *
 * Frees the result structure allocated for nlm4_Sm_Notify. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Sm_Notify_Free(nfs_res_t * pres)
{
  return;
}
