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
 *  version 3 of the License, or (at your option) any later version.
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
 * \file    nfs3_Mknod.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Mknod.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs3_Mknod: Implements NFSPROC3_MKNOD
 *
 * Implements NFSPROC3_COMMIT. Unused for now, but may be supported later. 
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

int nfs3_Mknod(nfs_arg_t * parg,
               exportlist_t * pexport,
               fsal_op_context_t * pcontext,
               cache_inode_client_t * pclient,
               hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  int rc;
  cache_entry_t *parent_pentry = NULL;
  fsal_attrib_list_t parent_attr;
  fsal_attrib_list_t *ppre_attr;
  fsal_attrib_list_t attr_parent_after;
  cache_inode_file_type_t parent_filetype;
  cache_inode_file_type_t nodetype;
  char *str_file_name = NULL;
  fsal_name_t file_name;
  cache_inode_status_t cache_status;
  cache_inode_status_t cache_status_lookup;
  fsal_accessmode_t mode = 0;
  cache_entry_t *node_pentry = NULL;
  fsal_attrib_list_t attr;
  cache_inode_create_arg_t create_arg;
  fsal_handle_t *pfsal_handle;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      sprint_fhandle3(str, &(parg->arg_mknod3.where.dir));
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs3_Mknod handle: %s name: %s",
               str, parg->arg_mknod3.where.name);
    }

  /* to avoid setting them on each error case */

  pres->res_mknod3.MKNOD3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
  pres->res_mknod3.MKNOD3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
  ppre_attr = NULL;

  /* retrieve parent entry */

  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         NULL,
                                         &(parg->arg_mknod3.where.dir),
                                         NULL,
                                         NULL,
                                         &(pres->res_mknod3.status),
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
   * Sanity checks: new node name must be non-null; parent must be a
   * directory. 
   */
  if(parent_filetype != DIRECTORY)
    {
      pres->res_mknod3.status = NFS3ERR_NOTDIR;
      return NFS_REQ_OK;
    }

  str_file_name = parg->arg_mknod3.where.name;

  switch (parg->arg_mknod3.what.type)
    {
    case NF3CHR:
    case NF3BLK:

      if(parg->arg_mknod3.what.mknoddata3_u.device.dev_attributes.mode.set_it)
        mode =
            (fsal_accessmode_t) parg->arg_mknod3.what.mknoddata3_u.device.dev_attributes.
            mode.set_mode3_u.mode;
      else
        mode = (fsal_accessmode_t) 0;

      create_arg.dev_spec.major =
          parg->arg_mknod3.what.mknoddata3_u.device.spec.specdata1;
      create_arg.dev_spec.minor =
          parg->arg_mknod3.what.mknoddata3_u.device.spec.specdata2;

      break;

    case NF3FIFO:
    case NF3SOCK:

      if(parg->arg_mknod3.what.mknoddata3_u.pipe_attributes.mode.set_it)
        mode =
            (fsal_accessmode_t) parg->arg_mknod3.what.mknoddata3_u.pipe_attributes.mode.
            set_mode3_u.mode;
      else
        mode = (fsal_accessmode_t) 0;

      create_arg.dev_spec.major = 0;
      create_arg.dev_spec.minor = 0;

      break;

    default:
      pres->res_mknod3.status = NFS3ERR_BADTYPE;
      return NFS_REQ_OK;
    }

  switch (parg->arg_mknod3.what.type)
    {
    case NF3CHR:
      nodetype = CHARACTER_FILE;
      break;
    case NF3BLK:
      nodetype = BLOCK_FILE;
      break;
    case NF3FIFO:
      nodetype = FIFO_FILE;
      break;
    case NF3SOCK:
      nodetype = SOCKET_FILE;
      break;
    default:
      pres->res_mknod3.status = NFS3ERR_BADTYPE;
      return NFS_REQ_OK;
    }

  //if(str_file_name == NULL || strlen(str_file_name) == 0)
  if(str_file_name == NULL || *str_file_name == '\0' )
    {
      pres->res_mknod3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }

  /* convert node name */

  if((cache_status = cache_inode_error_convert(FSAL_str2name(str_file_name,
                                                             FSAL_MAX_NAME_LEN,
                                                             &file_name))) ==
     CACHE_INODE_SUCCESS)
    {
      /*
       * Lookup node to see if it exists.  If so, use it.  Otherwise
       * create a new one.
       */
      node_pentry = cache_inode_lookup( parent_pentry,
                                        &file_name,
                                        pexport->cache_inode_policy,
                                        &attr,
                                        ht, 
                                        pclient, 
                                        pcontext, 
                                        &cache_status_lookup);

      if(cache_status_lookup == CACHE_INODE_NOT_FOUND)
        {

          /* Create the node */

          if((node_pentry = cache_inode_create(parent_pentry,
                                               &file_name,
                                               nodetype,
                                               pexport->cache_inode_policy,
                                               mode,
                                               &create_arg,
                                               &attr,
                                               ht,
                                               pclient, pcontext, &cache_status)) != NULL)
            {

              /*
               * Get the FSAL handle for this entry 
               */
              pfsal_handle = cache_inode_get_fsal_handle(node_pentry, &cache_status);

              if(cache_status == CACHE_INODE_SUCCESS)
                {
                  /* Build file handle */
                  if((pres->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.handle.data.
                      data_val = Mem_Alloc_Label(NFS3_FHSIZE, "Filehandle V3 in nfs3_mknod")) == NULL)
                    {
                      pres->res_mknod3.status = NFS3ERR_IO;
                      return NFS_REQ_OK;
                    }

                  if(nfs3_FSALToFhandle
                     (&pres->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.handle,
                      pfsal_handle, pexport) == 0)
                    {
                      Mem_Free((char *)pres->res_mknod3.MKNOD3res_u.resok.obj.
                               post_op_fh3_u.handle.data.data_val);
                      pres->res_mknod3.status = NFS3ERR_INVAL;
                      return NFS_REQ_OK;
                    }
                  else
                    {
                      /* Set Post Op Fh3 structure */
                      pres->res_mknod3.MKNOD3res_u.resok.obj.handle_follows = TRUE;
                      pres->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.handle.data.
                          data_len = sizeof(file_handle_v3_t);

                      /*
                       * Build entry
                       * attributes 
                       */
                      nfs_SetPostOpAttr(pcontext, pexport,
                                        node_pentry,
                                        &attr,
                                        &(pres->res_mknod3.MKNOD3res_u.resok.
                                          obj_attributes));

                      /* Get the attributes of the parent after the operation */
                      cache_inode_get_attributes(parent_pentry, &attr_parent_after);

                      /*
                       * Build Weak Cache
                       * Coherency data 
                       */
                      nfs_SetWccData(pcontext, pexport,
                                     parent_pentry,
                                     ppre_attr,
                                     &attr_parent_after,
                                     &(pres->res_mknod3.MKNOD3res_u.resok.dir_wcc));

                      pres->res_mknod3.status = NFS3_OK;
                    }

                  return NFS_REQ_OK;

                }
              /* get fsal handle success */
            }
          /* mknod sucess */
        }                       /* not found */
      else
        {
          /* object already exists or failure during lookup */
          if(cache_status_lookup == CACHE_INODE_SUCCESS)
            {
              /* Trying to create an entry that already exists */
              cache_status = CACHE_INODE_ENTRY_EXISTS;
              pres->res_mknod3.status = NFS3ERR_EXIST;
            }
          else
            {
              /* Server fault */
              cache_status = cache_status_lookup;
              pres->res_mknod3.status = NFS3ERR_INVAL;
            }

          nfs_SetFailedStatus(pcontext, pexport,
                              preq->rq_vers,
                              cache_status,
                              NULL,
                              &pres->res_mknod3.status,
                              NULL, NULL,
                              parent_pentry,
                              ppre_attr,
                              &(pres->res_mknod3.MKNOD3res_u.resfail.dir_wcc),
                              NULL, NULL, NULL);

          return NFS_REQ_OK;
        }

    }

  /* convertion OK */
  /* If we are here, there was an error */
  if(nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;

    }
  nfs_SetFailedStatus(pcontext, pexport,
                      preq->rq_vers,
                      cache_status,
                      NULL,
                      &pres->res_mknod3.status,
                      NULL, NULL,
                      parent_pentry,
                      ppre_attr,
                      &(pres->res_mknod3.MKNOD3res_u.resfail.dir_wcc), NULL, NULL, NULL);

  return NFS_REQ_OK;

}                               /* nfs3_Mknod */

/**
 * nfs3_Mknod_Free: Frees the result structure allocated for nfs3_Mknod.
 * 
 * Frees the result structure allocated for nfs3_Mknod.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Mknod_Free(nfs_res_t * pres)
{
  if((pres->res_mknod3.status == NFS3_OK) &&
     (pres->res_mknod3.MKNOD3res_u.resok.obj.handle_follows == TRUE))
    Mem_Free(pres->res_mknod3.MKNOD3res_u.resok.obj.post_op_fh3_u.handle.data.data_val);

}                               /* nfs3_Mknod_Free */
