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
 * @file nfs4_op_write.c
 * @brief Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 */

#include "config.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "fsal_pnfs.h"
#include "server_stats.h"

static int op_dswrite(struct nfs_argop4 *op,
                      compound_data_t *data,
                      struct nfs_resop4 *resp);

/**
 * @brief The NFS4_OP_WRITE operation
 *
 * This functions handles the NFS4_OP_WRITE operation in NFSv4. This
 * function can be called only from nfs4_Compound.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661, p. 376
 */

int nfs4_op_write(struct nfs_argop4 *op,
                  compound_data_t *data,
                  struct nfs_resop4 *resp)
{
  WRITE4args *const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
  WRITE4res *const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
  uint32_t                 size, check_size;
  size_t                   written_size;
  uint64_t                 offset;
  bool                     eof_met;
  bool                     sync = false;
  void                   * bufferdata;
  stable_how4              stable_how;
  state_t                * state_found = NULL;
  state_t                * state_open = NULL;
  cache_inode_status_t     cache_status = CACHE_INODE_SUCCESS;
  cache_entry_t          * entry = NULL;
  fsal_status_t            fsal_status;
  /* This flag is set to true in the case of an anonymous read so that
     we know to release the state lock afterward.  The state lock does
     not need to be held during a non-anonymous read, since the open
     state itself prevents a conflict. */
  bool                     anonymous = false;
  struct gsh_buffdesc       verf_desc;

  /* Lock are not supported */
  resp->resop = NFS4_OP_WRITE;
  res_WRITE4->status = NFS4_OK;

  /*
   * Do basic checks on a filehandle
   * Only files can be written
   */
  res_WRITE4->status = nfs4_sanity_check_FH(data, REGULAR_FILE, true);
  if(res_WRITE4->status != NFS4_OK)
    return res_WRITE4->status;

    /* if quota support is active, then we should check is the FSAL
       allows inode creation or not */
  fsal_status = data->pexport->export_hdl->ops->check_quota(
          data->pexport->export_hdl,
          data->pexport->fullpath,
          FSAL_QUOTA_INODES,
          data->req_ctx);
  if (FSAL_IS_ERROR( fsal_status))
    {
      res_WRITE4->status = NFS4ERR_DQUOT ;
      return res_WRITE4->status;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_write_xattr(op, data, resp);

  if((data->minorversion == 1) &&
     (nfs4_Is_Fh_DSHandle(&data->currentFH)))
    {
      return(op_dswrite(op, data, resp));
    }

  /* Manage access type */
  switch( data->pexport->access_type )
   {
     case ACCESSTYPE_MDONLY:
     case ACCESSTYPE_MDONLY_RO:
        res_WRITE4->status = NFS4ERR_DQUOT;
        return res_WRITE4->status;
        break ;

     case ACCESSTYPE_RO:
        res_WRITE4->status = NFS4ERR_ROFS ;
        return res_WRITE4->status;
        break ;

     default:
        break ;
   } /* switch( data->pexport->access_type ) */

  /* vnode to manage is the current one */
  entry = data->current_entry;

  /* Check stateid correctness and get pointer to state
   * (also checks for special stateids)
   */
  res_WRITE4->status = nfs4_Check_Stateid(&arg_WRITE4->stateid,
					  entry,
					  &state_found,
					  data,
					  STATEID_SPECIAL_ANY,
					  0,
					  FALSE,
					  "WRITE");
  if(res_WRITE4->status != NFS4_OK)
    return res_WRITE4->status;

  /* NB: After this points, if state_found == NULL, then the stateid is all-0 or all-1 */

  if(state_found != NULL)
    {
      switch(state_found->state_type)
        {
          case STATE_TYPE_SHARE:
            state_open = state_found;
            // TODO FSF: need to check against existing locks
            break;

          case STATE_TYPE_LOCK:
            state_open = state_found->state_data.lock.openstate;
            /**
             * @todo FSF: should check that write is in range of an
             * exclusive lock... */
            break;

          case STATE_TYPE_DELEG:
            /**
             * TODO FSF: should check that this is a write delegation?
             */
          case STATE_TYPE_LAYOUT:
            state_open = NULL;
            break;

          default:
            res_WRITE4->status = NFS4ERR_BAD_STATEID;
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "WRITE with invalid stateid of type %d",
                     (int) state_found->state_type);
            return res_WRITE4->status;
        }

      /* This is a write operation, this means that the file MUST have
         been opened for writing */
      if(state_open != NULL &&
         (state_open->state_data.share.share_access &
          OPEN4_SHARE_ACCESS_WRITE) == 0)
        {
          /* Bad open mode, return NFS4ERR_OPENMODE */
          res_WRITE4->status = NFS4ERR_OPENMODE;
          LogDebug(COMPONENT_NFS_V4_LOCK,
                   "WRITE state %p doesn't have OPEN4_SHARE_ACCESS_WRITE",
                   state_found);
          return res_WRITE4->status;
        }
    }
  else
    {
      /* Special stateid, no open state, check to see if any share conflicts */
      state_open = NULL;

      PTHREAD_RWLOCK_rdlock(&entry->state_lock);
      anonymous = true;

      /*
       * Special stateid, no open state, check to see if any share conflicts
       * The stateid is all-0 or all-1
       */
      res_WRITE4->status = nfs4_check_special_stateid(entry,"WRITE",
						      FATTR4_ATTR_WRITE);
      if(res_WRITE4->status != NFS4_OK)
        {
          PTHREAD_RWLOCK_unlock(&entry->state_lock);
          return res_WRITE4->status;
        }
    }

  if (state_open == NULL)
    {
      cache_status = cache_inode_access(entry,
					FSAL_WRITE_ACCESS,
					data->req_ctx);
      if (cache_status != CACHE_INODE_SUCCESS)
        {
          res_WRITE4->status = nfs4_Errno(cache_status);
          if (anonymous)
            PTHREAD_RWLOCK_unlock(&entry->state_lock);
          return res_WRITE4->status;
        }
    }

  /* Get the characteristics of the I/O to be made */
  offset = arg_WRITE4->offset;
  size = arg_WRITE4->data.data_len;
  stable_how = arg_WRITE4->stable;
  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_WRITE: offset = %"PRIu64"  length = %"PRIu32
               "  stable = %d",
               offset, size, stable_how);

  if((data->pexport->export_perms.options & EXPORT_OPTION_MAXOFFSETWRITE) ==
     EXPORT_OPTION_MAXOFFSETWRITE)
    if((offset + size) > data->pexport->MaxOffsetWrite)
      {
        res_WRITE4->status = NFS4ERR_DQUOT;
        if (anonymous)
          {
            PTHREAD_RWLOCK_unlock(&entry->state_lock);
          }
        return res_WRITE4->status;
      }

  /* The size to be written should not be greater than
   * FATTR4_MAXWRITESIZE because this value is asked by the client at
   * mount time, but we check this by security */

  /* We should check against the value we returned in getattr. This was not
   * the case before the following check_size code was added.
   */
  if( ((data->pexport->export_perms.options & EXPORT_OPTION_MAXWRITE) == EXPORT_OPTION_MAXWRITE)) 
    check_size = data->pexport->MaxWrite;
  else
    check_size = entry->obj_handle->export->ops->fs_maxwrite(entry->obj_handle->export);
  if( size > check_size )
    {
      /*
       * The client asked for too much data, we
       * must restrict him
       */

      LogFullDebug(COMPONENT_NFS_V4,
                   "NFS4_OP_WRITE: write requested size = %"PRIu32
                   " write allowed size = %"PRIu32,
                   size, check_size);

      size = check_size;
    }

  /* Where are the data ? */
  bufferdata = arg_WRITE4->data.data_val;

  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_WRITE: offset = %"PRIu64" length = %"PRIu32,
               offset, size);

  /* if size == 0 , no I/O) are actually made and everything is alright */
  if(size == 0)
    {
      res_WRITE4->WRITE4res_u.resok4.count = 0;
      res_WRITE4->WRITE4res_u.resok4.committed = FILE_SYNC4;

      verf_desc.addr = res_WRITE4->WRITE4res_u.resok4.writeverf;
      verf_desc.len = sizeof(verifier4);
      data->pexport->export_hdl->ops->get_write_verifier(&verf_desc);

      res_WRITE4->status = NFS4_OK;
      if (anonymous)
        {
          PTHREAD_RWLOCK_unlock(&entry->state_lock);
        }
      return res_WRITE4->status;
    }

  if(arg_WRITE4->stable == UNSTABLE4)
    {
      sync = false;
    }
  else
    {
      sync = true;
    }

  if (!anonymous &&
      data->minorversion == 0) {
          data->req_ctx->clientid
                  = &state_found->state_owner->so_owner.
                  so_nfs4_owner.so_clientid;
  }

  cache_status = cache_inode_rdwr(entry,
				  CACHE_INODE_WRITE,
				  offset,
				  size,
				  &written_size,
				  bufferdata,
				  &eof_met,
				  data->req_ctx,
				  &sync);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      LogDebug(COMPONENT_NFS_V4,
               "cache_inode_rdwr returned %s",
               cache_inode_err_str(cache_status));
      res_WRITE4->status = nfs4_Errno(cache_status);
      if (anonymous)
        {
          PTHREAD_RWLOCK_unlock(&entry->state_lock);
        }
      return res_WRITE4->status;
    }

  if (!anonymous &&
      data->minorversion == 0) {
          data->req_ctx->clientid = NULL;
  }

  /* Set the returned value */
  if(sync)
    res_WRITE4->WRITE4res_u.resok4.committed = FILE_SYNC4;
  else
    res_WRITE4->WRITE4res_u.resok4.committed = UNSTABLE4;

  res_WRITE4->WRITE4res_u.resok4.count = written_size;

  verf_desc.addr = res_WRITE4->WRITE4res_u.resok4.writeverf;
  verf_desc.len = sizeof(verifier4);
  data->pexport->export_hdl->ops->get_write_verifier(&verf_desc);

  res_WRITE4->status = NFS4_OK;

  if (anonymous)
    {
      PTHREAD_RWLOCK_unlock(&entry->state_lock);
    }
#ifdef USE_DBUS_STATS
  server_stats_io_done(data->req_ctx,
		       size,
		       written_size,
		       (res_WRITE4->status == NFS4_OK) ? true : false,
		       true);
#endif

  return res_WRITE4->status;
}                               /* nfs4_op_write */

/**
 * @brief Free memory allocated for WRITE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_WRITE operation.
 *
 * @param[in,out] resp nfs4_op results
*
 */
void nfs4_op_write_Free(nfs_resop4 *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_write_Free */

/**
 * @brief Write for a data server
 *
 * This function bypasses cache_inode and calls directly into the FSAL
 * to perform a pNFS data server write.
 *
 * @param[in]     op    Arguments for nfs41_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs41_op
 *
 * @return per RFC5661, p. 376
 *
 */

static int
op_dswrite(struct nfs_argop4 *op,
           compound_data_t *data,
           struct nfs_resop4 *resp)
{
	WRITE4args *const arg_WRITE4 = &op->nfs_argop4_u.opwrite;
	WRITE4res *const res_WRITE4 = &resp->nfs_resop4_u.opwrite;
        /* NFSv4 return code */
        nfsstat4 nfs_status = 0;

        nfs_status = data->current_ds->ops->write(
                data->current_ds,
                data->req_ctx,
                &arg_WRITE4->stateid,
                arg_WRITE4->offset,
                arg_WRITE4->data.data_len,
                arg_WRITE4->data.data_val,
                arg_WRITE4->stable,
                &res_WRITE4->WRITE4res_u.resok4.count,
                &res_WRITE4->WRITE4res_u.resok4.writeverf,
                &res_WRITE4->WRITE4res_u.resok4.committed);

        res_WRITE4->status = nfs_status;
        return res_WRITE4->status;
}

