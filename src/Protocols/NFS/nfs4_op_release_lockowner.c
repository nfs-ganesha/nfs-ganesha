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
 * \file    nfs4_op_release_lockowner.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_release_lockowner.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/**
 * nfs4_op_release_lockowner: The NFS4_OP_RELEASE_LOCKOWNER
 * 
 * Implements the NFS4_OP_RELEASE_LOCKOWNER
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if ok, any other value show an error.
 *
 */
#define arg_RELEASE_LOCKOWNER4 op->nfs_argop4_u.oprelease_lockowner
#define res_RELEASE_LOCKOWNER4 resp->nfs_resop4_u.oprelease_lockowner

int nfs4_op_release_lockowner(struct nfs_argop4 * op,
                              compound_data_t   * data,
                              struct nfs_resop4 * resp)
{
  nfs_client_id_t         * pnfs_client_id;
  state_owner_t           * plock_owner;
  state_nfs4_owner_name_t   owner_name;

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 RELEASE_LOCKOWNER handler -----------------------------------------------------");

  resp->resop = NFS4_OP_RELEASE_LOCKOWNER;
  res_RELEASE_LOCKOWNER4.status = NFS4_OK;

  /* Check clientid */
  if(nfs_client_id_get_confirmed(arg_RELEASE_LOCKOWNER4.lock_owner.clientid,
                                 &pnfs_client_id) != CLIENT_ID_SUCCESS)
    {
      res_RELEASE_LOCKOWNER4.status = NFS4ERR_STALE_CLIENTID;
      goto out2;
    }

  /* The protocol doesn't allow for EXPIRED, so return STALE_CLIENTID */
  P(pnfs_client_id->cid_mutex);

  if(!reserve_lease(pnfs_client_id))
    {
      V(pnfs_client_id->cid_mutex);

      dec_client_id_ref(pnfs_client_id);

      res_RELEASE_LOCKOWNER4.status = NFS4ERR_STALE_CLIENTID;
      goto out2;
    }

  V(pnfs_client_id->cid_mutex);

  /* look up the lock owner and see if we can find it */
  convert_nfs4_lock_owner(&arg_RELEASE_LOCKOWNER4.lock_owner, &owner_name);

  /* If this open owner is not known yet, allocated and set up a new one */
  plock_owner = create_nfs4_owner(&owner_name,
                                  pnfs_client_id,
                                  STATE_OPEN_OWNER_NFSV4,
                                  NULL,
                                  0,
                                  NULL,
                                  CARE_NOT);

  if(plock_owner == NULL)
    {
      /* the owner doesn't exist, we are done */
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "lock owner does not exist");
      res_RELEASE_LOCKOWNER4.status = NFS4_OK;
      goto out1;
    }

  P(plock_owner->so_mutex);

  /* got the owner, does it still have any locks being held */
  if(!glist_empty(&plock_owner->so_lock_list))
    {
      V(plock_owner->so_mutex);

      res_RELEASE_LOCKOWNER4.status = NFS4ERR_LOCKS_HELD;
    }
  else
    {
      V(plock_owner->so_mutex);

      /* found the lock owner and it doesn't have any locks, release it */
      release_lockstate(plock_owner);

      res_RELEASE_LOCKOWNER4.status = NFS4_OK;
    }

  /* Release the reference to the lock owner acquired via create_nfs4_owner */
  dec_state_owner_ref(plock_owner);

 out1:

  /* Update the lease before exit */
  P(pnfs_client_id->cid_mutex);

  update_lease(pnfs_client_id);

  V(pnfs_client_id->cid_mutex);

  dec_client_id_ref(pnfs_client_id);

 out2:

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Leaving NFS v4 RELEASE_LOCKOWNER handler -----------------------------------------------------");

  return res_RELEASE_LOCKOWNER4.status;
}                               /* nfs4_op_release_lock_owner */

/**
 * nfs4_op_release_lockowner_Free: frees what was allocared to handle nfs4_op_release_lockowner.
 * 
 * Frees what was allocared to handle nfs4_op_release_lockowner.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_release_lockowner_Free(RELEASE_LOCKOWNER4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_release_lockowner_Free */
