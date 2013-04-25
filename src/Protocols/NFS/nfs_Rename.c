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
 * \file    nfs_Rename.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.16 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Rename.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * @brief The NFS PROC2 and PROC3 RENAME
 *
 * Implements the NFS PROC RENAME function (for V2 and V3).
 *
 * @param[in]  parg     NFS argument union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pclient  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successful
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs_Rename(nfs_arg_t *parg,
               exportlist_t *pexport,
               fsal_op_context_t *pcontext,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres)
{
  char *str_entry_name = NULL;
  fsal_name_t entry_name;
  char *str_new_entry_name = NULL;
  fsal_name_t new_entry_name;
  cache_entry_t *parent_pentry = NULL;
  cache_entry_t *new_parent_pentry = NULL;
  cache_inode_status_t cache_status;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *pnew_pre_attr;
  fsal_attrib_list_t new_parent_attr;
  int rc = NFS_REQ_OK;
  short to_exportid = 0;
  short from_exportid = 0;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char                  strto[LEN_FH_STR];
      char                  strfrom[LEN_FH_STR];
      struct display_buffer dspbuf_to = {sizeof(strto), strto, strto};
      struct display_buffer dspbuf_from = {sizeof(strfrom), strfrom, strfrom};

      switch (preq->rq_vers)
        {
        case NFS_V2:
            str_entry_name = parg->arg_rename2.from.name;
            str_new_entry_name = parg->arg_rename2.to.name;
            (void) display_fhandle2(&dspbuf_from, &(parg->arg_rename2.from.dir));
            (void) display_fhandle2(&dspbuf_to, &(parg->arg_rename2.to.dir));
            break;

        case NFS_V3:
            str_entry_name = parg->arg_rename3.from.name;
            str_new_entry_name = parg->arg_rename3.to.name;
            (void) display_fhandle3(&dspbuf_from, &(parg->arg_rename3.from.dir));
            (void) display_fhandle3(&dspbuf_to, &(parg->arg_rename3.to.dir));
            break;
        }

      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Rename from handle: %s name %s to handle: %s name: %s",
               strfrom, str_entry_name, strto, str_new_entry_name);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_rename3.RENAME3res_u.resfail.fromdir_wcc.before.attributes_follow = FALSE;
      pres->res_rename3.RENAME3res_u.resfail.fromdir_wcc.after.attributes_follow = FALSE;
      pres->res_rename3.RENAME3res_u.resfail.todir_wcc.before.attributes_follow = FALSE;
      pres->res_rename3.RENAME3res_u.resfail.todir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
      pnew_pre_attr = NULL;
    }

  /* Get the exportids for the two handles. */
  switch (preq->rq_vers)
    {
    case NFS_V2:
      {
        to_exportid = nfs2_FhandleToExportId(&(parg->arg_rename2.to.dir));
        from_exportid = nfs2_FhandleToExportId(&(parg->arg_rename2.from.dir));
        break;
      }
    case NFS_V3:
      {
        to_exportid = nfs3_FhandleToExportId(&(parg->arg_rename3.to.dir));
        from_exportid = nfs3_FhandleToExportId(&(parg->arg_rename3.from.dir));
        break;
      }
    }

  /* Validate the to_exportid */
  if(to_exportid < 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "NFS%d RENAME Request from client %s has badly formed handle for to dir",
              preq->rq_vers, pworker->hostaddr_str);

      /* Bad handle, report to client */
      if(preq->rq_vers == NFS_V2)
        pres->res_dirop2.status = NFSERR_STALE;
      else
        pres->res_rename3.status = NFS3ERR_BADHANDLE;
      goto out;
    }

  if(nfs_Get_export_by_id(nfs_param.pexportlist,
                          to_exportid) == NULL)
    {
      LogInfo(COMPONENT_DISPATCH,
              "NFS%d RENAME Request from client %s has invalid export %d for to dir",
              preq->rq_vers, pworker->hostaddr_str,
              to_exportid);

      /* Bad export, report to client */
      if(preq->rq_vers == NFS_V2)
        pres->res_dirop2.status = NFSERR_STALE;
      else
        pres->res_rename3.status = NFS3ERR_STALE;
      goto out;
    }

  /*
   * Both objects have to be in the same filesystem 
   */

  if(to_exportid != from_exportid)
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_dirop2.status = NFSERR_PERM;
      if(preq->rq_vers == NFS_V3)
        pres->res_rename3.status = NFS3ERR_XDEV;
      goto out;
    }

  /* Convert fromdir file handle into a cache_entry */
  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_rename2.from.dir),
                                         &(parg->arg_rename3.from.dir),
                                         NULL,
                                         &(pres->res_dirop2.status),
                                         &(pres->res_rename3.status),
                                         NULL,
                                         &pre_attr, pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  /* Convert todir file handle into a cache_entry */
  if((new_parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                             &(parg->arg_rename2.to.dir),
                                             &(parg->arg_rename3.to.dir),
                                             NULL,
                                             &(pres->res_dirop2.status),
                                             &(pres->res_rename3.status),
                                             NULL,
                                             &new_parent_attr,
                                             pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  /* get the attr pointers */
  ppre_attr = &pre_attr;
  pnew_pre_attr = &new_parent_attr;

  /* Get the from and to names */
  switch (preq->rq_vers)
    {
    case NFS_V2:
      str_entry_name = parg->arg_rename2.from.name;
      str_new_entry_name = parg->arg_rename2.to.name;
      break;

    case NFS_V3:
      str_entry_name = parg->arg_rename3.from.name;
      str_new_entry_name = parg->arg_rename3.to.name;
      break;
    }

  if(str_entry_name == NULL ||
     *str_entry_name == '\0' ||
     str_new_entry_name == NULL ||
     *str_new_entry_name == '\0' ||
     FSAL_IS_ERROR(FSAL_str2name(str_entry_name, 0, &entry_name)) ||
     FSAL_IS_ERROR(FSAL_str2name(str_new_entry_name, 0, &new_entry_name)))
    {
      cache_status = CACHE_INODE_INVALID_ARGUMENT;
    }
  else
    {
      if(cache_inode_rename(parent_pentry,
                            &entry_name,
                            new_parent_pentry,
                            &new_entry_name,
                            pcontext,
                            &cache_status) == CACHE_INODE_SUCCESS)
        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_dirop2.status = NFS_OK;
              break;

            case NFS_V3:
              /*
               * Build Weak Cache Coherency
               * data 
               */
              if(cache_inode_lock_trust_attrs(parent_pentry,
                                              pcontext,
                                              FALSE) == CACHE_INODE_SUCCESS)
              {
                nfs_SetWccData(pexport,
                               ppre_attr,
                               &parent_pentry->attributes,
                               &(pres->res_rename3.RENAME3res_u.resok.fromdir_wcc));

                PTHREAD_RWLOCK_UNLOCK(&parent_pentry->attr_lock);
              }

              if(cache_inode_lock_trust_attrs(new_parent_pentry,
                                              pcontext,
                                              FALSE) == CACHE_INODE_SUCCESS)
              {
                nfs_SetWccData(pexport,
                               pnew_pre_attr,
                               &new_parent_pentry->attributes,
                               &(pres->res_rename3.RENAME3res_u.resok.todir_wcc));

                PTHREAD_RWLOCK_UNLOCK(&new_parent_pentry->attr_lock);
              }

              pres->res_rename3.status = NFS3_OK;
              break;
            }

          rc = NFS_REQ_OK;
          goto out;
        }
    }

  /* If we are here, there was an error */
  rc = nfs_SetFailedStatus(pexport, preq->rq_vers, cache_status,
                           &pres->res_dirop2.status, &pres->res_rename3.status,
                           NULL, ppre_attr,
                           &(pres->res_rename3.RENAME3res_u.resfail.fromdir_wcc),
                           pnew_pre_attr,
                           &(pres->res_rename3.RENAME3res_u.resfail.todir_wcc));
out:
  if (parent_pentry)
      cache_inode_put(parent_pentry);

  if (new_parent_pentry)
      cache_inode_put(new_parent_pentry);

  return (rc);

}                               /* nfs_Rename */

/**
 * nfs_Rename_Free: Frees the result structure allocated for nfs_Rename.
 * 
 * Frees the result structure allocated for nfs_Rename.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Rename_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Rename_Free */
