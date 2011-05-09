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
 * \file    nfs41_op_sequence.c
 * \author  $Author: deniel $
 * \brief   Routines used for managing the NFS4_OP_SEQUENCE operation.
 *
 * nfs41_op_sequence.c : Routines used for managing the NFS4_OP_SEQUENCE operation.
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
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 *
 * nfs41_op_sequence: the NFS4_OP_SEQUENCE operation
 *
 * This functions handles the NFS4_OP_SEQUENCE operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs4_op_<*> function
 * @see nfs4_Compound
 *
 */
int nfs41_op_sequence(struct nfs_argop4 *op,
                      compound_data_t * data, struct nfs_resop4 *resp)
{
#define arg_SEQUENCE4  op->nfs_argop4_u.opsequence
#define res_SEQUENCE4  resp->nfs_resop4_u.opsequence

  nfs41_session_t *psession;

  resp->resop = NFS4_OP_SEQUENCE;
  res_SEQUENCE4.sr_status = NFS4_OK;

  /* OP_SEQUENCE is always the first operation of the request */
  if(data->oppos != 0)
    {
      res_SEQUENCE4.sr_status = NFS4ERR_SEQUENCE_POS;
      return res_SEQUENCE4.sr_status;
    }

  if(!nfs41_Session_Get_Pointer(arg_SEQUENCE4.sa_sessionid, &psession))
    {
      res_SEQUENCE4.sr_status = NFS4ERR_BADSESSION;
      return res_SEQUENCE4.sr_status;
    }

  /* Check is slot is compliant with ca_maxrequests */
  if(arg_SEQUENCE4.sa_slotid >= psession->fore_channel_attrs.ca_maxrequests)
    {
      res_SEQUENCE4.sr_status = NFS4ERR_BADSLOT;
      return res_SEQUENCE4.sr_status;
    }

  /* By default, no DRC replay */
  data->use_drc = FALSE;

  P(psession->slots[arg_SEQUENCE4.sa_slotid].lock);
  if(psession->slots[arg_SEQUENCE4.sa_slotid].sequence + 1 != arg_SEQUENCE4.sa_sequenceid)
    {
      if(psession->slots[arg_SEQUENCE4.sa_slotid].sequence == arg_SEQUENCE4.sa_sequenceid)
        {
          if(psession->slots[arg_SEQUENCE4.sa_slotid].cache_used == TRUE)
            {
              /* Replay operation through the DRC */
              data->use_drc = TRUE;
              data->pcached_res = psession->slots[arg_SEQUENCE4.sa_slotid].cached_result;

              res_SEQUENCE4.sr_status = NFS4_OK;
              return res_SEQUENCE4.sr_status;
            }
          else
            {
              /* Illegal replay */
              res_SEQUENCE4.sr_status = NFS4ERR_RETRY_UNCACHED_REP;
              return res_SEQUENCE4.sr_status;
            }
        }
      V(psession->slots[arg_SEQUENCE4.sa_slotid].lock);
      res_SEQUENCE4.sr_status = NFS4ERR_SEQ_MISORDERED;
      return res_SEQUENCE4.sr_status;
    }

  /* Keep memory of the session in the COMPOUND's data */
  data->psession = psession;

  /* Update the sequence id within the slot */
  psession->slots[arg_SEQUENCE4.sa_slotid].sequence += 1;

  memcpy((char *)res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_sessionid,
         (char *)arg_SEQUENCE4.sa_sessionid, NFS4_SESSIONID_SIZE);
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_sequenceid =
      psession->slots[arg_SEQUENCE4.sa_slotid].sequence;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_slotid = arg_SEQUENCE4.sa_slotid;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_highest_slotid = NFS41_NB_SLOTS - 1;
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_target_highest_slotid = arg_SEQUENCE4.sa_slotid;    /* Maybe not the best choice */
  res_SEQUENCE4.SEQUENCE4res_u.sr_resok4.sr_status_flags = 0;   /* What is to be set here ? */

  if(arg_SEQUENCE4.sa_cachethis == TRUE)
    {
      data->pcached_res = psession->slots[arg_SEQUENCE4.sa_slotid].cached_result;
      psession->slots[arg_SEQUENCE4.sa_slotid].cache_used = TRUE;
    }
  else
    {
      data->pcached_res = NULL;
      psession->slots[arg_SEQUENCE4.sa_slotid].cache_used = FALSE;
    }
  V(psession->slots[arg_SEQUENCE4.sa_slotid].lock);

  res_SEQUENCE4.sr_status = NFS4_OK;
  return res_SEQUENCE4.sr_status;
}                               /* nfs41_op_sequence */

/**
 * nfs41_op_sequence_Free: frees what was allocared to handle nfs41_op_sequence.
 * 
 * Frees what was allocared to handle nfs41_op_sequence.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_sequence_Free(SEQUENCE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_sequence_Free */
