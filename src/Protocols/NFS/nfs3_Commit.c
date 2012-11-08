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
 * \file    nfs3_Commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.10 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Commit.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 * @brief Implements NFSPROC3_COMMIT
 *
 * Implements NFSPROC3_COMMIT.
 *
 * @param[in]  parg     NFS arguments union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pworker  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_Commit(nfs_arg_t *parg,
                exportlist_t *pexport,
                fsal_op_context_t *pcontext,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres)
{
  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *ppre_attr;
  uint64_t typeofcommit;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      sprint_fhandle3(str, &(parg->arg_commit3.file));
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs3_Commit handle: %s", str);
    }

  /* to avoid setting it on each error case */
  pres->res_commit3.COMMIT3res_u.resfail.file_wcc.before.attributes_follow = FALSE;
  pres->res_commit3.COMMIT3res_u.resfail.file_wcc.after.attributes_follow = FALSE;
  ppre_attr = NULL;

  /* Convert file handle into a fsal_handle */
  if(nfs3_FhandleToFSAL(&(parg->arg_commit3.file), &fsal_data.fh_desc, pcontext) == 0)
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  /* Get the entry in the cache_inode */
  if((pentry = cache_inode_get(&fsal_data,
                               &pre_attr,
                               pcontext,
                               NULL,
                               &cache_status)) == NULL)
    {
      /* Stale NFS FH ? */
      pres->res_commit3.status = NFS3ERR_STALE;
      rc = NFS_REQ_OK;
      goto out;
    }

  if((pexport->use_commit == TRUE) &&
     (pexport->use_ganesha_write_buffer == FALSE))
    typeofcommit = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;
  else if((pexport->use_commit == TRUE) &&
          (pexport->use_ganesha_write_buffer == TRUE))
    typeofcommit = CACHE_INODE_UNSAFE_WRITE_TO_GANESHA_BUFFER;
  else
    {
      /* We only do stable writes with this export so no need to execute a commit */
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Do not use DC if data cache is enabled, the data is kept synchronous is the DC */
  if(cache_inode_commit(pentry,
                        parg->arg_commit3.offset,
                        parg->arg_commit3.count,
                        typeofcommit,
                        pcontext,
                        &cache_status) != CACHE_INODE_SUCCESS)
    {
      pres->res_commit3.status = nfs3_Errno(cache_status);

      nfs_SetWccData(pexport,
                     ppre_attr,
                     ppre_attr, &(pres->res_commit3.COMMIT3res_u.resfail.file_wcc));

      rc = NFS_REQ_OK;
      goto out;
    }

  /* Set the pre_attr */
  ppre_attr = &pre_attr;

  nfs_SetWccData(pexport,
                 ppre_attr, ppre_attr, &(pres->res_commit3.COMMIT3res_u.resok.file_wcc));

  /* Set the write verifier */
  memcpy(pres->res_commit3.COMMIT3res_u.resok.verf, NFS3_write_verifier,
         sizeof(writeverf3));
  pres->res_commit3.status = NFS3_OK;

 out:

  if (pentry)
    {
      cache_inode_put(pentry);
    }

  return rc;
}                               /* nfs3_Commit */

/**
 * nfs3_Commit_Free: Frees the result structure allocated for nfs3_Commit.
 * 
 * Frees the result structure allocated for nfs3_Commit.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Commit_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs3_Commit_Free */
