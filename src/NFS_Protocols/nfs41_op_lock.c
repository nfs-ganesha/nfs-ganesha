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
 * \file    nfs41_op_lock.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_file_handle.h"
#include "nfs_tools.h"

/**
 * 
 * nfs41_op_lock: The NFS4_OP_LOCK operation. 
 *
 * This function implements the NFS4_OP_LOCK operation.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 *
 * @see all the nfs41_op_<*> function
 * @see nfs4_Compound
 *
 */

#define arg_LOCK4 op->nfs_argop4_u.oplock
#define res_LOCK4 resp->nfs_resop4_u.oplock

extern char all_zero[];
extern char all_one[12];

int nfs41_op_lock(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs41_op_lock";

  cache_inode_status_t cache_status;
  cache_inode_state_data_t candidate_data;
  cache_inode_state_type_t candidate_type;
  int rc = 0;
  cache_inode_state_t *file_state = NULL;
  cache_inode_state_t *pstate_exists = NULL;
  cache_inode_state_t *pstate_open = NULL;
  cache_inode_state_t *pstate_found = NULL;
  cache_inode_state_t *pstate_previous_iterate = NULL;
  cache_inode_state_t *pstate_found_iterate = NULL;
  cache_inode_open_owner_t *powner = NULL;
  cache_inode_open_owner_t *popen_owner = NULL;
  cache_inode_open_owner_t *powner_exists = NULL;
  cache_inode_open_owner_name_t *powner_name = NULL;
  uint64_t a, b, a1, b1;
  unsigned int overlap = FALSE;
  cache_inode_open_owner_name_t owner_name;

  /* Lock are not supported */
  resp->resop = NFS4_OP_LOCK;
  res_LOCK4.status = NFS4ERR_LOCK_NOTSUPP;

#ifndef _WITH_NFSV4_LOCKS
  return res_LOCK4.status;
#else

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOCK4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_BADHANDLE;
      return res_LOCK4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOCK4.status = NFS4ERR_FHEXPIRED;
      return res_LOCK4.status;
    }

  /* Commit is done only on a file */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* Type of the entry is not correct */
      switch (data->current_filetype)
        {
        case DIR_BEGINNING:
        case DIR_CONTINUE:
          res_LOCK4.status = NFS4ERR_ISDIR;
          break;
        default:
          res_LOCK4.status = NFS4ERR_INVAL;
          break;
        }
    }

  /* Lock length should not be 0 */
  if(arg_LOCK4.length == 0LL)
    {
      res_LOCK4.status = NFS4ERR_INVAL;
      return res_LOCK4.status;
    }

  /* Check for range overflow 
   * Remember that a length with all bits set to 1 means "lock until the end of file" (RFC3530, page 157) */
  if(arg_LOCK4.length != 0xffffffffffffffffLL)
    {
      /* Comparing beyond 2^64 is not possible int 64 bits precision, but off+len > 2^64 is equivalent to len > 2^64 - off */
      if(arg_LOCK4.length > (0xffffffffffffffffLL - arg_LOCK4.offset))
        {
          res_LOCK4.status = NFS4ERR_INVAL;
          return res_LOCK4.status;
        }
    }

  switch (arg_LOCK4.locker.new_lock_owner)
    {
    case TRUE:
      if(cache_inode_get_state(arg_LOCK4.locker.locker4_u.open_owner.open_stateid.other,
                               &pstate_open,
                               data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_LOCK4.status = NFS4ERR_STALE_STATEID;
          return res_LOCK4.status;
        }

      popen_owner = pstate_open->powner;

      break;

    case FALSE:
      if(cache_inode_get_state(arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.other,
                               &pstate_exists,
                               data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
        {
          /* Handle the case where all-0 stateid is used */
          if(!
             (!memcmp
              ((char *)all_zero,
               arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.other, 12)
              && arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.seqid == 0))
            {
              if(cache_status == CACHE_INODE_NOT_FOUND)
                res_LOCK4.status = NFS4ERR_STALE_STATEID;
              else
                res_LOCK4.status = NFS4ERR_INVAL;

              return res_LOCK4.status;
            }
        }

      if(pstate_exists != NULL)
        {
          /* Get the old lockowner. We can do the following 'cast', in NFSv4 lock_owner4 and open_owner4
           * are different types but with the same definition*/
          powner = pstate_exists->powner;
          powner_exists = pstate_exists->powner;
          popen_owner = pstate_exists->powner->related_owner;
        }

      break;
    }                           /* switch( arg_LOCK4.locker.new_lock_owner ) */

  /* Check for conflicts with previously obtained states */
  /* At this step of the code, if pstate_exists == NULL, then all-0 or all-1 stateid is used */

  /* loop into the states related to this pentry to find the related lock */
  pstate_found_iterate = NULL;
  pstate_previous_iterate = pstate_found;
  do
    {
      cache_inode_state_iterate(data->current_entry,
                                &pstate_found_iterate,
                                pstate_previous_iterate,
                                data->pclient, data->pcontext, &cache_status);
      if((cache_status == CACHE_INODE_STATE_ERROR)
         || (cache_status == CACHE_INODE_INVALID_ARGUMENT))
        {
          res_LOCK4.status = NFS4ERR_INVAL;
          return res_LOCK4.status;
        }

      if(pstate_found_iterate != NULL)
        {
          if(pstate_found_iterate->state_type == CACHE_INODE_STATE_LOCK)
            {
              /* Check lock upgrade/downgrade */
              if(pstate_exists != NULL)
                {
                  if((pstate_exists == pstate_found_iterate) &&
                     (pstate_exists->state_data.lock.lock_type != arg_LOCK4.locktype))
                    LogCrit(COMPONENT_NFS_V4,
                        "&&&&&&&& CAS FOIREUX !!!!!!!!!!!!!!!!!!\n");
                }

              a = pstate_found_iterate->state_data.lock.offset;
              b = pstate_found_iterate->state_data.lock.offset +
                  pstate_found_iterate->state_data.lock.length;
              a1 = arg_LOCK4.offset;
              b1 = arg_LOCK4.offset + arg_LOCK4.length;

              /* Locks overlap is a <= a1 < b or a < b1 <= b */
              overlap = FALSE;
              if(a <= a1)
                {
                  if(a1 < b)
                    overlap = TRUE;
                }
              else
                {
                  if(a < b1)
                    {
                      if(b1 <= b)
                        overlap = TRUE;
                    }
                }

              if(overlap == TRUE)

                /* Locks overlap is a < a1 < b or a < b1 < b */
                if(overlap == TRUE)
                  {
                    /* Locks are overlapping */

                    /* If both lock are READ, this is not a case of error  */
                    if((arg_LOCK4.locktype != READ_LT)
                       || (pstate_found_iterate->state_data.lock.lock_type != READ_LT))
                      {
                        /* Overlapping lock is found, if owner is different than the calling owner, return NFS4ERR_DENIED */
                        if((pstate_exists != NULL) &&   /* all-O/all-1 stateid is considered a different owner */
                           ((powner_exists->owner_len ==
                             pstate_found_iterate->powner->owner_len)
                            &&
                            (!memcmp
                             (powner_exists->owner_val,
                              pstate_found_iterate->powner->owner_val,
                              pstate_found_iterate->powner->owner_len))))
                          {
                            /* The calling state owner is the same. There is a discussion on this case at page 161 of RFC3530. I choose to ignore this
                             * lock and continue iterating on the other states */
                          }
                        else
                          {
                            /* A  conflicting lock from a different lock_owner, returns NFS4ERR_DENIED */
                            res_LOCK4.LOCK4res_u.denied.offset =
                                pstate_found_iterate->state_data.lock.offset;
                            res_LOCK4.LOCK4res_u.denied.length =
                                pstate_found_iterate->state_data.lock.length;
                            res_LOCK4.LOCK4res_u.denied.locktype =
                                pstate_found_iterate->state_data.lock.lock_type;
                            res_LOCK4.LOCK4res_u.denied.owner.owner.owner_len =
                                pstate_found_iterate->powner->owner_len;
                            res_LOCK4.LOCK4res_u.denied.owner.owner.owner_val =
                                pstate_found_iterate->powner->owner_val;
                            res_LOCK4.status = NFS4ERR_DENIED;
                            return res_LOCK4.status;
                          }
                      }
                  }
            }
          /* if( ... == CACHE_INODE_STATE_LOCK */
          if(pstate_found_iterate->state_type == CACHE_INODE_STATE_SHARE)
            {
              /* In a correct POSIX behavior, a write lock should not be allowed on a read-mode file */
              if((pstate_found_iterate->state_data.share.
                  share_deny & OPEN4_SHARE_DENY_WRITE)
                 && !(pstate_found_iterate->state_data.share.
                      share_access & OPEN4_SHARE_ACCESS_WRITE)
                 && (arg_LOCK4.locktype == WRITE_LT))
                {
                  /* A conflicting open state, return NFS4ERR_OPENMODE
                   * This behavior is implemented to comply with newpynfs's test LOCK4 */
                  res_LOCK4.status = NFS4ERR_OPENMODE;
                  return res_LOCK4.status;

                }
            }

        }                       /* if( pstate_found_iterate != NULL ) */
      pstate_previous_iterate = pstate_found_iterate;
    }
  while(pstate_found_iterate != NULL);

  switch (arg_LOCK4.locker.new_lock_owner)
    {
    case TRUE:
      /* A lock owner is always associated with a previously made open
       * which has itself a previously made stateid */

      /* Check stateid correctness */
      if((rc = nfs4_Check_Stateid(&arg_LOCK4.locker.locker4_u.open_owner.open_stateid,
                                  data->current_entry,
                                  data->psession->clientid)) != NFS4_OK)
        {
          res_LOCK4.status = rc;
          return res_LOCK4.status;
        }

      /* An open state has been found. Check its type */
      if(pstate_open->state_type != CACHE_INODE_STATE_SHARE)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          return res_LOCK4.status;
        }

      /* Sanity check : Is this the right file ? */
      if(pstate_open->pentry != data->current_entry)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          return res_LOCK4.status;
        }

      /* Is this open_owner known ? */
      if(!nfs_convert_open_owner
         ((open_owner4 *) & arg_LOCK4.locker.locker4_u.open_owner.lock_owner,
          &owner_name))
        {
          res_LOCK4.status = NFS4ERR_SERVERFAULT;
          return res_LOCK4.status;
        }

      /* This open owner is not known yet, allocated and set up a new one */
      GET_PREALLOC(powner,
                   data->pclient->pool_open_owner,
                   data->pclient->nb_pre_state_v4, cache_inode_open_owner_t, next);

      GET_PREALLOC(powner_name,
                   data->pclient->pool_open_owner_name,
                   data->pclient->nb_pre_state_v4, cache_inode_open_owner_name_t, next);

      memcpy((char *)powner_name, (char *)&owner_name,
             sizeof(cache_inode_open_owner_name_t));

      /* set up the content of the open_owner */
      powner->confirmed = FALSE;
      powner->seqid = 0;
      powner->related_owner = pstate_open->powner;
      powner->next = NULL;
      powner->clientid = arg_LOCK4.locker.locker4_u.open_owner.lock_owner.clientid;
      powner->owner_len =
          arg_LOCK4.locker.locker4_u.open_owner.lock_owner.owner.owner_len;
      memcpy((char *)powner->owner_val,
             (char *)arg_LOCK4.locker.locker4_u.open_owner.lock_owner.owner.owner_val,
             arg_LOCK4.locker.locker4_u.open_owner.lock_owner.owner.owner_len);
      powner->owner_val[powner->owner_len] = '\0';

      pthread_mutex_init(&powner->lock, NULL);

      if(!nfs_open_owner_Set(powner_name, powner))
        {
          res_LOCK4.status = NFS4ERR_SERVERFAULT;
          return res_LOCK4.status;
        }

      /* Prepare state management structure */
      candidate_type = CACHE_INODE_STATE_LOCK;
      candidate_data.lock.lock_type = arg_LOCK4.locktype;
      candidate_data.lock.offset = arg_LOCK4.offset;
      candidate_data.lock.length = arg_LOCK4.length;
      candidate_data.lock.popenstate = (void *)pstate_open;

      /* Add the lock state to the lock table */
      if(cache_inode_add_state(data->current_entry,
                               candidate_type,
                               &candidate_data,
                               powner,
                               data->pclient,
                               data->pcontext,
                               &file_state, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_LOCK4.status = NFS4ERR_STALE_STATEID;
          return res_LOCK4.status;
        }

        /** @todo BUGAZOMEU: Manage the case if lock conflicts */
      res_LOCK4.LOCK4res_u.resok4.lock_stateid.seqid = 0;
      memcpy(res_LOCK4.LOCK4res_u.resok4.lock_stateid.other, file_state->stateid_other,
             12);

      /* update the lock counter in the related open-stateid */
      pstate_open->state_data.share.lockheld += 1;

      break;

    case FALSE:
      /* The owner already exists, use the provided owner to create a new state */
      /* Get the former state */
      if(cache_inode_get_state(arg_LOCK4.locker.locker4_u.lock_owner.lock_stateid.other,
                               &pstate_found,
                               data->pclient, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_LOCK4.status = NFS4ERR_STALE_STATEID;
          return res_LOCK4.status;
        }

      /* An lock state has been found. Check its type */
      if(pstate_found->state_type != CACHE_INODE_STATE_LOCK)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          return res_LOCK4.status;
        }

      /* Sanity check : Is this the right file ? */
      if(pstate_found->pentry != data->current_entry)
        {
          res_LOCK4.status = NFS4ERR_BAD_STATEID;
          return res_LOCK4.status;
        }

      memcpy(res_LOCK4.LOCK4res_u.resok4.lock_stateid.other, pstate_found->stateid_other,
             12);

      break;
    }                           /* switch( arg_LOCK4.locker.new_lock_owner ) */

  res_LOCK4.status = NFS4_OK;
  return res_LOCK4.status;
#endif
}                               /* nfs41_op_lock */

/**
 * nfs41_op_lock_Free: frees what was allocared to handle nfs41_op_lock.
 * 
 * Frees what was allocared to handle nfs41_op_lock.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_lock_Free(LOCK4res * resp)
{
  /* Nothing to Mem_Free */
  return;
}                               /* nfs41_op_lock_Free */
