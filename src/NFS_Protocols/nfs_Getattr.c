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
 * \file	  nfs_Geattr.c 
 * \author  $Author: deniel $
 * \data    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.15 $
 * \brief   Implements NFS PROC2 GETATTR and NFS PROC3 GETATTR.
 *
 *  Implements the GETATTR function in V2 and V3. This function is used by
 *  the client to get attributes about a filehandle.
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
#include "rpc.h"
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
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Getattr: get attributes for a file
 *
 * Get attributes for a file. Implements  NFS PROC2 GETATTR and NFS PROC3 GETATTR.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs_Getattr(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Getattr";

  fsal_attrib_list_t attr;
  cache_entry_t *pentry = NULL;
  cache_inode_status_t cache_status;
  int rc = 0;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_getattr2),
                       &(parg->arg_getattr3.object),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Getattr handle: %s", str);
    }

  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_getattr2),
                                  &(parg->arg_getattr3.object),
                                  NULL,
                                  &(pres->res_attr2.status),
                                  &(pres->res_getattr3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs_Getattr returning %d", rc);
      return rc;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_getattr3.object))))
    {
      rc = nfs3_Getattr_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs_Getattr returning %d from nfs3_Getattr_Xattr", rc);
      return rc;
    }

  /*
   * Get attributes.  Use NULL for the file name since we have the
   * vnode to define the file. 
   */
  if(cache_inode_getattr(pentry,
                         &attr,
                         ht, pclient, pcontext, &cache_status) == CACHE_INODE_SUCCESS)
    {
      /*
       * Client API should be keeping us from crossing junctions,
       * but double check to be sure. 
       */

      switch (preq->rq_vers)
        {

        case NFS_V2:
          /* Copy data from vattr to Attributes */
          if(nfs2_FSALattr_To_Fattr(pexport, &attr,
                                    &(pres->res_attr2.ATTR2res_u.attributes)) == 0)
            {
              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  CACHE_INODE_INVALID_ARGUMENT,
                                  &pres->res_attr2.status,
                                  &pres->res_getattr3.status,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
              LogFullDebug(COMPONENT_NFSPROTO,
                           "nfs_Getattr set failed status v2");
              return NFS_REQ_OK;
            }
          pres->res_attr2.status = NFS_OK;
          break;

        case NFS_V3:
          if(nfs3_FSALattr_To_Fattr(pexport, &attr,
                                    &(pres->res_getattr3.GETATTR3res_u.resok.
                                      obj_attributes)) == 0)
            {
              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  CACHE_INODE_INVALID_ARGUMENT,
                                  &pres->res_attr2.status,
                                  &pres->res_getattr3.status,
                                  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

              LogFullDebug(COMPONENT_NFSPROTO,
                           "nfs_Getattr set failed status v3");
              return NFS_REQ_OK;
            }
          pres->res_getattr3.status = NFS3_OK;
          break;
        }                       /* switch */

      LogFullDebug(COMPONENT_NFSPROTO, "nfs_Getattr succeeded");
      return NFS_REQ_OK;
    }

  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      CACHE_INODE_INVALID_ARGUMENT,
                      &pres->res_attr2.status,
                      &pres->res_getattr3.status,
                      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

  LogFullDebug(COMPONENT_NFSPROTO, "nfs_Getattr set failed status");
  return NFS_REQ_OK;
}                               /* nfs_Getattr */

/**
 * nfs_Getattr_Free: Frees the result structure allocated for nfs_Getattr.
 * 
 * Frees the result structure allocated for nfs_Getattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Getattr_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Getattr_Free */
