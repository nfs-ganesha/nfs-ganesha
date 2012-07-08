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
 * \file    nfs2_Root.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs2_Root.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/**
 * @brief Implements NFSPROC2_ROOT.
 *
 * Implements NFSPROC2_ROOT.
 *
 * @param[in]  parg     NFS argument union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pworker  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK (this routine does nothing)
 *
 */
int nfs2_Root(nfs_arg_t *parg,
              exportlist_t *pexport,
	      struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres)
{
  /* This is an unsupported function, it is never used */
  LogCrit(COMPONENT_NFSPROTO,
          "NFS2_ROOT: Received unexpected call to deprecated function NFS2PROC_ROOT");
  return NFS_REQ_OK;
}                               /* nfs2_Root */

/**
 * nfs2_Root_Free: Frees the result structure allocated for nfs2_Root.
 * 
 * Frees the result structure allocated for nfs2_Root.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Root_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs2_Root_Free */
