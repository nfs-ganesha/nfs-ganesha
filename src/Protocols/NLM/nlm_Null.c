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
#include "log.h"
#include "ganesha_rpc.h"
#include "nlm4.h"
#include "cache_inode.h"
#include "nlm_util.h"
#include "nlm_async.h"

/**
 * @brief The NLM proc null function, for all versions.
 *
 * The NLM proc null function, for all versions.
 *
 * @param[in]  parg     Ignored
 * @param[in]  pexport  Ignored
 * @param[in]  pcontext Ignored
 * @param[in]  pworker  Ignored
 * @param[in]  preq     Ignored
 * @param[out] pres     Ignored
 *
 */

int nlm_Null(nfs_arg_t *parg,
             exportlist_t *pexport,
	     struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres)
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
