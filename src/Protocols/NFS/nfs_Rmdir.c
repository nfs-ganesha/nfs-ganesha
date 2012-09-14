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
 * \file    nfs_Rmdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.13 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Rmdir.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * @brief The NFS PROC2 and PROC3 RMDIR
 *
 * Implements the NFS PROC RMDIR function (for V2 and V3).
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

int nfs_Rmdir(nfs_arg_t *parg,
              exportlist_t *pexport,
              fsal_op_context_t *pcontext,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres)
{
  cache_entry_t *parent_pentry = NULL;
  cache_entry_t *pentry_child = NULL;
  fsal_attrib_list_t pre_parent_attr;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t pentry_child_attr;
  cache_inode_file_type_t filetype;
  cache_inode_file_type_t childtype;
  cache_inode_status_t cache_status;
  fsal_name_t name;
  char *dir_name = NULL;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          dir_name = parg->arg_rmdir2.name;
          break;
        case NFS_V3:
          dir_name = parg->arg_rmdir3.object.name;
          break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_rmdir2.dir),
                       &(parg->arg_rmdir3.object.dir),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Rmdir handle: %s name: %s",
               str, dir_name);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
      pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  /* Convert file handle into a pentry */
  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_rmdir2.dir),
                                         &(parg->arg_rmdir3.object.dir),
                                         NULL,
                                         &(pres->res_stat2),
                                         &(pres->res_rmdir3.status),
                                         NULL,
                                         &pre_parent_attr,
                                         pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_parent_attr;

  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert(pre_parent_attr.type);

  /*
   * Sanity checks: new directory name must be non-null; parent must be
   * a directory. 
   */
  if(filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_stat2 = NFSERR_NOTDIR;
          break;
        case NFS_V3:
          pres->res_rmdir3.status = NFS3ERR_NOTDIR;
          break;
        }

      rc = NFS_REQ_OK;
      goto out;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      dir_name = parg->arg_rmdir2.name;
      break;

    case NFS_V3:
      dir_name = parg->arg_rmdir3.object.name;
      break;

    }

  //if(dir_name == NULL || strlen(dir_name) == 0)
  if(dir_name == NULL || *dir_name == '\0' )
    {
      cache_status = CACHE_INODE_INVALID_ARGUMENT;      /* for lack of better... */
    }
  else
    {
      if((cache_status = cache_inode_error_convert(FSAL_str2name(dir_name,
                                                                 FSAL_MAX_NAME_LEN,
                                                                 &name))) ==
         CACHE_INODE_SUCCESS)
        {
          /*
           * Lookup to the entry to be removed to check if it is a directory
           */
          if((pentry_child = cache_inode_lookup(parent_pentry,
                                                &name,
                                                &pentry_child_attr,
                                                pcontext,
                                                &cache_status)) != NULL)
            {
              /* Extract the filetype */
              childtype = cache_inode_fsal_type_convert(pentry_child_attr.type);

              /*
               * Sanity check: make sure we are about to remove a directory
               */
              if(childtype != DIRECTORY)
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFSERR_NOTDIR;
                      break;

                    case NFS_V3:
                      pres->res_rmdir3.status = NFS3ERR_NOTDIR;
                      break;
                    }
                  rc = NFS_REQ_OK;
                  goto out;
                }

              /*
               * Remove the directory.  Use NULL vnode for the directory
               * that's being removed because we know the directory's name. 
               */

              if(cache_inode_remove(parent_pentry,
                                    &name,
                                    &parent_attr,
                                    pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFS_OK;
                      break;

                    case NFS_V3:
                      /* Build Weak Cache Coherency data */
                      nfs_SetWccData(pexport,
                                     ppre_attr,
                                     &parent_attr,
                                     &(pres->res_rmdir3.RMDIR3res_u.resok.dir_wcc));

                      pres->res_rmdir3.status = NFS3_OK;
                      break;
                    }
                  rc = NFS_REQ_OK;
                  goto out;
                }
            }
        }
    }

  /* If we are here, there was an error */
  rc = nfs_SetFailedStatus(pexport, preq->rq_vers, cache_status,
                           &pres->res_stat2, &pres->res_rmdir3.status,
                           NULL, ppre_attr,
                           &(pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc),
                           NULL, NULL);
out:
  /* return references */
  if (pentry_child)
      cache_inode_put(pentry_child);

  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);

}                               /* nfs_Rmdir */

/**
 * nfs_Rmdir_Free: Frees the result structure allocated for nfs_Rmdir.
 * 
 * Frees the result structure allocated for nfs_Rmdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Rmdir_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Rmdir_Free */
