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
 * \file    nfs2_Root.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:49 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs2_Root.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs2_Root: Implements NFSPROC2_ROOT.
 *
 * Implements NFSPROC2_ROOT.
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
int nfs2_Root(nfs_arg_t * parg,
              exportlist_t * pexport,
              fsal_op_context_t * pcontext,
              cache_inode_client_t * pclient,
              hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  /* This is an unsupported function, it is never used */
  DisplayLogJdLevel(pclient->log_outputs, NIV_CRIT,
                    "NFS2_ROOT:  /!\\ | Received unexpected call to deprecated function NFS2PROC_ROOT");
  return NFS_REQ_OK;
}                               /* nfs2_Root */

/**
 * nfs2_Root_Free: Frees the result structure allocated for nfs2_Root.
 * 
 * Frees the result structure allocated for nfs2_Root.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Root_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* nfs2_Root_Free */
