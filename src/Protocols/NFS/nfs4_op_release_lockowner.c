/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs4_op_release_lockowner.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "cache_inode.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/**
 * @brief NFS4_OP_RELEASE_LOCKOWNER
 *
 * This function implements the NFS4_OP_RELEASE_LOCKOWNER function.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @retval NFS4_OK or errors for NFSv4.0.
 * @retval NFS4ERR_NOTSUPP for NFSv4.1.
 */

#define arg_RELEASE_LOCKOWNER4 op->nfs_argop4_u.oprelease_lockowner
#define res_RELEASE_LOCKOWNER4 resp->nfs_resop4_u.oprelease_lockowner

int nfs4_op_release_lockowner(struct nfs_argop4 * op,
                              compound_data_t   * data,
                              struct nfs_resop4 * resp)
{
  nfs_client_id_t         * nfs_client_id;
  state_owner_t           * lock_owner;
  state_nfs4_owner_name_t   owner_name;

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4 RELEASE_LOCKOWNER handler ----------------------");

  resp->resop = NFS4_OP_RELEASE_LOCKOWNER;
  res_RELEASE_LOCKOWNER4.status = NFS4_OK;

  if (data->minorversion > 0)
    {
      res_RELEASE_LOCKOWNER4.status = NFS4ERR_NOTSUPP;
      return res_RELEASE_LOCKOWNER4.status;
    }

  /* Check clientid */
  if(nfs_client_id_get_confirmed(arg_RELEASE_LOCKOWNER4.lock_owner.clientid,
                                 &nfs_client_id) != CLIENT_ID_SUCCESS)
    {
      res_RELEASE_LOCKOWNER4.status = NFS4ERR_STALE_CLIENTID;
      goto out2;
    }

  /* The protocol doesn't allow for EXPIRED, so return STALE_CLIENTID */
  P(nfs_client_id->cid_mutex);

  if(!reserve_lease(nfs_client_id))
    {
      V(nfs_client_id->cid_mutex);

      dec_client_id_ref(nfs_client_id);

      res_RELEASE_LOCKOWNER4.status = NFS4ERR_STALE_CLIENTID;
      goto out2;
    }

  V(nfs_client_id->cid_mutex);

  /* look up the lock owner and see if we can find it */
  convert_nfs4_lock_owner(&arg_RELEASE_LOCKOWNER4.lock_owner, &owner_name, 0LL);

  if(!nfs4_owner_Get_Pointer(&owner_name, &lock_owner))
    {
      /* the owner doesn't exist, we are done */
      LogDebug(COMPONENT_NFS_V4_LOCK,
               "lock owner does not exist");
      res_RELEASE_LOCKOWNER4.status = NFS4_OK;
      goto out1;
    }

  /* got the owner, does it still have any locks being held */
  if(!glist_empty(&lock_owner->so_lock_list))
    {
      res_RELEASE_LOCKOWNER4.status = NFS4ERR_LOCKS_HELD;
    }
  else
    {
      /* found the lock owner and it doesn't have any locks, release it */
      release_lockstate(lock_owner);

      res_RELEASE_LOCKOWNER4.status = NFS4_OK;
    }

  /* Release the reference to the lock owner acquired via
     nfs4_owner_Get_Pointer */
  dec_state_owner_ref(lock_owner);

 out1:

  /* Update the lease before exit */
  P(nfs_client_id->cid_mutex);

  update_lease(nfs_client_id);

  V(nfs_client_id->cid_mutex);

  dec_client_id_ref(nfs_client_id);

 out2:

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Leaving NFS v4 RELEASE_LOCKOWNER handler -----------------------");

  return res_RELEASE_LOCKOWNER4.status;
}                               /* nfs4_op_release_lock_owner */

/**
 * @brief Free memory allocated for REELASE_LOCKOWNER result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_REELASE_LOCKOWNER operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_release_lockowner_Free(RELEASE_LOCKOWNER4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_release_lockowner_Free */
