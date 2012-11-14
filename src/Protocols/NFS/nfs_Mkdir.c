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
 * \file nfs_Mkdir.c 
 * \author  $Author: deniel $
 * \data    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.21 $
 * \brief   evrythinhg you need to handle NFS PROC2-3 MKDIR.
 *
 * Evrythinhg you need to handle NFS PROC2-3 MKDIR. 
 * MKDIR is used to create new directories
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
 * @brief The NFS PROC2 and PROC3 MKDIR
 *
 * Implements the NFS PROC MKDIR function (for V2 and V3).
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

int nfs_Mkdir(nfs_arg_t *parg,
              exportlist_t *pexport,
              fsal_op_context_t *pcontext,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres)
{
  char *str_dir_name = NULL;
  fsal_accessmode_t mode = 0;
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *parent_pentry = NULL;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t attr_parent_after;
  cache_inode_file_type_t parent_filetype;
  fsal_handle_t *pfsal_handle;
  fsal_name_t dir_name;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_inode_status_t cache_status_lookup;
  cache_inode_create_arg_t create_arg;
  int rc = NFS_REQ_OK;
#ifdef _USE_QUOTA
  fsal_status_t fsal_status ;
#endif

  memset(&create_arg, 0, sizeof(create_arg));

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          str_dir_name = parg->arg_mkdir2.where.name;
          break;
        case NFS_V3:
          str_dir_name = parg->arg_mkdir3.where.name;
          break;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_mkdir2.where.dir),
                       &(parg->arg_mkdir3.where.dir),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Mkdir handle: %s name: %s",
               str, str_dir_name);
    }

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
      pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
      ppre_attr = NULL;
    }

  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         &(parg->arg_mkdir2.where.dir),
                                         &(parg->arg_mkdir3.where.dir),
                                         NULL,
                                         &(pres->res_dirop2.status),
                                         &(pres->res_mkdir3.status),
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
   * Sanity checks: 
   */
  if(parent_filetype != DIRECTORY)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_dirop2.status = NFSERR_NOTDIR;
          break;

        case NFS_V3:
          pres->res_mkdir3.status = NFS3ERR_NOTDIR;
          break;
        }

      rc = NFS_REQ_OK;
      goto out;
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
             pres->res_dirop2.status = NFSERR_DQUOT;
             break;

           case NFS_V3:
             pres->res_mkdir3.status = NFS3ERR_DQUOT;
             break;
         }

       rc = NFS_REQ_OK ;
       goto out;
     }
#endif /* _USE_QUOTA */


  switch (preq->rq_vers)
    {
    case NFS_V2:
      str_dir_name = parg->arg_mkdir2.where.name;

      if(parg->arg_mkdir2.attributes.mode != (unsigned int)-1)
        {
          mode = (fsal_accessmode_t) parg->arg_mkdir2.attributes.mode;
        }
      else
        {
          mode = (fsal_accessmode_t) 0;
        }
      break;

    case NFS_V3:
      str_dir_name = parg->arg_mkdir3.where.name;

      if(parg->arg_mkdir3.attributes.mode.set_it == TRUE)
        mode = (fsal_accessmode_t) parg->arg_mkdir3.attributes.mode.set_mode3_u.mode;
      else
        mode = (fsal_accessmode_t) 0;
      break;
    }

  //if(str_dir_name == NULL || strlen(str_dir_name) == 0)
  if(str_dir_name == NULL || *str_dir_name == '\0' )
    {
      if(preq->rq_vers == NFS_V2)
        pres->res_dirop2.status = NFSERR_IO;
      if(preq->rq_vers == NFS_V3)
        pres->res_mkdir3.status = NFS3ERR_INVAL;
    }
  else
    {
      /* Make the directory */
      if((cache_status = cache_inode_error_convert(FSAL_str2name(str_dir_name,
                                                                 0,
                                                                 &dir_name))) ==
         CACHE_INODE_SUCCESS)
        {
          /*
           * Lookup file to see if it exists.  If so, use it.  Otherwise
           * create a new one.
           */
          dir_pentry = cache_inode_lookup(parent_pentry,
                                          &dir_name,
                                          &attr,
                                          pcontext,
                                          &cache_status_lookup);

          if(cache_status_lookup == CACHE_INODE_NOT_FOUND)
            {
              /* The create_arg structure contains the information "newly created directory"
               * to be passed to cache_inode_new_entry from cache_inode_create */
              create_arg.newly_created_dir = TRUE;

              /* Create the directory */
              if((dir_pentry = cache_inode_create(parent_pentry,
                                                  &dir_name,
                                                  DIRECTORY,
                                                  mode,
                                                  &create_arg,
                                                  &attr,
                                                  pcontext, &cache_status)) != NULL)
                {
                  /*
                   * Get the FSAL handle for this entry
                   */
                  pfsal_handle = &dir_pentry->handle;

                  if(preq->rq_vers == NFS_V2)
                    {
                      DIROP2resok *d2ok = &pres->res_dirop2.DIROP2res_u.diropok;

                      /* Build file handle */
                      if(!nfs2_FSALToFhandle(&d2ok->file, pfsal_handle, pexport))
                        pres->res_dirop2.status = NFSERR_IO;
                      else
                        {
                          /*
                           * Build entry
                           * attributes
                           */
                          if(nfs2_FSALattr_To_Fattr(pexport, &attr,
                                                    &d2ok->attributes) == 0)
                            pres->res_dirop2.status = NFSERR_IO;
                          else
                            pres->res_dirop2.status = NFS_OK;
                        }
                    }
                  else
                    {
                      MKDIR3resok *d3ok = &pres->res_mkdir3.MKDIR3res_u.resok;

                      /* Build file handle */
                      pres->res_mkdir3.status =
                          nfs3_AllocateFH(&d3ok->obj.post_op_fh3_u.handle);
                      if(pres->res_mkdir3.status !=  NFS3_OK)
                        {
                          rc = NFS_REQ_OK;
                          goto out;
                        }

                      if(nfs3_FSALToFhandle(&d3ok->obj.post_op_fh3_u.handle,
                                            pfsal_handle, pexport) == 0)
                        {
                          gsh_free(d3ok->obj.post_op_fh3_u.handle.data.data_val);
                          pres->res_mkdir3.status = NFS3ERR_INVAL;
                          rc = NFS_REQ_OK;
                          goto out;
                        }

                      /* Set Post Op Fh3 structure */
                      d3ok->obj.handle_follows = TRUE;

                      /*
                       * Build entry attributes 
                       */
                      nfs_SetPostOpAttr(pexport, &attr, &d3ok->obj_attributes);

                      /* Get the attributes of the parent after the operation */
                      if(cache_inode_getattr(parent_pentry,
                                             &attr_parent_after,
                                             pcontext, &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          pres->res_mkdir3.status = nfs3_Errno(cache_status);
                          rc = NFS_REQ_OK;
                          goto out;
                        }


                      /*
                       * Build Weak Cache Coherency data 
                       */
                      nfs_SetWccData(pexport, ppre_attr, &attr_parent_after,
                                     &d3ok->dir_wcc);

                      pres->res_mkdir3.status = NFS3_OK;
                    }
                  rc = NFS_REQ_OK;
                  goto out;
                }
            }                   /* If( cache_status_lookup == CACHE_INODE_NOT_FOUND ) */
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
                      pres->res_mkdir3.status = NFS3ERR_EXIST;
                  else
                      pres->res_mkdir3.status = NFS3ERR_INVAL;
                  nfs_SetWccData(pexport, ppre_attr, NULL,
                                 &(pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc));
                  break;
                }
              rc = NFS_REQ_OK;
              goto out;
            }
        }
    }

  /* If we are here, there was an error */
  rc = nfs_SetFailedStatus(pexport, preq->rq_vers, cache_status,
                           &pres->res_dirop2.status, &pres->res_mkdir3.status,
                           NULL, ppre_attr,
                           &(pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc),
                           NULL, NULL);
out:
  /* return references */
  if (dir_pentry)
      cache_inode_put(dir_pentry);

  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);
}

/**
 * nfs_Mkdir_Free: Frees the result structure allocated for nfs_Mkdir.
 *
 * Frees the result structure allocated for nfs_Mkdir.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Mkdir_Free(nfs_res_t * resp)
{
  if((resp->res_mkdir3.status == NFS3_OK) &&
     (resp->res_mkdir3.MKDIR3res_u.resok.obj.handle_follows == TRUE))
    gsh_free(resp->res_mkdir3.MKDIR3res_u.resok.obj
             .post_op_fh3_u.handle.data.data_val);
}                               /* nfs_Mkdir_Free */
