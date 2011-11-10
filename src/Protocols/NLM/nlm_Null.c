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
#include "cache_inode.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * nlm_Null: The Mount proc null function, for all versions.
 *
 * The MOUNT proc null function, for all versions.
 *
 *  @param parg        [IN]    ignored
 *  @param pexportlist [IN]    ignored
 *  @param pcontextp   [IN]    ignored
 *  @param pclient     [INOUT] ignored
 *  @param ht          [INOUT] ignored
 *  @param preq        [IN]    ignored
 *  @param pres        [OUT]   ignored
 *
 */

int nlm_Null(nfs_arg_t * parg /* IN     */ ,
             exportlist_t * pexport /* IN     */ ,
             fsal_op_context_t * pcontext /* IN     */ ,
             cache_inode_client_t * pclient /* INOUT  */ ,
             hash_table_t * ht /* INOUT  */ ,
             struct svc_req *preq /* IN     */ ,
             nfs_res_t * pres /* OUT    */ )
{
  LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling nlm_Null");
  /* 0 is success */
  return 0;
}

/**
 * nlm_Null_Free: Frees the result structure allocated for nlm_Null
 *
 * Frees the result structure allocated for nlm_Null. Does Nothing in fact.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nlm_Null_Free(nfs_res_t * pres)
{
  return;
}
