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
 * \file    nfs_Readlink.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.13 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Readlink.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "ganesha_rpc.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Readlink: The NFS PROC2 and PROC3 READLINK.
 *
 *  Implements the NFS PROC2-3 READLINK function. 
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param creds   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @see cache_inode_readlink
 *
 *----------------------------------------------------------------------------*/

int nfs_Readlink(nfs_arg_t * parg,
                 exportlist_t * pexport,
                 struct user_cred *creds,
                 cache_inode_client_t * pclient,
                 struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Readlink";

  cache_entry_t *pentry = NULL;
  fsal_attrib_list_t attr;
  cache_inode_status_t cache_status;
  fsal_path_t symlink_data;
  char *ptr = NULL;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_readlink2),
                       &(parg->arg_readlink3.symlink),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Readlink handle: %s", str);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_readlink3.READLINK3res_u.resfail.symlink_attributes.attributes_follow =
          FALSE;
    }
  /* Convert file handle into a vnode */
  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_readlink2),
                                  &(parg->arg_readlink3.symlink),
                                  NULL,
                                  &(pres->res_readlink2.status),
                                  &(pres->res_readlink3.status),
                                  NULL, &attr, pexport, pclient, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  /* Sanity Check: the pentry must be a link */
  if(attr.type != SYMBOLIC_LINK)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_readlink2.status = NFSERR_IO;
          break;

        case NFS_V3:
          pres->res_readlink3.status = NFS3ERR_INVAL;
        }                       /* switch */
      rc = NFS_REQ_OK;
      goto out;
    }

  /* if */
  /* Perform readlink on the pentry */
  if(cache_inode_readlink(pentry,
                          &symlink_data,
                          pclient, creds, &cache_status) == CACHE_INODE_SUCCESS)
    {
      if((ptr = Mem_Alloc(symlink_data.len+1)) == NULL)
        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_readlink2.status = NFSERR_NXIO;
              break;

            case NFS_V3:
              pres->res_readlink3.status = NFS3ERR_IO;
            }                   /* switch */
          rc = NFS_REQ_OK;
          goto out;
        }

      strcpy(ptr, symlink_data.path);

      /* Reply to the client (think about Mem_Free data after use ) */
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_readlink2.READLINK2res_u.data = ptr;
          pres->res_readlink2.status = NFS_OK;
          break;

        case NFS_V3:
          pres->res_readlink3.READLINK3res_u.resok.data = ptr;
          nfs_SetPostOpAttr(pexport,
                            &attr,
                            &(pres->res_readlink3.READLINK3res_u.resok.
                              symlink_attributes));
          pres->res_readlink3.status = NFS3_OK;
          break;
        }
      rc = NFS_REQ_OK;
      goto out;
    }

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  nfs_SetFailedStatus(pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_readlink2.status,
                      &pres->res_readlink3.status,
                      pentry,
                      &(pres->res_readlink3.READLINK3res_u.resfail.symlink_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry, pclient);

  return (rc);
}                               /* nfs_Readlink */

/**
 * nfs2_Readlink_Free: Frees the result structure allocated for nfs2_Readlink.
 * 
 * Frees the result structure allocated for nfs2_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Readlink_Free(nfs_res_t * resp)
{
  if(resp->res_readlink2.status == NFS_OK)
    Mem_Free(resp->res_readlink2.READLINK2res_u.data);
}                               /* nfs2_Readlink_Free */

/**
 * nfs3_Readlink_Free: Frees the result structure allocated for nfs3_Readlink.
 * 
 * Frees the result structure allocated for nfs3_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Readlink_Free(nfs_res_t * resp)
{
  if(resp->res_readlink3.status == NFS3_OK)
    Mem_Free(resp->res_readlink3.READLINK3res_u.resok.data);
}                               /* nfs3_Readlink_Free */
