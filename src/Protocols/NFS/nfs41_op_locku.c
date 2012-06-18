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
 * @file    nfs41_op_locku.c
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

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * @brief The NFS4_OP_LOCKU operation.
 *
 * This function implements the NFS4_OP_LOCKU operation.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[in]     resp  Results for nfs4_op
 *
 * @return per RFC5661 p. 368
 *
 * @see nfs4_Compound
 *
 */

#define arg_LOCKU4 op->nfs_argop4_u.oplocku
#define res_LOCKU4 resp->nfs_resop4_u.oplocku

int nfs41_op_locku(struct nfs_argop4 *op, compound_data_t *data,
                   struct nfs_resop4 *resp)
{
  state_status_t      state_status;
  state_t           * state_found = NULL;
  state_owner_t     * lock_owner;
  fsal_lock_param_t   lock_desc;
  unsigned int        rc = 0;
  const char        * tag = "LOCKU";

  LogDebug(COMPONENT_NFS_V4_LOCK,
           "Entering NFS v4.1 LOCKU handler -----------------------------------------------------");

  /* Initialize to sane default */
  resp->resop = NFS4_OP_LOCKU;
  res_LOCKU4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * LOCKU is done only on a file
   */
  res_LOCKU4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_LOCKU4.status != NFS4_OK)
    return res_LOCKU4.status;

  /* Convert lock parameters to internal types */
  switch(arg_LOCKU4.locktype)
    {
      case READ_LT:
      case READW_LT:
        lock_desc.lock_type = FSAL_LOCK_R;
        break;

      case WRITE_LT:
      case WRITEW_LT:
        lock_desc.lock_type = FSAL_LOCK_W;
        break;
    }

  lock_desc.lock_start = arg_LOCKU4.offset;

  if(arg_LOCKU4.length != STATE_LOCK_OFFSET_EOF)
    lock_desc.lock_length = arg_LOCKU4.length;
  else
    lock_desc.lock_length = 0;

  /* Check stateid correctness and get pointer to state */
  if((rc = nfs4_Check_Stateid(&arg_LOCKU4.lock_stateid,
                              data->current_entry,
                              &state_found,
                              data,
                              STATEID_SPECIAL_FOR_LOCK,
                              tag)) != NFS4_OK)
    {
      res_LOCKU4.status = rc;
      return res_LOCKU4.status;
    }

  lock_owner = state_found->state_powner;

  /* Lock length should not be 0 */
  if(arg_LOCKU4.length == 0LL)
    {
      res_LOCKU4.status = NFS4ERR_INVAL;
      return res_LOCKU4.status;
    }

  /* Check for range overflow
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if(lock_desc.lock_length > (STATE_LOCK_OFFSET_EOF - lock_desc.lock_start))
    {
      res_LOCKU4.status = NFS4ERR_INVAL;
      return res_LOCKU4.status;
    }

  LogLock(COMPONENT_NFS_V4_LOCK, NIV_FULL_DEBUG,
          tag,
          data->current_entry,
          lock_owner,
          &lock_desc);

  /* Now we have a lock owner and a stateid.
   * Go ahead and push unlock into SAL (and FSAL).
   */
  if(state_unlock(data->current_entry,
                  data->pexport,
                  lock_owner,
                  state_found,
                  &lock_desc,
                  &state_status) != STATE_SUCCESS)
    {
      res_LOCKU4.status = nfs4_Errno_state(state_status);
      return res_LOCKU4.status;
    }

  /* Successful exit */
  res_LOCKU4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(state_found,
                 &res_LOCKU4.LOCKU4res_u.lock_stateid,
                 data,
                 tag);

  return res_LOCKU4.status;

} /* nfs41_op_locku */

/**
 * @brief Free memory allocated for LOCKU result.
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_LOCKU operation.
 *
 * @param[in,out] resp nfs4_op results
 *
 */
void nfs41_op_locku_Free(LOCKU4res * resp)
{
  return;
} /* nfs41_op_locku_Free */
