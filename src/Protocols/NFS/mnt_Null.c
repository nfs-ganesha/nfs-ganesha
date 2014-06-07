/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * file    mnt_Null.c
 * brief   MOUNTPROC_NULL for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_NULL in V1, V3.
 *
 */
#include "config.h"
#include "log.h"
#include "nfs_core.h"
#include "mount.h"
#include "nfs_proto_functions.h"

/**
 * @brief The Mount proc null function, for all versions.
 *
 * The MOUNT proc null function, for all versions.
 *
 * @param[in]  arg     ignored
 * @param[in]  export  ignored
 * @param[in]  worker  ignored
 * @param[in]  req     ignored
 * @param[out] res     ignored
 *
 */

int mnt_Null(nfs_arg_t *arg,
	     nfs_worker_data_t *worker,
	     struct svc_req *req, nfs_res_t *res)
{
	LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Null");
	return MNT3_OK;
}				/* mnt_Null */

/**
 * @brief Frees the result structure allocated for mnt_Null
 *
 * @param res        [INOUT]   Pointer to the result structure.
 *
 */
void mnt_Null_Free(nfs_res_t *res)
{
	return;
}
