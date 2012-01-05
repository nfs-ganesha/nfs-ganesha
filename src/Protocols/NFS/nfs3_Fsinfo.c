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
 * nfs3_Fsinfo: Implements NFSPROC3_FSINFO
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

int nfs3_Fsinfo(nfs_arg_t * parg,
                exportlist_t * pexport,
                fsal_op_context_t * pcontext,
                cache_inode_client_t * pclient,
                hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs3_Fsinfo";

  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_attrib_list_t attr;

  if(isDebug(COMPONENT_NFSPROTO))
    {
      char str[LEN_FH_STR];
      sprint_fhandle3(str, &(parg->arg_fsinfo3.fsroot));
      LogDebug(COMPONENT_NFSPROTO,
               "REQUEST PROCESSING: Calling nfs3_Fsinfo handle: %s", str);
    }

  /* to avoid setting it on each error case */
  pres->res_fsinfo3.FSINFO3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  /* Convert file handle into a fsal_handle */
  if(nfs3_FhandleToFSAL(&(parg->arg_fsinfo3.fsroot), &fsal_data.handle, pcontext) == 0)
    return NFS_REQ_DROP;

  /* Set the cookie */
  fsal_data.cookie = DIR_START;

  /* Get the entry in the cache_inode */
  if((pentry = cache_inode_get( &fsal_data,
                                pexport->cache_inode_policy,
                                &attr, 
                                ht, 
                                pclient, 
                                pcontext, 
                                &cache_status)) == NULL)
    {
      /* Stale NFS FH ? */
      pres->res_fsinfo3.status = NFS3ERR_STALE;
      return NFS_REQ_OK;
    }

  /*
   * New fields were added to nfs_config_t to handle this value. We use
   * them 
   */

#define FSINFO_FIELD pres->res_fsinfo3.FSINFO3res_u.resok
  FSINFO_FIELD.rtmax = pexport->MaxRead;
  FSINFO_FIELD.rtpref = pexport->PrefRead;

  /* This field is generally unused, it will be removed in V4 */
  FSINFO_FIELD.rtmult = DEV_BSIZE;

  FSINFO_FIELD.wtmax = pexport->MaxWrite;
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
  FSINFO_FIELD.properties = FSF3_LINK | FSF3_SYMLINK | FSF3_HOMOGENEOUS | FSF3_CANSETTIME;

  nfs_SetPostOpAttr(pcontext, pexport,
                    pentry,
                    &attr, &(pres->res_fsinfo3.FSINFO3res_u.resok.obj_attributes));
  pres->res_fsinfo3.status = NFS3_OK;

  return NFS_REQ_OK;
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
