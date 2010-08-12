/*
 * Copyright IBM Corporation, 2010
 *  Contributor: M. Mohan Kumar <mohan@in.ibm.com>
 *               Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
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
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif
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
#include "nlm4.h"
#include "nlm_async.h"
#include "nlm_send_reply.h"

static void nlm4_lock_message_resp(void *arg)
{
  int proc;
  nlm_async_res_t *res = arg;

  if(res->pres.res_nlm4.stat.stat == NLM4_GRANTED)
    {
      proc = NLMPROC4_GRANTED_MSG;
    }
  else
    {
      proc = NLMPROC4_LOCK_RES;
    }
  nlm_send_reply(proc, res->caller_name, &(res->pres), NULL);
  Mem_Free(arg);
}

/**
 * nlm4_Lock_Message: Lock Message
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
int nlm4_Lock_Message(nfs_arg_t * parg /* IN     */ ,
                      exportlist_t * pexport /* IN     */ ,
                      fsal_op_context_t * pcontext /* IN     */ ,
                      cache_inode_client_t * pclient /* INOUT  */ ,
                      hash_table_t * ht /* INOUT  */ ,
                      struct svc_req *preq /* IN     */ ,
                      nfs_res_t * pres /* OUT    */ )
{
  nlm_async_res_t *arg;
  LogFullDebug(COMPONENT_NFSPROTO,
                    "REQUEST PROCESSING: Calling nlm_Lock_Message");

  nlm4_Lock(parg, pexport, pcontext, pclient, ht, preq, pres);

  arg = nlm_build_async_res(parg->arg_nlm4_lock.alock.caller_name, pres);
  nlm_async_callback(nlm4_lock_message_resp, arg);

  return NFS_REQ_OK;

}

/**
 * nlm4_Lock_Message_Free: Frees the result structure allocated for nlm4_Lock
 *
 * Frees the result structure allocated for nlm4_Lock. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm4_Lock_Message_Free(nfs_res_t * pres)
{
  return;
}
