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
 * \file    nfs3_Fsinfo.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.11 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs3_Fsinfo.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
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

/**
 * @brief Implements NFSPROC3_FSINFO
 *
 * Implements NFSPROC3_FSINFO.
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

int nfs3_Fsinfo(nfs_arg_t *parg,
                exportlist_t *pexport,
                fsal_op_context_t *pcontext,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t * pres)
{
  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t attr;
  int rc = NFS_REQ_OK;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char                  str[LEN_FH_STR];
      struct display_buffer dspbuf = {sizeof(str), str, str};

      (void) display_fhandle3(&dspbuf, (nfs_fh3 *) parg);

      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs3_Fsinfo handle: %s", str);
    }

  /* to avoid setting it on each error case */
  pres->res_fsinfo3.FSINFO3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  /* Convert file handle into a fsal_handle */
  if(nfs3_FhandleToFSAL(&(parg->arg_fsinfo3.fsroot), &fsal_data.fh_desc, pcontext) == 0)
    {
      rc = NFS_REQ_DROP;
      goto out;
    }

  /* Get the entry in the cache_inode */
  if((pentry = cache_inode_get(&fsal_data,
                               &attr,
                               pcontext,
                               NULL,
                               &cache_status)) == NULL)
    {
      /* Stale NFS FH ? */
      pres->res_fsinfo3.status = NFS3ERR_STALE;
      rc = NFS_REQ_OK;
      goto out;
    }

  /*
   * New fields were added to nfs_config_t to handle this value. We use
   * them
   */

#define FSINFO_FIELD pres->res_fsinfo3.FSINFO3res_u.resok
  FSINFO_FIELD.rtmax = pexport->MaxRead < nfs_param.core_param.max_send_buffer_size ? pexport->MaxRead : nfs_param.core_param.max_send_buffer_size;
  FSINFO_FIELD.rtpref = pexport->PrefRead;

  /* This field is generally unused, it will be removed in V4 */
  FSINFO_FIELD.rtmult = DEV_BSIZE;

  FSINFO_FIELD.wtmax = pexport->MaxWrite < nfs_param.core_param.max_recv_buffer_size ? pexport->MaxWrite : nfs_param.core_param.max_recv_buffer_size;
  FSINFO_FIELD.wtpref = pexport->PrefWrite;

  /* This field is generally unused, it will be removed in V4 */
  FSINFO_FIELD.wtmult = DEV_BSIZE;

  FSINFO_FIELD.dtpref = pexport->PrefReaddir;

  FSINFO_FIELD.maxfilesize = FSINFO_MAX_FILESIZE;
  FSINFO_FIELD.time_delta.seconds = 1;
  FSINFO_FIELD.time_delta.nseconds = 0;

  LogFullDebug(COMPONENT_NFSPROTO,
               "rtmax = %d | rtpref = %d | trmult = %d",
               FSINFO_FIELD.rtmax,
               FSINFO_FIELD.rtpref,
               FSINFO_FIELD.rtmult);
  LogFullDebug(COMPONENT_NFSPROTO,
               "wtmax = %d | wtpref = %d | wrmult = %d",
               FSINFO_FIELD.wtmax,
               FSINFO_FIELD.wtpref,
               FSINFO_FIELD.wtmult);
  LogFullDebug(COMPONENT_NFSPROTO,
               "dtpref = %d | maxfilesize = %llu ",
               FSINFO_FIELD.dtpref,
               FSINFO_FIELD.maxfilesize);

  /*
   * Allow all kinds of operations to be performed on the server
   * through NFS v3
   */
  FSINFO_FIELD.properties = FSF3_LINK | FSF3_SYMLINK |
       FSF3_HOMOGENEOUS | FSF3_CANSETTIME;

  nfs_SetPostOpAttr(pexport,
                    &attr,
                    &(pres->res_fsinfo3.FSINFO3res_u.resok.obj_attributes));
  pres->res_fsinfo3.status = NFS3_OK;

 out:

  if (pentry)
    {
      cache_inode_put(pentry);
    }

  return rc;
}                               /* nfs3_Fsinfo */

/**
 * nfs3_Fsinfo_Free: Frees the result structure allocated for nfs3_Fsinfo.
 *
 * Frees the result structure allocated for nfs3_Fsinfo.
 *
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Fsinfo_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs3_Fsinfo_Free */
