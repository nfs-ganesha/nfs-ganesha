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
 * \file    nfs4_op_read.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.14 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_read.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "cache_content_policy.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

/**
 * nfs4_op_read: The NFS4_OP_READ operation
 *
 * This functions handles the NFS4_OP_READ operation in NFSv4. This function can be called only from nfs4_Compound.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.
 *
 */

#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

int nfs4_op_read(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_read";

  fsal_seek_t              seek_descriptor;
  fsal_size_t              size;
  fsal_size_t              read_size;
  fsal_off_t               offset;
  fsal_boolean_t           eof_met;
  caddr_t                  bufferdata;
  cache_inode_status_t     cache_status;
  state_t                * pstate_found = NULL;
  state_t                * pstate_open;
  state_t                * pstate_iterate;
  cache_content_status_t   content_status;
  fsal_attrib_list_t       attr;
  cache_entry_t          * pentry = NULL;
  int                      rc = 0;
  struct glist_head      * glist;

  cache_content_policy_data_t datapol;

  datapol.UseMaxCacheSize = FALSE;

  /* Say we are managing NFS4_OP_READ */
  resp->resop = NFS4_OP_READ;
  res_READ4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_READ4.status = NFS4ERR_NOFILEHANDLE;
      return res_READ4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_READ4.status = NFS4ERR_BADHANDLE;
      return res_READ4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_READ4.status = NFS4ERR_FHEXPIRED;
      return res_READ4.status;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_read_xattr(op, data, resp);

  /* Manage access type MDONLY */
  if(data->pexport->access_type == ACCESSTYPE_MDONLY)
    {
      res_READ4.status = NFS4ERR_DQUOT;
      return res_READ4.status;
    }

  /* Only files can be read */
  if(data->current_filetype != REGULAR_FILE)
    {
      /* If the source is no file, return EISDIR if it is a directory and EINAVL otherwise */
      if(data->current_filetype == DIRECTORY)
        res_READ4.status = NFS4ERR_ISDIR;
      else
        res_READ4.status = NFS4ERR_INVAL;

      return res_READ4.status;
    }

  /* vnode to manage is the current one */
  pentry = data->current_entry;

  /* Check stateid correctness and get pointer to state
   * (also checks for special stateids)
   */
  if((rc = nfs4_Check_Stateid(&arg_READ4.stateid,
                              pentry,
                              0LL,
                              &pstate_found,
                              data,
                              STATEID_SPECIAL_ANY,
                              "READ")) != NFS4_OK)
    {
      res_READ4.status = rc;
      return res_READ4.status;
    }

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
            // TODO FSF: should check that write is in range of an byte range lock...
            break;

          case STATE_TYPE_DELEG:
            pstate_open = NULL;
            // TODO FSF: should check that this is a read delegation?
            break;

          default:
            res_READ4.status = NFS4ERR_BAD_STATEID;
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "READ with invalid statid of type %d",
                     (int) pstate_found->state_type);
            return res_READ4.status;
        }

      /* This is a read operation, this means that the file MUST have been opened for reading */
      if(pstate_open != NULL &&
         (pstate_open->state_data.share.share_access & OPEN4_SHARE_ACCESS_READ) == 0)
        {
         /* Even if file is open for write, the client may do accidently read operation (caching).
          * Because of this, READ is allowed if not explicitely denied.
          * See page 72 in RFC3530 for more details */
 
          if( pstate_open->state_data.share.share_deny & OPEN4_SHARE_DENY_READ )
           {
             /* Bad open mode, return NFS4ERR_OPENMODE */
             res_READ4.status = NFS4ERR_OPENMODE;
             LogDebug(COMPONENT_NFS_V4_LOCK,
                      "READ state %p doesn't have OPEN4_SHARE_ACCESS_READ",
                       pstate_found);
             return res_READ4.status;
           }
        }

      /* If NFSv4::Use_OPEN_CONFIRM is set to TRUE in the configuration file, check is state is confirmed */
      if(nfs_param.nfsv4_param.use_open_confirm == TRUE)
        {
          if(pstate_found->state_powner->so_owner.so_nfs4_owner.so_confirmed == FALSE)
            {
              res_READ4.status = NFS4ERR_BAD_STATEID;
              return res_READ4.status;
            }
        }
    }
  else
    {
      /* Special stateid, no open state, check to see if any share conflicts */
      pstate_open = NULL;

      /* Acquire lock to enter critical section on this entry */
      P_r(&pentry->lock);

      /* Iterate through file's state to look for conflicts */
      glist_for_each(glist, &pentry->object.file.state_list)
        {
          pstate_iterate = glist_entry(glist, state_t, state_list);

          switch(pstate_iterate->state_type)
            {
              case STATE_TYPE_SHARE:
                if(pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_READ)
                  {
                    /* Reading from this file is prohibited, file is read-denied */
                    V_r(&pentry->lock);
                    res_READ4.status = NFS4ERR_LOCKED;
                    LogDebug(COMPONENT_NFS_V4_LOCK,
                             "READ is denied by state %p",
                             pstate_iterate);
                    return res_READ4.status;
                  }
                break;

              case STATE_TYPE_LOCK:
                /* Skip, will check for conflicting locks later */
                break;

              case STATE_TYPE_DELEG:
                // TODO FSF: should check for conflicting delegations, may need to recall
                break;

              case STATE_TYPE_LAYOUT:
                // TODO FSF: should check for conflicting layouts, may need to recall
                // Need to look at this even for NFS v4 READ since there may be NFS v4.1 users of the file
                break;

              case STATE_TYPE_NONE:
                break;
            }
        }
      // TODO FSF: need to check against existing locks
      V_r(&pentry->lock);
    }

  if (pstate_open == NULL)
    {
      if(cache_inode_access(pentry,
                            FSAL_READ_ACCESS,
                            data->ht,
                            data->pclient,
                            data->pcontext,
                            &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_READ4.status = nfs4_Errno(cache_status);
          return res_READ4.status;
        }
    }
  /* Get the size and offset of the read operation */
  offset = arg_READ4.offset;
  size = arg_READ4.count;

  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_READ: offset = %llu  length = %llu",
               (unsigned long long)offset, size);

  if((data->pexport->options & EXPORT_OPTION_MAXOFFSETREAD) ==
     EXPORT_OPTION_MAXOFFSETREAD)
    if((fsal_off_t) (offset + size) > data->pexport->MaxOffsetRead)
      {
        res_READ4.status = NFS4ERR_DQUOT;
        return res_READ4.status;
      }

  /* Do not read more than FATTR4_MAXREAD */
  if((data->pexport->options & EXPORT_OPTION_MAXREAD) == EXPORT_OPTION_MAXREAD &&
     size > data->pexport->MaxRead)
    {
      /* the client asked for too much data,
       * this should normally not happen because
       * client will get FATTR4_MAXREAD value at mount time */
      size = data->pexport->MaxRead;
    }

  /* If size == 0 , no I/O is to be made and everything is alright */
  if(size == 0)
    {
      res_READ4.READ4res_u.resok4.eof = FALSE;  /* end of file was not reached because READ occured, and a size = 0 can not lead to eof */
      res_READ4.READ4res_u.resok4.data.data_len = 0;
      res_READ4.READ4res_u.resok4.data.data_val = NULL;

      res_READ4.status = NFS4_OK;
      return res_READ4.status;
    }

  if((data->pexport->options & EXPORT_OPTION_USE_DATACACHE) &&
     (pentry->object.file.pentry_content == NULL))
    {
      /* Entry is not in datacache, but should be in, cache it .
       * Several threads may call this function at the first time and a race condition can occur here
       * in order to avoid this, cache_inode_add_data_cache is "mutex protected"
       * The first call will create the file content cache entry, the further will return
       * with error CACHE_INODE_CACHE_CONTENT_EXISTS which is not a pathological thing here */

      datapol.UseMaxCacheSize = data->pexport->options & EXPORT_OPTION_MAXCACHESIZE;
      datapol.MaxCacheSize = data->pexport->MaxCacheSize;

      /* Status is set in last argument */
      cache_inode_add_data_cache(pentry, data->ht, data->pclient, data->pcontext,
                                 &cache_status);

      if((cache_status != CACHE_INODE_SUCCESS) &&
         (cache_content_cache_behaviour(pentry,
                                        &datapol,
                                        (cache_content_client_t *) (data->pclient->
                                                                    pcontent_client),
                                        &content_status) == CACHE_CONTENT_FULLY_CACHED)
         && (cache_status != CACHE_INODE_CACHE_CONTENT_EXISTS))
        {
          res_READ4.status = NFS4ERR_SERVERFAULT;
          return res_READ4.status;
        }

    }

  /* Some work is to be done */
  if((bufferdata = (char *)Mem_Alloc(size)) == NULL)
    {
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }
  memset((char *)bufferdata, 0, size);

  seek_descriptor.whence = FSAL_SEEK_SET;
  seek_descriptor.offset = offset;

  if(cache_inode_rdwr(pentry,
                      CACHE_INODE_READ,
                      &seek_descriptor,
                      size,
                      &read_size,
                      &attr,
                      bufferdata,
                      &eof_met,
                      data->ht,
                      data->pclient,
                      data->pcontext, TRUE, &cache_status) != CACHE_INODE_SUCCESS)
    {
      res_READ4.status = nfs4_Errno(cache_status);
      return res_READ4.status;
    }

  res_READ4.READ4res_u.resok4.data.data_len = read_size;
  res_READ4.READ4res_u.resok4.data.data_val = bufferdata;

  LogFullDebug(COMPONENT_NFS_V4,
               "NFS4_OP_READ: offset = %llu  read length = %llu eof=%u",
               (unsigned long long)offset, read_size, eof_met);

  /* Is EOF met or not ? */
  if( ( eof_met == TRUE ) || 
      ( (offset + read_size) >= attr.filesize) )
    res_READ4.READ4res_u.resok4.eof = TRUE;
  else
    res_READ4.READ4res_u.resok4.eof = FALSE;

  /* Say it is ok */
  res_READ4.status = NFS4_OK;

  return res_READ4.status;
}                               /* nfs4_op_read */

/**
 * nfs4_op_read_Free: frees what was allocared to handle nfs4_op_read.
 *
 * Frees what was allocared to handle nfs4_op_read.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_read_Free(READ4res * resp)
{
  if(resp->status == NFS4_OK)
    if(resp->READ4res_u.resok4.data.data_len != 0)
      Mem_Free(resp->READ4res_u.resok4.data.data_val);
  return;
}                               /* nfs4_op_read_Free */
