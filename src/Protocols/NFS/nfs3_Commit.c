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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 * nfs3_Commit: Implements NFSPROC3_COMMIT
 *
 * Implements NFSPROC3_COMMIT. Unused for now, all the storage is GUARDED (see write/read for details).
 * 
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK (this routine does nothing)
 *
 */

extern writeverf3 NFS3_write_verifier;  /* NFS V3 write verifier      */

int nfs3_Commit(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs3_Access";

  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *ppre_attr;
  uint64_t typeofcommit;

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
  if(nfs3_FhandleToFSAL(&(parg->arg_commit3.file), &fsal_data.handle, pcontext) == 0)
    return NFS_REQ_DROP;

  /* Set cookie to 0 */
  fsal_data.cookie = DIR_START;

  /* Get the entry in the cache_inode */
  if((pentry = cache_inode_get( &fsal_data,
                                pexport->cache_inode_policy,
                                &pre_attr, 
                                ht, 
                                pclient, 
                                pcontext, 
                                &cache_status)) == NULL)
    {
      /* Stale NFS FH ? */
      pres->res_commit3.status = NFS3ERR_STALE;
      return NFS_REQ_OK;
    }

  if((pexport->use_commit == TRUE) &&
     (pexport->use_ganesha_write_buffer == FALSE))
    typeofcommit = FSAL_UNSAFE_WRITE_TO_FS_BUFFER;
  else if((pexport->use_commit == TRUE) &&
          (pexport->use_ganesha_write_buffer == TRUE))
    typeofcommit = FSAL_UNSAFE_WRITE_TO_GANESHA_BUFFER;
  else 
    /* We only do stable writes with this export so no need to execute a commit */
    return NFS_REQ_OK;    

  /* Do not use DC if data cache is enabled, the data is kept synchronous is the DC */
  if(cache_inode_commit(pentry,
                        parg->arg_commit3.offset,
                        parg->arg_commit3.count,
                        &pre_attr,
                        ht, pclient, pcontext, typeofcommit, &cache_status) != CACHE_INODE_SUCCESS)
    {
      pres->res_commit3.status = NFS3ERR_IO;;

      nfs_SetWccData(pcontext,
                     pexport,
                     pentry,
                     ppre_attr,
                     ppre_attr, &(pres->res_commit3.COMMIT3res_u.resfail.file_wcc));

      return NFS_REQ_OK;
    }

  /* Set the pre_attr */
  ppre_attr = &pre_attr;

  nfs_SetWccData(pcontext,
                 pexport,
                 pentry,
                 ppre_attr, ppre_attr, &(pres->res_commit3.COMMIT3res_u.resok.file_wcc));

  /* Set the write verifier */
  memcpy(pres->res_commit3.COMMIT3res_u.resok.verf, NFS3_write_verifier,
         sizeof(writeverf3));
  pres->res_commit3.status = NFS3_OK;

  return NFS_REQ_OK;
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
