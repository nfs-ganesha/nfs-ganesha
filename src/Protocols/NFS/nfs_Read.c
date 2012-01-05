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
 * \file    nfs_Read.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS READ request.
 *
 * nfs_Read.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "cache_content_policy.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 *
 * nfs_Read: The NFS PROC2 and PROC3 READ
 *
 * Implements the NFS PROC READ function (for V2 and V3).
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

int nfs_Read(nfs_arg_t * parg,
             exportlist_t * pexport,
             fsal_op_context_t * pcontext,
             cache_inode_client_t * pclient,
             hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Read";

  cache_entry_t *pentry;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *ppre_attr;
  int rc;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_content_status_t content_status;
  fsal_seek_t seek_descriptor;
  fsal_size_t size = 0;
  fsal_size_t read_size;
  fsal_off_t offset = 0;
  caddr_t data = NULL;
  cache_inode_file_type_t filetype;
  fsal_boolean_t eof_met;
  cache_content_policy_data_t datapol;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];

      switch (preq->rq_vers)
        {
        case NFS_V2:
          offset = parg->arg_read2.offset;
          size = parg->arg_read2.count;
          break;
        case NFS_V3:
          offset = parg->arg_read3.offset;
          size = parg->arg_read3.count;
        }

      nfs_FhandleToStr(preq->rq_vers,
                       &(parg->arg_read2.file),
                       &(parg->arg_read3.file),
                       NULL,
                       str);
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs_Read handle: %s start: %llx len: %llx",
               str, (unsigned long long) offset, (unsigned long long) size);
    }

  datapol.UseMaxCacheSize = FALSE;

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_read3.READ3res_u.resfail.file_attributes.attributes_follow = FALSE;
    }

  /* Convert file handle into a cache entry */
  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_read2.file),
                                  &(parg->arg_read3.file),
                                  NULL,
                                  &(pres->res_read2.status),
                                  &(pres->res_read3.status),
                                  NULL, &pre_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_read3.file))))
    return nfs3_Read_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  if(cache_inode_access(pentry,
                        FSAL_READ_ACCESS,
                        ht,
                        pclient,
                        pcontext,
                        &cache_status) != CACHE_INODE_SUCCESS)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_attr2.status = nfs2_Errno(cache_status);
          break;

        case NFS_V3:
          pres->res_read3.status = nfs3_Errno(cache_status);
          break;
        }
      return NFS_REQ_OK;
    }

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_attr;

  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert(pre_attr.type);

  /* Sanity check: read only from a regular file */
  if(filetype != REGULAR_FILE)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          /*
           * In the RFC tell it not good but it does
           * not tell what to do ... 
           */
          pres->res_attr2.status = NFSERR_ISDIR;
          break;

        case NFS_V3:
          if(filetype == DIRECTORY)
            pres->res_read3.status = NFS3ERR_ISDIR;
          else
            pres->res_read3.status = NFS3ERR_INVAL;
          break;
        }

      return NFS_REQ_OK;
    }

  /* For MDONLY export, reject write operation */
  /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level */
  /* This is done by replying EDQUOT (this error is known for not disturbing the client's requests cache */
  if(pexport->access_type == ACCESSTYPE_MDONLY
     || pexport->access_type == ACCESSTYPE_MDONLY_RO)
    {
      switch (preq->rq_vers)
        {
        case NFS_V2:
          pres->res_attr2.status = NFSERR_DQUOT;
          break;

        case NFS_V3:
          pres->res_read3.status = NFS3ERR_DQUOT;
          break;
        }

      nfs_SetFailedStatus(pcontext, pexport,
                          preq->rq_vers,
                          cache_status,
                          &pres->res_read2.status,
                          &pres->res_read3.status,
                          pentry,
                          &(pres->res_read3.READ3res_u.resfail.file_attributes),
                          NULL, NULL, NULL, NULL, NULL, NULL);

      return NFS_REQ_OK;
    }

  /* Extract the argument from the request */
  switch (preq->rq_vers)
    {
    case NFS_V2:
      offset = parg->arg_read2.offset;  /* beginoffset is obsolete */
      size = parg->arg_read2.count;     /* totalcount is obsolete  */
      break;

    case NFS_V3:
      offset = parg->arg_read3.offset;
      size = parg->arg_read3.count;
      break;
    }

  /* 
   * do not exceed maxium READ offset if set 
   */
  if((pexport->options & EXPORT_OPTION_MAXOFFSETREAD) == EXPORT_OPTION_MAXOFFSETREAD)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "-----> Read offset=%llu count=%llu MaxOffSet=%llu",
                   (unsigned long long) offset,
                   (unsigned long long) size,
                   (unsigned long long) pexport->MaxOffsetRead);

      if((fsal_off_t) (offset + size) > pexport->MaxOffsetRead)
        {
          LogEvent(COMPONENT_NFSPROTO,
                   "NFS READ: A client tryed to violate max file size %llu for exportid #%hu",
                   (unsigned long long) pexport->MaxOffsetRead, pexport->id);

          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_attr2.status = NFSERR_DQUOT;
              break;

            case NFS_V3:
              pres->res_read3.status = NFS3ERR_INVAL;
              break;
            }

          nfs_SetFailedStatus(pcontext, pexport,
                              preq->rq_vers,
                              cache_status,
                              &pres->res_read2.status,
                              &pres->res_read3.status,
                              pentry,
                              &(pres->res_read3.READ3res_u.resfail.file_attributes),
                              NULL, NULL, NULL, NULL, NULL, NULL);

          return NFS_REQ_OK;
        }
    }

  /*
   * We should not exceed the FSINFO rtmax field for
   * the size 
   */
  if(((pexport->options & EXPORT_OPTION_MAXREAD) == EXPORT_OPTION_MAXREAD) &&
     size > pexport->MaxRead)
    {
      /*
       * The client asked for too much, normally
       * this should not happen because the client
       * is calling nfs_Fsinfo at mount time and so
       * is aware of the server maximum write size 
       */
      size = pexport->MaxRead;
    }

  if(size == 0)
    {
      cache_status = CACHE_INODE_SUCCESS;
      read_size = 0;
      data = NULL;
    }
  else
    {
      data = Mem_Alloc(size);

      if(data == NULL)
        {
          return NFS_REQ_DROP;
        }

      seek_descriptor.whence = FSAL_SEEK_SET;
      seek_descriptor.offset = offset;

      datapol.UseMaxCacheSize = pexport->options & EXPORT_OPTION_MAXCACHESIZE;
      datapol.MaxCacheSize = pexport->MaxCacheSize;

      /* If export is not cached, cache it now */
      if((pexport->options & EXPORT_OPTION_USE_DATACACHE) &&
         (cache_content_cache_behaviour(pentry,
                                        &datapol,
                                        (cache_content_client_t *)
                                        pclient->pcontent_client,
                                        &content_status) == CACHE_CONTENT_FULLY_CACHED)
         && (pentry->object.file.pentry_content == NULL))
        {
          /* Entry is not in datacache, but should be in, cache it */
          cache_inode_add_data_cache(pentry, ht, pclient, pcontext, &cache_status);
          if((cache_status != CACHE_INODE_SUCCESS) &&
             (cache_status != CACHE_INODE_CACHE_CONTENT_EXISTS))
            {
              /* Entry is not in datacache, but should be in, cache it .
               * Several threads may call this function at the first time and a race condition can occur here
               * in order to avoid this, cache_inode_add_data_cache is "mutex protected" 
               * The first call will create the file content cache entry, the further will return
               * with error CACHE_INODE_CACHE_CONTENT_EXISTS which is not a pathological thing here */

              /* If we are here, there was an error */
              if(nfs_RetryableError(cache_status))
                {
                  return NFS_REQ_DROP;
                }

              nfs_SetFailedStatus(pcontext, pexport,
                                  preq->rq_vers,
                                  cache_status,
                                  &pres->res_read2.status,
                                  &pres->res_read3.status,
                                  pentry,
                                  &(pres->res_read3.READ3res_u.resfail.file_attributes),
                                  NULL, NULL, NULL, NULL, NULL, NULL);

              return NFS_REQ_OK;
            }
        }

      if(cache_inode_rdwr(pentry,
                          CACHE_INODE_READ,
                          &seek_descriptor,
                          size,
                          &read_size,
                          &attr,
                          data,
                          &eof_met,
                          ht,
                          pclient, pcontext, TRUE, &cache_status) == CACHE_INODE_SUCCESS)

        {
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_read2.READ2res_u.readok.data.nfsdata2_val = data;
              pres->res_read2.READ2res_u.readok.data.nfsdata2_len = read_size;

              nfs2_FSALattr_To_Fattr(pexport, &attr,
                                     &(pres->res_read2.READ2res_u.readok.attributes));

              pres->res_attr2.status = NFS_OK;
              break;

            case NFS_V3:

              pres->res_read3.READ3res_u.resok.eof = FALSE;

              /* Did we reach eof ? */
              /* BUGAZOMEU use eof */
              if((offset + read_size) >= attr.filesize)
                pres->res_read3.READ3res_u.resok.eof = TRUE;

              /* Build Post Op Attributes */
              nfs_SetPostOpAttr(pcontext, pexport,
                                pentry,
                                &attr,
                                &(pres->res_read3.READ3res_u.resok.file_attributes));

              pres->res_read3.READ3res_u.resok.file_attributes.attributes_follow = TRUE;

              pres->res_read3.READ3res_u.resok.count = read_size;
              pres->res_read3.READ3res_u.resok.data.data_val = data;
              pres->res_read3.READ3res_u.resok.data.data_len = read_size;

              pres->res_read3.status = NFS3_OK;
              break;
            }                   /* switch */

          return NFS_REQ_OK;
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
                      &pres->res_read2.status,
                      &pres->res_read3.status,
                      pentry,
                      &(pres->res_read3.READ3res_u.resfail.file_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs_Read */

/**
 * nfs2_Read_Free: Frees the result structure allocated for nfs2_Read.
 * 
 * Frees the result structure allocated for nfs2_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Read_Free(nfs_res_t * resp)
{
  if((resp->res_read2.status == NFS_OK) &&
     (resp->res_read2.READ2res_u.readok.data.nfsdata2_len != 0))
    Mem_Free(resp->res_read2.READ2res_u.readok.data.nfsdata2_val);
}                               /* nfs2_Read_Free */

/**
 * nfs3_Read_Free: Frees the result structure allocated for nfs3_Read.
 * 
 * Frees the result structure allocated for nfs3_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Read_Free(nfs_res_t * resp)
{
  if((resp->res_read3.status == NFS3_OK) &&
     (resp->res_read3.READ3res_u.resok.data.data_len != 0))
    Mem_Free(resp->res_read3.READ3res_u.resok.data.data_val);
}                               /* nfs3_Read_Free */
