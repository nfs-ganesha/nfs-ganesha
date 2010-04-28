/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

/**
 * \file    nfs_cb_Null.c
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/11 10:52:15 $
 * \version 1.0
 * \brief   CB_PROC_PNULL 
 *
 * nfs_cb_Null.c : CB_PROC_NULL 
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
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
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

/**
 * nfs_cb_Null: The NFS4/CB proc null function, for all versions.
 * 
 * The NFS proc null function, for all versions.
 * 
 *  @param parg        [IN]    ignored
 *  @param pexportlist [IN]    ignored
 *	@param pcontextp      [IN]    ignored
 *  @param pclient     [INOUT] ignored
 *  @param ht          [INOUT] ignored
 *  @param preq        [IN]    ignored 
 *	@param pres        [OUT]   ignored
 *
 */

int nfs_cb_Null(nfs_arg_t * parg /* IN     */ ,
                exportlist_t * pexport /* IN     */ ,
                fsal_op_context_t * pcontext /* IN     */ ,
                cache_inode_client_t * pclient /* INOUT  */ ,
                hash_table_t * ht /* INOUT  */ ,
                struct svc_req *preq /* IN     */ ,
                nfs_res_t * pres /* OUT    */ )
{
  DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                    "REQUEST PROCESSING: Calling nfs_Null");
  return NFS_OK;
}                               /* nfs_Null */

/**
 * nfs_cb_Null_Free: Frees the result structure allocated for nfs_Null.
 * 
 * Frees the result structure allocated for nfs_Null.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_cb_Null_Free(nfs_res_t * resp)
{
  /* Nothing to do here */
  return;
}                               /* nfs_Null_Free */
