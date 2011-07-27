/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * ---------------------------------------
 */

/**
 * \file    nfs2_Writecache.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.15 $
 * \brief   Implement NFSPROC2_WRITECACHE.
 *
 * Everything you wanted to know about NFSPROC2_WRITECACHE but were afraid to ask.
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
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/**
 * nfs2_Writecache: Implements NFSPROC2_WRITECACHE.
 *
 * Implements NFSPROC2_WRITECACHE
 * 
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param ht      [INOUT] cache inode hash table
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK (this routine does nothing)
 *
 */

int nfs2_Writecache(nfs_arg_t * parg,
                    exportlist_t * pexport,
                    fsal_op_context_t * pcontext,
                    cache_inode_client_t * pclient,
                    hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  /* This is an unsupported function, it is never used */
  LogCrit(COMPONENT_NFSPROTO,
          "NFS2_WRITECACHE: Received unexpected call to deprecated function NFS2PROC_WRITECACHE");
  return NFS_REQ_OK;
}

/**
 * nfs2_Writecache_Free: Frees the result structure allocated for nfs2_Writecache.
 * 
 * Frees the result structure allocated for nfs2_Root.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Writecache_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs2_Writecache_Free */
