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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *
 */

#include "config.h"
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
 * @param[in]  pworker  Ignored
 * @param[in]  preq     Ignored
 * @param[out] pres     Ignored
 *
 */

int nlm_Null(nfs_arg_t *arg,
	     nfs_worker_data_t *worker,
	     struct svc_req *req, nfs_res_t *res)
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
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void nlm_Null_Free(nfs_res_t *res)
{
	return;
}
