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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

static void
nfs_read_ok(exportlist_t * pexport,
            struct svc_req *preq, 
            nfs_res_t * pres,
            char *data,
            fsal_size_t read_size,
            fsal_attrib_list_t *attr,
            int eof)
{
    if((read_size == 0) && (data != NULL)) {
        gsh_free(data);
        data = NULL;
    }
    switch (preq->rq_vers) {
    case NFS_V2:
        pres->res_read2.READ2res_u.readok.data.nfsdata2_val = data;
        pres->res_read2.READ2res_u.readok.data.nfsdata2_len = read_size;

        nfs2_FSALattr_To_Fattr(pexport, attr,
                               &(pres->res_read2.READ2res_u.readok.attributes));

        pres->res_attr2.status = NFS_OK;
        break;

    case NFS_V3:
        /* Build Post Op Attributes */
        nfs_SetPostOpAttr(pexport, attr,
                          &(pres->res_read3.READ3res_u.resok.file_attributes));

        pres->res_read3.READ3res_u.resok.eof = eof;
        pres->res_read3.READ3res_u.resok.count = read_size;
        pres->res_read3.READ3res_u.resok.data.data_val = data;
        pres->res_read3.READ3res_u.resok.data.data_len = read_size;

        pres->res_read3.status = NFS3_OK;
        break;
    }                   /* switch */
}

/**
 *
 * @brief The NFS PROC2 and PROC3 READ
 *
 * Implements the NFS PROC READ function (for V2 and V3).
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

int nfs_Read(nfs_arg_t *parg,
             exportlist_t *pexport,
             fsal_op_context_t *pcontext,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres)
{
  cache_entry_t *pentry;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t pre_attr;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  size_t size = 0;
  size_t read_size = 0;
  fsal_off_t offset = 0;
  void *data = NULL;
  cache_inode_file_type_t filetype;
  fsal_boolean_t eof_met=FALSE;
  int rc = NFS_REQ_OK;

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

  if(preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_read3.READ3res_u.resfail.file_attributes.attributes_follow = FALSE;
      /* initialize for read of size 0 */
      pres->res_read3.READ3res_u.resok.eof = FALSE;
      pres->res_read3.READ3res_u.resok.count = 0;
      pres->res_read3.READ3res_u.resok.data.data_val = NULL;
      pres->res_read3.READ3res_u.resok.data.data_len = 0;
      pres->res_read3.status = NFS3_OK;
    }
  else if(preq->rq_vers == NFS_V2)
    {
      /* initialize for read of size 0 */
      pres->res_read2.READ2res_u.readok.data.nfsdata2_val = NULL;
      pres->res_read2.READ2res_u.readok.data.nfsdata2_len = 0;
      pres->res_attr2.status = NFS_OK;
    }

  /* Convert file handle into a cache entry */
  if((pentry = nfs_FhandleToCache(preq->rq_vers,
                                  &(parg->arg_read2.file),
                                  &(parg->arg_read3.file),
                                  NULL,
                                  &(pres->res_read2.status),
                                  &(pres->res_read3.status),
                                  NULL, &pre_attr, pcontext, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  if((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_read3.file))))
  {
    rc = nfs3_Read_Xattr(parg, pexport, pcontext, preq, pres);
    goto out;
  }

  if(cache_inode_access(pentry,
                        FSAL_READ_ACCESS,
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
      rc = NFS_REQ_OK;
      goto out;
    }

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

      rc = NFS_REQ_OK;
      goto out;
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
  if((pexport->export_perms.options & EXPORT_OPTION_MAXOFFSETREAD) == EXPORT_OPTION_MAXOFFSETREAD)
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
              nfs_SetPostOpAttr(pexport, NULL, 
                                &(pres->res_read3.READ3res_u.resfail.file_attributes));
              break;
            }
          rc = NFS_REQ_OK;
          goto out;
        }
    }

  /*
   * We should not exceed the FSINFO rtmax field for
   * the size 
   */
  if(((pexport->export_perms.options & EXPORT_OPTION_MAXREAD) == EXPORT_OPTION_MAXREAD) &&
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
      nfs_read_ok(pexport, preq, pres, NULL, 0, &pre_attr, 0);
      rc = NFS_REQ_OK;
      goto out;
    }
  else
    {
      data = gsh_malloc(size);
      if(data == NULL)
        {
          rc = NFS_REQ_DROP;
          goto out;
        }

      if((cache_inode_rdwr(pentry,
                           CACHE_INODE_READ,
                           offset,
                           size,
                           &read_size,
                           data,
                           &eof_met,
                           pcontext,
                           CACHE_INODE_SAFE_WRITE_TO_FS,
                           &cache_status) == CACHE_INODE_SUCCESS) &&
         (cache_inode_getattr(pentry, &attr, pcontext,
                              &cache_status)) == CACHE_INODE_SUCCESS)

        {
          nfs_read_ok(pexport, preq, pres, data, read_size, &attr, 
                      ((offset + read_size) >= attr.filesize));
          rc = NFS_REQ_OK;
          goto out;
        }
      gsh_free(data);
    }

  /* If we are here, there was an error */
  rc = nfs_SetFailedStatus(pexport, preq->rq_vers, cache_status,
                           &pres->res_read2.status, &pres->res_read3.status,
                           &(pres->res_read3.READ3res_u.resfail.file_attributes),
                           NULL, NULL, NULL, NULL);

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);

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
    gsh_free(resp->res_read2.READ2res_u.readok.data.nfsdata2_val);
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
    gsh_free(resp->res_read3.READ3res_u.resok.data.data_val);
}                               /* nfs3_Read_Free */
