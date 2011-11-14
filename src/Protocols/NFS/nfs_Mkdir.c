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
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"

/**
 *
 * nfs_Mkdir: The NFS PROC2 and PROC3 MKDIR
 *
 * Implements the NFS PROC MKDIR function (for V2 and V3).
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

int nfs_Mkdir(nfs_arg_t * parg,
              exportlist_t * pexport,
              fsal_op_context_t * pcontext,
              cache_inode_client_t * pclient,
              hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Mkdir";

  char *str_dir_name = NULL;
  fsal_accessmode_t mode = 0;
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *parent_pentry = NULL;
  int rc = 0;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t attr_parent_after;
  cache_inode_file_type_t parent_filetype;
  fsal_handle_t *pfsal_handle;
  fsal_name_t dir_name;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_inode_status_t cache_status_lookup;

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
                                         pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
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

      return NFS_REQ_OK;
    }

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
                                                                 FSAL_MAX_NAME_LEN,
                                                                 &dir_name))) ==
         CACHE_INODE_SUCCESS)
        {
          /*
           * Lookup file to see if it exists.  If so, use it.  Otherwise
           * create a new one.  
           */
          dir_pentry = cache_inode_lookup( parent_pentry,
                                           &dir_name,
                                           pexport->cache_inode_policy,
                                           &attr,
                                           ht, 
                                           pclient, 
                                           pcontext, 
                                           &cache_status_lookup);

          if(cache_status_lookup == CACHE_INODE_NOT_FOUND)
            {
              /* Create the directory */
              if((dir_pentry = cache_inode_create(parent_pentry,
                                                  &dir_name,
                                                  DIRECTORY,
                                                  pexport->cache_inode_policy,
                                                  mode,
                                                  NULL,
                                                  &attr,
                                                  ht,
                                                  pclient,
                                                  pcontext, &cache_status)) != NULL)
                {
                  /*
                   * Get the FSAL handle for this entry 
                   */
                  pfsal_handle = cache_inode_get_fsal_handle(dir_pentry, &cache_status);

                  if(cache_status == CACHE_INODE_SUCCESS)
                    {
                      switch (preq->rq_vers)
                        {
                        case NFS_V2:
                          /* Build file handle */
                          if(!nfs2_FSALToFhandle
                             (&(pres->res_dirop2.DIROP2res_u.diropok.file), pfsal_handle,
                              pexport))
                            pres->res_dirop2.status = NFSERR_IO;
                          else
                            {
                              /*
                               * Build entry
                               * attributes 
                               */
                              if(nfs2_FSALattr_To_Fattr(pexport, &attr,
                                                        &(pres->res_dirop2.DIROP2res_u.
                                                          diropok.attributes)) == 0)
                                pres->res_dirop2.status = NFSERR_IO;
                              else
                                pres->res_dirop2.status = NFS_OK;
                            }
                          break;

                        case NFS_V3:
                          /* Build file handle */
                          if((pres->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.
                              handle.data.data_val = Mem_Alloc_Label(NFS3_FHSIZE,
                                                                     "Filehandle V3 in nfs3_mkdir")) == NULL)
                            {
                              pres->res_mkdir3.status = NFS3ERR_IO;
                              return NFS_REQ_OK;
                            }

                          if(nfs3_FSALToFhandle
                             (&pres->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.
                              handle, pfsal_handle, pexport) == 0)
                            {
                              Mem_Free((char *)pres->res_mkdir3.MKDIR3res_u.resok.obj.
                                       post_op_fh3_u.handle.data.data_val);
                              pres->res_mkdir3.status = NFS3ERR_INVAL;
                              return NFS_REQ_OK;
                            }
                          else
                            {
                              /* Set Post Op Fh3 structure */
                              pres->res_mkdir3.MKDIR3res_u.resok.obj.handle_follows =
                                  TRUE;
                              pres->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.handle.
                                  data.data_len = sizeof(file_handle_v3_t);

                              /*
                               * Build entry
                               * attributes 
                               */
                              nfs_SetPostOpAttr(pcontext, pexport,
                                                dir_pentry,
                                                &attr,
                                                &(pres->res_mkdir3.MKDIR3res_u.resok.
                                                  obj_attributes));

                              /* Get the attributes of the parent after the operation */
                              cache_inode_get_attributes(parent_pentry,
                                                         &attr_parent_after);

                              /*
                               * Build Weak Cache
                               * Coherency data 
                               */
                              nfs_SetWccData(pcontext, pexport,
                                             parent_pentry,
                                             ppre_attr,
                                             &attr_parent_after,
                                             &(pres->res_mkdir3.MKDIR3res_u.resok.
                                               dir_wcc));

                              pres->res_mkdir3.status = NFS3_OK;
                            }

                          break;
                        }
                      return NFS_REQ_OK;
                    }
                }
            }                   /* If( cache_status_lookup == CACHE_INODE_NOT_FOUND ) */
          else
            {
              /* object already exists or failure during lookup */
              if(cache_status_lookup == CACHE_INODE_SUCCESS)
                {
                  /* Trying to create a file that already exists */
                  cache_status = CACHE_INODE_ENTRY_EXISTS;

                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_dirop2.status = NFSERR_EXIST;
                      break;

                    case NFS_V3:
                      pres->res_mkdir3.status = NFS3ERR_EXIST;
                      break;
                    }
                }
              else
                {
                  /* Server fault */
                  cache_status = cache_status_lookup;

                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_dirop2.status = NFSERR_IO;
                      break;

                    case NFS_V3:
                      pres->res_mkdir3.status = NFS3ERR_INVAL;
                      break;
                    }
                }

              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  cache_status,
                                  &pres->res_dirop2.status,
                                  &pres->res_mkdir3.status,
                                  NULL, NULL,
                                  parent_pentry,
                                  ppre_attr,
                                  &(pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc),
                                  NULL, NULL, NULL);

              return NFS_REQ_OK;
            }
        }
    }

  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;

    }
  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      &pres->res_dirop2.status,
                      &pres->res_mkdir3.status,
                      NULL, NULL,
                      parent_pentry,
                      ppre_attr,
                      &(pres->res_mkdir3.MKDIR3res_u.resfail.dir_wcc), NULL, NULL, NULL);

  return NFS_REQ_OK;

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
    Mem_Free(resp->res_mkdir3.MKDIR3res_u.resok.obj.post_op_fh3_u.handle.data.data_val);
}                               /* nfs_Mkdir_Free */
