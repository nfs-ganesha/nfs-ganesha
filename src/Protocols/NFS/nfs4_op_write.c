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
 * \file    nfs4_op_write.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:52 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_write.c : Routines used for managing the NFS4 COMPOUND functions.
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
#ifdef _PNFS_DS
#include "fsal_pnfs.h"
#endif /* _PNFS_DS */

#ifdef _PNFS_DS
static int op_dswrite(struct nfs_argop4 *op,
                      compound_data_t * data,
                      struct nfs_resop4 *resp);
#endif /* _PNFS_DS */

/**
 * nfs4_op_write: The NFS4_OP_WRITE operation
 *
 * This functions handles the NFS4_OP_WRITE operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */

#define arg_WRITE4 op->nfs_argop4_u.opwrite
#define res_WRITE4 resp->nfs_resop4_u.opwrite

int nfs4_op_write(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_size_t              size;
  fsal_size_t              written_size;
  fsal_off_t               offset;
  fsal_boolean_t           eof_met;
  cache_inode_stability_t  stability = CACHE_INODE_SAFE_WRITE_TO_FS;
  caddr_t                  bufferdata;
  stable_how4              stable_how;
  state_t                * pstate_found = NULL;
  state_t                * pstate_open;
  cache_inode_status_t     cache_status;
  cache_entry_t          * pentry = NULL;
#ifdef _USE_QUOTA
  fsal_status_t            fsal_status ;
#endif
  /* This flag is set to true in the case of an anonymous read so that
     we know to release the state lock afterward.  The state lock does
     not need to be held during a non-anonymous read, since the open
     state itself prevents a conflict. */
  bool_t                   anonymous = FALSE;

  /* Lock are not supported */
  resp->resop = NFS4_OP_WRITE;
  res_WRITE4.status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * Only files can be written
   */
  res_WRITE4.status = nfs4_sanity_check_FH(data, REGULAR_FILE);
  if(res_WRITE4.status != NFS4_OK)
    return res_WRITE4.status;

#ifdef _USE_QUOTA
    /* if quota support is active, then we should check is the FSAL allows inode creation or not */
    fsal_status = FSAL_check_quota( data->pexport->fullpath, 
                                    FSAL_QUOTA_BLOCKS,
                                    FSAL_OP_CONTEXT_TO_UID( data->pcontext ) ) ;
    if( FSAL_IS_ERROR( fsal_status ) )
     {
      res_WRITE4.status = NFS4ERR_DQUOT ;
      return res_WRITE4.status;
     }
#endif /* _USE_QUOTA */

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_write_xattr(op, data, resp);

#ifdef _PNFS_DS
  if((data->minorversion == 1) &&
     (nfs4_Is_Fh_DSHandle(&data->currentFH)))
    {
      return(op_dswrite(op, data, resp));
    }
#endif /* _PNFS_DS */

  /* vnode to manage is the current one */
  pentry = data->current_entry;

  /* Check stateid correctness and get pointer to state
   * (also checks for special stateids)
   */
  res_WRITE4.status = nfs4_Check_Stateid(&arg_WRITE4.stateid,
                                         pentry,
                                         &pstate_found,
                                         data,
                                         STATEID_SPECIAL_ANY,
                                         0,FALSE,                  /* do not check owner seqid */
                                         "WRITE");
  if(res_WRITE4.status != NFS4_OK)
    return res_WRITE4.status;

  /* NB: After this points, if pstate_found == NULL, then the stateid is all-0 or all-1 */

  if(pstate_found != NULL)
    {
      switch(pstate_found->state_type)
        {
          case STATE_TYPE_SHARE:
            pstate_open = pstate_found;
            // TODO FSF: need to check against existing locks
            break;

          case STATE_TYPE_LOCK:
            pstate_open = pstate_found->state_data.lock.popenstate;
            // TODO FSF: should check that write is in range of an exclusive lock...
            break;

          case STATE_TYPE_DELEG:
#ifdef _USE_NFS4_1
          case STATE_TYPE_LAYOUT:
#endif /* _USE_NFS4_1 */
            pstate_open = NULL;
            // TODO FSF: should check that this is a write delegation?
            break;

          default:
            res_WRITE4.status = NFS4ERR_BAD_STATEID;
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "WRITE with invalid stateid of type %d",
                     (int) pstate_found->state_type);
            return res_WRITE4.status;
        }

      /* This is a write operation, this means that the file MUST have been opened for writing */
      if(pstate_open != NULL &&
         (pstate_open->state_data.share.share_access & OPEN4_SHARE_ACCESS_WRITE) == 0)
        {
          /* Bad open mode, return NFS4ERR_OPENMODE */
          res_WRITE4.status = NFS4ERR_OPENMODE;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "WRITE state %p doesn't have OPEN4_SHARE_ACCESS_WRITE",
                   pstate_found);
          return res_WRITE4.status;
        }
    }
  else
    {
      /* Special stateid, no open state, check to see if any share conflicts */
      pstate_open = NULL;

      pthread_rwlock_rdlock(&pentry->state_lock);
      anonymous = TRUE;

      /*
       * Special stateid, no open state, check to see if any share conflicts
       * The stateid is all-0 or all-1
       */
      res_WRITE4.status = nfs4_check_special_stateid(pentry,"WRITE",
                                                     FATTR4_ATTR_WRITE);
      if(res_WRITE4.status != NFS4_OK)
        {
          pthread_rwlock_unlock(&pentry->state_lock); 
          return res_WRITE4.status;
        }
    }

  if (pstate_open == NULL)
    {
      if(cache_inode_access(pentry,
                            FSAL_WRITE_ACCESS,
                            data->pcontext,
                            &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_WRITE4.status = nfs4_Errno(cache_status);;
          pthread_rwlock_unlock(&pentry->state_lock);
          return res_WRITE4.status;
        }
    }

  /* Get the characteristics of the I/O to be made */
  offset = arg_WRITE4.offset;
  size = arg_WRITE4.data.data_len;
  stable_how = arg_WRITE4.stable;
  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_WRITE: offset = %"PRIu64"  length = %zu  stable = %d",
               offset, size, stable_how);

  if((data->pexport->export_perms.options & EXPORT_OPTION_MAXOFFSETWRITE) ==
     EXPORT_OPTION_MAXOFFSETWRITE)
    if((fsal_off_t) (offset + size) > data->pexport->MaxOffsetWrite)
      {
        res_WRITE4.status = NFS4ERR_DQUOT;
        if (anonymous)
          {
            pthread_rwlock_unlock(&pentry->state_lock);
          }
        return res_WRITE4.status;
      }

  /* The size to be written should not be greater than FATTR4_MAXWRITESIZE because this value is asked
   * by the client at mount time, but we check this by security */

  if( size > data->pexport->MaxWrite )
    {
      /*
       * The client asked for too much data, we
       * must restrict him
       */

      LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_WRITE: write requested size = %"PRIu64" write allowed size = %"PRIu64,
               size, data->pexport->MaxWrite);

      size = data->pexport->MaxWrite;
    }

  /* Where are the data ? */
  bufferdata = arg_WRITE4.data.data_val;

  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_WRITE: offset = %"PRIu64" length = %zu",
               offset, size);

  /* if size == 0 , no I/O) are actually made and everything is alright */
  if(size == 0)
    {
      res_WRITE4.WRITE4res_u.resok4.count = 0;
      res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;

      memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier,
             sizeof(verifier4));

      res_WRITE4.status = NFS4_OK;
      if (anonymous)
        {
          pthread_rwlock_unlock(&pentry->state_lock);
        }
      return res_WRITE4.status;
    }

  if(arg_WRITE4.stable == UNSTABLE4)
    {
      stability = CACHE_INODE_UNSAFE_WRITE_TO_FS_BUFFER;
    }
  else
    {
      stability = CACHE_INODE_SAFE_WRITE_TO_FS;
    }

  if(cache_inode_rdwr(pentry,
                      CACHE_INODE_WRITE,
                      offset,
                      size,
                      &written_size,
                      bufferdata,
                      &eof_met,
                      data->pcontext,
                      stability,
                      &cache_status) != CACHE_INODE_SUCCESS)
    {
      LogDebug(COMPONENT_NFS_V4,
               "cache_inode_rdwr returned %s",
               cache_inode_err_str(cache_status));
      res_WRITE4.status = nfs4_Errno(cache_status);
      if (anonymous)
        {
          pthread_rwlock_unlock(&pentry->state_lock);
        }
      return res_WRITE4.status;
    }

  /* Set the returned value */
  if(stability == CACHE_INODE_SAFE_WRITE_TO_FS)
    res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;
  else
    res_WRITE4.WRITE4res_u.resok4.committed = UNSTABLE4;

  res_WRITE4.WRITE4res_u.resok4.count = written_size;
  memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier, sizeof(verifier4));

  res_WRITE4.status = NFS4_OK;

  if (anonymous)
    {
      pthread_rwlock_unlock(&pentry->state_lock);
    }

  return res_WRITE4.status;
}                               /* nfs4_op_write */

/**
 * nfs4_op_write_Free: frees what was allocared to handle nfs4_op_write.
 *
 * Frees what was allocared to handle nfs4_op_write.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_write_Free(WRITE4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_write_Free */

#ifdef _PNFS_DS

/**
 * op_dswrite: Write for a data server
 *
 * @param op    [IN]    pointer to nfs41_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs41_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */

static int op_dswrite(struct nfs_argop4 *op,
                      compound_data_t * data,
                      struct nfs_resop4 *resp)
{
  /* FSAL file handle */
  fsal_handle_t handle;
  /* NFSv4 return code */
  nfsstat4 nfs_status = 0;
  struct fsal_handle_desc fh_desc;

  /* Construct the FSAL file handle */

  if ((nfs4_FhandleToFSAL(&data->currentFH,
                          &fh_desc,
                          data->pcontext)) == 0)
    {
      res_WRITE4.status = NFS4ERR_INVAL;
      return res_WRITE4.status;
    }

  memset((caddr_t) &handle, 0, sizeof(handle));
  memcpy((caddr_t) &handle, fh_desc.start, fh_desc.len);
  nfs_status
    = fsal_dsfunctions.DS_write(&handle,
                                data->pcontext,
                                &arg_WRITE4.stateid,
                                arg_WRITE4.offset,
                                arg_WRITE4.data.data_len,
                                arg_WRITE4.data.data_val,
                                arg_WRITE4.stable,
                                &res_WRITE4.WRITE4res_u.resok4.count,
                                &res_WRITE4.WRITE4res_u.resok4.writeverf,
                                &res_WRITE4.WRITE4res_u.resok4.committed);

  res_WRITE4.status = nfs_status;
  return res_WRITE4.status;
}
#endif /* _PNFS_DS */
