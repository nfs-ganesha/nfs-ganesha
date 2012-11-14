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
 * \file    nfs_Create.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Create.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 *
 * @brief The NFS PROC2 and PROC3 CREATE
 *
 * Implements the NFS PROC CREATE function (for V2 and V3).
 *
 * @param[in]  parg     NFS arguments union
 * @param[in]  pexport  NFS export list
 * @param[in]  pcontext Credentials to be used for this request
 * @param[in]  pworker  Worker thread data
 * @param[in]  preq     SVC request related to this call
 * @param[out] pres     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs_Create(nfs_arg_t *parg,
               exportlist_t *pexport,
               fsal_op_context_t *pcontext,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres)
{
  char *str_file_name = NULL;
  fsal_name_t file_name;
  fsal_accessmode_t mode = 0;
  cache_entry_t *file_pentry = NULL;
  cache_entry_t *parent_pentry = NULL;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_parent_after;
  fsal_attrib_list_t attr_newfile;
  fsal_attrib_list_t attributes_create;
  fsal_attrib_list_t *ppre_attr;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_inode_status_t cache_status_lookup;
  cache_inode_file_type_t parent_filetype;
  int rc = NFS_REQ_OK;
#ifdef _USE_QUOTA
  fsal_status_t fsal_status ;
#endif

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          str_file_name = parg->arg_create2.where.name;
          break;
        case NFS_V3:
          str_file_name = parg->arg_create3.where.name;
          break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_create2.where.dir),
                       &(parg->arg_create3.where.dir),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Create handle: %s name: %s",
               str, str_file_name);
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_create3.where.dir))))
    {
      rc = nfs3_Create_Xattr(parg, pexport, pcontext, preq, pres);
      goto out;
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_create3.CREATE3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
      pres->res_create3.CREATE3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_create2.where.dir),
                                         &(parg->arg_create3.where.dir),
                                         NULL,
                                         &(pres->res_dirop2.status),
                                         &(pres->res_create3.status),
                                         NULL,
                                         &parent_attr,
                                         pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &parent_attr;

  /* Extract the filetype */
  parent_filetype = cache_inode_fsal_type_convert(parent_attr.type);

  /*
   * Sanity checks: new file name must be non-null; parent must be a
   * directory. 
   */
  if(parent_filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_dirop2.status = NFSERR_NOTDIR;
          break;

        case NFS_V3:
          pres->res_create3.status = NFS3ERR_NOTDIR;
          break;
        }

      rc = NFS_REQ_OK;
      goto out;
    }

  switch (preq->rq_vers)
    {
    case NFS_V2:
      str_file_name = parg->arg_create2.where.name;

      if(parg->arg_create2.attributes.mode != (unsigned int)-1)
        {
          mode = unix2fsal_mode(parg->arg_create2.attributes.mode);
        }
      else
        {
          mode = 0;
        }

      break;

    case NFS_V3:
      str_file_name = parg->arg_create3.where.name;
      if(parg->arg_create3.how.mode == EXCLUSIVE)
        {
          /*
           * Client has not provided mode information.
           * If the create works, the client will issue
           * a separate setattr request to fix up the
           * file's mode, so pick arbitrary value for now.
           */

          mode = 0;
        }
      else if(parg->arg_create3.how.createhow3_u.obj_attributes.mode.set_it == TRUE)
        mode =
            unix2fsal_mode(parg->arg_create3.how.createhow3_u.obj_attributes.mode.
                           set_mode3_u.mode);
      else
        mode = 0;
      break;
    }

#ifdef _USE_QUOTA
    /* if quota support is active, then we should check is the FSAL allows inode creation or not */
    fsal_status = FSAL_check_quota( pexport->fullpath, 
                                    FSAL_QUOTA_INODES,
                                    FSAL_OP_CONTEXT_TO_UID( pcontext ) ) ;
    if( FSAL_IS_ERROR( fsal_status ) )
     {

       switch (preq->rq_vers)
         {
           case NFS_V2:
             pres->res_dirop2.status = NFSERR_DQUOT ;
             break;

           case NFS_V3:
             pres->res_create3.status = NFS3ERR_DQUOT;
             break;
         }

       rc = NFS_REQ_OK ;
       goto out;
     }
#endif /* _USE_QUOTA */

  if(str_file_name == NULL || *str_file_name == '\0' )
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_dirop2.status = NFSERR_IO;
      if(preq->rq_vers == NFS_V3)
        pres->res_create3.status = NFS3ERR_INVAL;
    }
  else
    {
      if((cache_status = cache_inode_error_convert(FSAL_str2name(str_file_name,
                                                                 0,
                                                                 &file_name))) ==
         CACHE_INODE_SUCCESS)
        {
          /*
           * Lookup file to see if it exists.  If so, use it.  Otherwise
           * create a new one.
           */
          file_pentry = cache_inode_lookup(parent_pentry,
                                           &file_name,
                                           &attr,
                                           pcontext,
                                           &cache_status_lookup);

          if((cache_status_lookup == CACHE_INODE_NOT_FOUND) ||
             ((cache_status_lookup == CACHE_INODE_SUCCESS)
              && (parg->arg_create3.how.mode == UNCHECKED)))
            {
              /* Create the file */
              if((parg->arg_create3.how.mode == UNCHECKED)
                 && (cache_status_lookup == CACHE_INODE_SUCCESS))
                {
                  cache_status = CACHE_INODE_SUCCESS;
                  attr_newfile = attr;
                }
              else
                file_pentry = cache_inode_create(parent_pentry,
                                                 &file_name,
                                                 REGULAR_FILE,
                                                 mode,
                                                 NULL,
                                                 &attr_newfile,
                                                 pcontext, &cache_status);

              if(file_pentry != NULL)
                {
                  /*
                   * Look at sattr to see if some attributes are to be set at creation time 
                   */
                  attributes_create.asked_attributes = 0ULL;

                  switch (preq->rq_vers)
                    {
                    case NFS_V2:

                      if(nfs2_Sattr_To_FSALattr(&attributes_create,
                                                &parg->arg_create2.attributes) == 0)
                        {
                          pres->res_dirop2.status = NFSERR_IO;
                          rc = NFS_REQ_OK;
                          goto out;
                          break;
                        }
                      break;

                    case NFS_V3:
                      if(nfs3_Sattr_To_FSALattr(&attributes_create,
                                                &parg->arg_create3.how.createhow3_u.
                                                obj_attributes) == 0)
                        {
                          pres->res_create3.status = NFS3ERR_INVAL;
                          rc = NFS_REQ_OK;
                          goto out;
                        }
                      break;
                    }

                  /* Mode is managed above (in cache_inode_create), there is no need 
                   * to manage it */
                  if(attributes_create.asked_attributes & FSAL_ATTR_MODE)
                    attributes_create.asked_attributes &= ~FSAL_ATTR_MODE;

                  /* Some clients (like Solaris 10) try to set the size of the file to 0
                   * at creation time. The FSAL create empty file, so we ignore this */
                  if(attributes_create.asked_attributes & FSAL_ATTR_SIZE)
                    attributes_create.asked_attributes &= ~FSAL_ATTR_SIZE;

                  if(attributes_create.asked_attributes & FSAL_ATTR_SPACEUSED)
                    attributes_create.asked_attributes &= ~FSAL_ATTR_SPACEUSED;

                  /* If owner or owner_group are set, and the credential was
                   * squashed, then we must squash the set owner and owner_group.
                   */
                  squash_setattr(&pworker->export_perms,
                                 &pworker->user_credentials,
                                 &attributes_create);

                  /* Are there attributes to be set (additional to the mode) ? */
                  if(attributes_create.asked_attributes != 0ULL &&
                     attributes_create.asked_attributes != FSAL_ATTR_MODE)
                    {
                      /* A call to cache_inode_setattr is required */
                      if(cache_inode_setattr(file_pentry,
                                             &attributes_create,
                                             pcontext,
                                             FALSE,
                                             &cache_status) != CACHE_INODE_SUCCESS)
                        goto out_failed;

                      /* Get the resulting attributes from the Cache Inode */
                      if(cache_inode_getattr(file_pentry,
                                             &attr_newfile,
                                             pcontext,
                                             &cache_status) != CACHE_INODE_SUCCESS)
                        goto out_failed;

                    }

                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      /* Build file handle */
                      if(nfs2_FSALToFhandle(
                              &(pres->res_dirop2.DIROP2res_u.diropok.file),
                              &file_pentry->handle,
                              pexport) == 0)
                        pres->res_dirop2.status = NFSERR_IO;
                      else
                        {
                          if(!nfs2_FSALattr_To_Fattr(
                                  pexport, &attr_newfile,
                                  &(pres->res_dirop2.DIROP2res_u.
                                    diropok.attributes)))
                            pres->res_dirop2.status = NFSERR_IO;
                          else
                            pres->res_dirop2.status = NFS_OK;
                        }
                      break;

                    case NFS_V3:
                      /* Build file handle */
                      pres->res_create3.status =
                        nfs3_AllocateFH(&pres->res_create3.CREATE3res_u
                                        .resok.obj.post_op_fh3_u.handle);
                      if (pres->res_create3.status != NFS3_OK)
                        {
                          rc = NFS_REQ_OK;
                          goto out;
                        }

                      /* Set Post Op Fh3 structure */
                      if(nfs3_FSALToFhandle(
                              &(pres->res_create3.CREATE3res_u.resok
                                .obj.post_op_fh3_u.handle),
                              &file_pentry->handle, pexport) == 0)
                        {
                          gsh_free(pres->res_create3.CREATE3res_u.resok.obj.
                                   post_op_fh3_u.handle.data.data_val);

                          pres->res_create3.status = NFS3ERR_BADHANDLE;
                          rc = NFS_REQ_OK;
                          goto out;
                        }

                      /* Set Post Op Fh3 structure */
                      pres->res_create3.CREATE3res_u.resok.obj.handle_follows
                        = TRUE;

                      /* Get the attributes of the parent after the
                       * operation, if it fails then return no wcc data
                       * but don't fail the call - we already created
                       * the damn thing and it's too much to undo the
                       * act of creation */
                      if(cache_inode_getattr(parent_pentry,
                                             &attr_parent_after,
                                             pcontext, &cache_status) == CACHE_INODE_SUCCESS)
                        {
                          /* Build Weak Cache Coherency data */
                          nfs_SetWccData(pexport, ppre_attr, &attr_parent_after,
                                         &(pres->res_create3.CREATE3res_u.resok.dir_wcc));
                        }

                      /* Build entry attributes */
                      nfs_SetPostOpAttr(pexport,
                                        &attr_newfile,
                                        &(pres->res_create3.CREATE3res_u.resok.
                                          obj_attributes));


                      pres->res_create3.status = NFS3_OK;
                      break;
                    }       /* switch */
                  rc = NFS_REQ_OK;
                  goto out;
                }
            }
          else
            {
              switch (preq->rq_vers)
                {
                case NFS_V2:
                  if(cache_status_lookup == CACHE_INODE_SUCCESS)
                    pres->res_dirop2.status = NFSERR_EXIST;
                  else
                    pres->res_dirop2.status = NFSERR_IO;
                  break;

                case NFS_V3:
                  if(cache_status_lookup == CACHE_INODE_SUCCESS)
                    pres->res_create3.status = NFS3ERR_EXIST;
                  else
                    pres->res_create3.status = NFS3ERR_INVAL;
                  nfs_SetWccData(pexport, ppre_attr, NULL,
                                 &(pres->res_create3.CREATE3res_u.resfail.dir_wcc));
                  break;
                }
              rc = NFS_REQ_OK;
              goto out;
            } /* if( cache_status_lookup == CACHE_INODE_NOT_FOUND ) */
        }
    }

out_failed:
  /* Set the exit status */
  rc = nfs_SetFailedStatus(pexport, preq->rq_vers, cache_status,
                           &pres->res_dirop2.status, &pres->res_create3.status,
                           NULL, ppre_attr,
                           &(pres->res_create3.CREATE3res_u.resfail.dir_wcc),
                           NULL, NULL);
out:
  /* return references */
  if (file_pentry)
      cache_inode_put(file_pentry);

  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);

}                               /* nfs_Create */

/**
 * nfs_Create_Free: Frees the result structure allocated for nfs_Create.
 * 
 * Frees the result structure allocated for nfs_Create.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Create_Free(nfs_res_t * resp)
{
  if((resp->res_create3.status == NFS3_OK) &&
     (resp->res_create3.CREATE3res_u.resok.obj.handle_follows == TRUE))
    gsh_free(resp->res_create3.CREATE3res_u.resok.obj
             .post_op_fh3_u.handle.data.data_val);
}
