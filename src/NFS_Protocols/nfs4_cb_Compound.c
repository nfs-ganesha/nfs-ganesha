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
 * \file    nfs4_cb_Compound.c
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/11 13:25:44 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4/CB COMPOUND functions.
 *
 * nfs4_cb_Compound.c : Routines used for managing the NFS4/CB COMPOUND functions.
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

typedef struct nfs4_cb_desc__
{
  char *name;
  unsigned int val;
  int (*funct) (struct nfs_cb_argop4 *, compound_data_t *, struct nfs_cb_resop4 *);
} nfs4_cb_desc_t;

/* This array maps the operation number to the related position in array optab4 */
const int cbtab4index[] = { 0, 0, 0, 0, 1, 2 };

static const nfs4_cb_desc_t cbtab4[] = {
  {"OP_CB_GETATTR", NFS4_OP_CB_GETATTR, nfs4_cb_getattr},
  {"OP_CB_RECALL", NFS4_OP_CB_RECALL, nfs4_cb_recall},
  {"OP_CB_ILLEGAL", NFS4_OP_CB_ILLEGAL, nfs4_cb_illegal},
};

/**
 * nfs4_cb_COMPOUND: The NFSCB PROC4 COMPOUND
 * 
 * Implements the NFSCB PROC4 COMPOUND.
 *
 * 
 *  @param parg        [IN]  generic nfs arguments
 *  @param pexportlist [IN]  the full export list 
 *  @param pcontex     [IN]  context for the FSAL (unused but kept for nfs functions prototype homogeneity)
 *  @param pclient     [INOUT] client resource for request management
 *  @param ht          [INOUT] cache inode hash table
 *  @param preq        [IN]  RPC svc request
 *  @param pres        [OUT] generic nfs reply
 *
 *  @see   nfs4_op_<*> functions
 *  @see   nfs4_GetPseudoFs
 * 
 */

int nfs4_cb_Compound(nfs_arg_t * parg /* IN     */ ,
                     exportlist_t * pexport /* IN     */ ,
                     fsal_op_context_t * pcontext /* IN     */ ,
                     cache_inode_client_t * pclient /* INOUT  */ ,
                     hash_table_t * ht /* INOUT */ ,
                     struct svc_req *preq /* IN     */ ,
                     nfs_res_t * pres /* OUT    */ )
{
  return 0;
}                               /* nfs4_cb_Compound */

void nfs4_cb_Compound_Free(nfs_res_t * pres)
{
  return;
}                               /* nfs4_cb_Compound_Free */
