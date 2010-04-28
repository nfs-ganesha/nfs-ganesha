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
 * \file    mnt_Umnt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:52:14 $
 * \version $Revision: 1.7 $
 * \brief   MOUNTPROC_UMNT for Mount protocol v1 and v3.
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
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"

/**
 * mnt_Umnt: The Mount proc umount function, for all versions.
 *  
 *  @param parg        [IN]
 *  @param pexportlist [IN]
 *	@param pcontextp      [IN]
 *  @param pclient     [INOUT]
 *  @param ht          [INOUT] 
 *  @param preq        [IN] 
 *	@param pres        [OUT]
 *
 */

int mnt_Umnt(nfs_arg_t * parg /* IN     */ ,
             exportlist_t * pexport /* IN     */ ,
             fsal_op_context_t * pcontext /* IN     */ ,
             cache_inode_client_t * pclient /* INOUT  */ ,
             hash_table_t * ht /* INOUT  */ ,
             struct svc_req *preq /* IN     */ ,
             nfs_res_t * pres /* OUT    */ )
{
  char *hostname;

  DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                    "REQUEST PROCESSING: Calling mnt_Umnt");

  /* @todo: BUGAZOMEU; seul AUTHUNIX est supporte */
  hostname = ((struct authunix_parms *)(preq->rq_clntcred))->aup_machname;

  if(hostname == NULL)
    {
      DisplayLogJdLevel(pclient->log_outputs, NIV_CRIT,
                        "/!\\ | UMOUNT: NULL passed as Umount argument !!!");
      return NFS_REQ_DROP;
    }

  /* BUGAZOMEU: pas de verif sur le path */
  if(!nfs_Remove_MountList_Entry(hostname, NULL))
    {
      DisplayLogJd(pclient->log_outputs,
                   "UMOUNT: /!\\ | Cannot remove mount entry for client %s", hostname);
    }
  DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
                    "UMOUNT: Client %s was removed from mount list", hostname);

  return NFS_REQ_OK;
}                               /* mnt_Umnt */

/**
 * mnt_Umnt_Free: Frees the result structure allocated for mnt_Umnt.
 * 
 * Frees the result structure allocated for mnt_UmntAll.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt_Umnt_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* mnt_Umnt_Free */
