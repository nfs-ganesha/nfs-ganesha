/*
 *
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
 * @file    nfs_creds.h
 * @brief   Prototypes for the RPC credentials used in NFS.
 *
 * nfs_creds.h : Prototypes for the RPC credentials used in NFS.
 *
 *
 */

#ifndef _NFS_CREDS_H
#define _NFS_CREDS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "ganesha_rpc.h"
#include "fsal.h"
#include "cache_inode.h"

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"

bool get_req_creds(struct svc_req *req,
		   struct req_op_context *req_ctx);

void init_credentials(struct req_op_context *req_ctx);
void clean_credentials(struct req_op_context *req_ctx);

void squash_setattr(struct req_op_context *req_ctx,
		    struct attrlist *attr);

int nfs4_MakeCred(compound_data_t *);

cache_inode_status_t nfs_access_op(cache_entry_t *entry,
				   uint32_t requested_access,
				   uint32_t *granted_access,
				   uint32_t *supported_access,
				   struct req_op_context *req_ctx);

#endif				/* _NFS_CREDS_H */
