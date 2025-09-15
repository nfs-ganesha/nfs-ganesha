// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2025, IBM . All rights reserved.
 * Author: Naresh Chillarege<Naresh.Chillarege@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "log.h"
#include "gsh_rpc.h"
#include "cqos.h"
#include "nfs_core.h"

int cqos_Null(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	LogDebug(COMPONENT_NLM, "REQUEST PROCESSING: Calling CQOS_NULL");

	/* 0 is success */
	return 0;
}

/**
 * cqos_Null_Free: Frees the result structure allocated for cqos_Null
 *
 * Frees the result structure allocated for cqos_Null. Does Nothing in fact.
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void cqos_Null_Free(nfs_res_t *res)
{
	/* Nothing to do */
}
