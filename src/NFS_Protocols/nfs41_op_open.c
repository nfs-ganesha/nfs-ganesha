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
 * \file    nfs41_op_open.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.18 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs41_op_open.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

extern nfs_parameter_t nfs_param;

/**
 * nfs41_op_open: NFS4_OP_OPEN, opens and eventually creates a regular file.
 * 
 * NFS4_OP_OPEN, opens and eventually creates a regular file.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

extern time_t ServerBootTime;

#define arg_OPEN4 op->nfs_argop4_u.opopen
#define res_OPEN4 resp->nfs_resop4_u.opopen

int nfs41_op_open(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t *pentry_parent = NULL;
  cache_entry_t *pentry_lookup = NULL;
  cache_entry_t *pentry_newfile = NULL;
  fsal_handle_t *pnewfsal_handle = NULL;
  fsal_attrib_list_t attr_parent;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_newfile;
  fsal_attrib_list_t sattr;
  fsal_openflags_t openflags = 0;
  cache_inode_status_t cache_status;
  nfsstat4 rc;
  int retval;
  fsal_name_t filename;
  bool_t AttrProvided = FALSE;
  fsal_accessmode_t mode = 0600;
  nfs_fh4 newfh4;
  nfs_client_id_t nfs_clientid;
  nfs_worker_data_t *pworker = NULL;
  int convrc = 0;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_open";

  cache_inode_state_data_t candidate_data;
  cache_inode_state_type_t candidate_type;
  cache_inode_state_t *pfile_state = NULL;
  cache_inode_state_t *pstate_found_iterate = NULL;
  cache_inode_state_t *pstate_previous_iterate = NULL;
  cache_inode_state_t *pstate_found_same_owner = NULL;

  cache_inode_open_owner_name_t owner_name;
  cache_inode_open_owner_name_t *powner_name = NULL;
  cache_inode_open_owner_t *powner = NULL;
  bool_t open_owner_known = FALSE;

  resp->resop = NFS4_OP_OPEN;
  res_OPEN4.status = NFS4_OK;

  uint32_t tmp_attr[2];
  uint_t tmp_int = 2;

  int pnfs_status;
  cache_inode_create_arg_t create_arg;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_NOFILEHANDLE;
      return res_OPEN4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_BADHANDLE;
      return res_OPEN4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_FHEXPIRED;
      return res_OPEN4.status;
    }

  /* This can't be done on the pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_ROFS;
      return res_OPEN4.status;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_open_xattr(op, data, resp);

  /* If data->current_entry is empty, repopulate it */
  if(data->current_entry == NULL)
    {
      if((data->current_entry = nfs_FhandleToCache(NFS_V4,
                                                   NULL,
                                                   NULL,
                                                   &(data->currentFH),
                                                   NULL,
                                                   NULL,
                                                   &(res_OPEN4.status),
                                                   &attr,
                                                   data->pcontext,
                                                   data->pclient,
                                                   data->ht, &retval)) == NULL)
        {
          res_OPEN4.status = NFS4ERR_SERVERFAULT;
          return res_OPEN4.status;
        }
    }

  /* Set parent */
  pentry_parent = data->current_entry;

  /* First switch is based upon claim type */
  switch (arg_OPEN4.claim.claim)
    {
    case CLAIM_DELEGATE_CUR:
    case CLAIM_DELEGATE_PREV:
      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          return res_OPEN4.status;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          return res_OPEN4.status;
        }

      res_OPEN4.status = NFS4ERR_NOTSUPP;
      return res_OPEN4.status;
      break;

    case CLAIM_NULL:
      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          return res_OPEN4.status;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          return res_OPEN4.status;
        }

      /* Check if asked attributes are correct */
      if(arg_OPEN4.openhow.openflag4_u.how.mode == GUARDED4 ||
         arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4)
        {
          if(!nfs4_Fattr_Supported
             (&arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs))
            {
              res_OPEN4.status = NFS4ERR_ATTRNOTSUPP;
              return res_OPEN4.status;
            }

          /* Do not use READ attr, use WRITE attr */
          if(!nfs4_Fattr_Check_Access
             (&arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs,
              FATTR4_ATTR_WRITE))
            {
              res_OPEN4.status = NFS4ERR_INVAL;
              return res_OPEN4.status;
            }
        }

      /* Check if filename is correct */
      if((cache_status =
          cache_inode_error_convert(FSAL_buffdesc2name
                                    ((fsal_buffdesc_t *) & arg_OPEN4.claim.open_claim4_u.
                                     file, &filename))) != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          return res_OPEN4.status;
        }

      /* Check parent */
      pentry_parent = data->current_entry;

      /* Parent must be a directory */
      if((pentry_parent->internal_md.type != DIR_BEGINNING) &&
         (pentry_parent->internal_md.type != DIR_CONTINUE))
        {
          /* Parent object is not a directory... */
          if(pentry_parent->internal_md.type == SYMBOLIC_LINK)
            res_OPEN4.status = NFS4ERR_SYMLINK;
          else
            res_OPEN4.status = NFS4ERR_NOTDIR;

          return res_OPEN4.status;
        }

      /* What kind of open is it ? */

      LogFullDebug(COMPONENT_NFS_V4,
           "     OPEN: Claim type = %d   Open Type = %d  Share Deny = %d   Share Access = %d \n",
           arg_OPEN4.claim.claim, arg_OPEN4.openhow.opentype, arg_OPEN4.share_deny,
           arg_OPEN4.share_access);


      /* It this a known client id ? */
      LogDebug(COMPONENT_NFS_V4, "OPEN Client id = %llx", arg_OPEN4.owner.clientid);

      /* Is this open_owner known ? */
      if(!nfs_convert_open_owner(&arg_OPEN4.owner, &owner_name))
        {
          res_OPEN4.status = NFS4ERR_SERVERFAULT;
          return res_OPEN4.status;
        }

      if(!nfs_open_owner_Get_Pointer(&owner_name, &powner))
        {
          /* This open owner is not known yet, allocated and set up a new one */
          GET_PREALLOC(powner,
                       data->pclient->pool_open_owner,
                       data->pclient->nb_pre_state_v4, cache_inode_open_owner_t, next);

          GET_PREALLOC(powner_name,
                       data->pclient->pool_open_owner_name,
                       data->pclient->nb_pre_state_v4,
                       cache_inode_open_owner_name_t, next);

          if(powner == NULL || powner_name == NULL)
            {
              res_OPEN4.status = NFS4ERR_SERVERFAULT;
              return res_OPEN4.status;
            }

          memcpy((char *)powner_name, (char *)&owner_name,
                 sizeof(cache_inode_open_owner_name_t));

          /* set up the content of the open_owner */
          powner->confirmed = FALSE;
          powner->seqid = 1;    /* NFSv4.1 specific, initial seqid is 1 */
          powner->related_owner = NULL;
          powner->next = NULL;
          powner->clientid = arg_OPEN4.owner.clientid;
          powner->owner_len = arg_OPEN4.owner.owner.owner_len;
          memcpy((char *)powner->owner_val, (char *)arg_OPEN4.owner.owner.owner_val,
                 arg_OPEN4.owner.owner.owner_len);
          powner->owner_val[powner->owner_len] = '\0';

          pthread_mutex_init(&powner->lock, NULL);

          if(!nfs_open_owner_Set(powner_name, powner))
            {
              res_OPEN4.status = NFS4ERR_SERVERFAULT;
              return res_OPEN4.status;
            }

        }

      /* Status of parent directory before the operation */
      if((cache_status = cache_inode_getattr(pentry_parent,
                                             &attr_parent,
                                             data->ht,
                                             data->pclient,
                                             data->pcontext,
                                             &cache_status)) != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          return res_OPEN4.status;
        }
      memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.before), 0, sizeof(changeid4));
      res_OPEN4.OPEN4res_u.resok4.cinfo.before =
          (changeid4) pentry_parent->internal_md.mod_time;

      /* CLient may have provided fattr4 to set attributes at creation time */
      if(arg_OPEN4.openhow.openflag4_u.how.mode == GUARDED4 ||
         arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4)
        {
          if(arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs.attrmask.
             bitmap4_len != 0)
            {
              /* Convert fattr4 so nfs4_sattr */
              convrc =
                  nfs4_Fattr_To_FSAL_attr(&sattr,
                                          &(arg_OPEN4.openhow.openflag4_u.how.
                                            createhow4_u.createattrs));

              if(convrc == 0)
                {
                  res_OPEN4.status = NFS4ERR_ATTRNOTSUPP;
                  return res_OPEN4.status;
                }

              if(convrc == -1)
                {
                  res_OPEN4.status = NFS4ERR_BADXDR;
                  return res_OPEN4.status;
                }

              AttrProvided = TRUE;
            }

        }

      /* Second switch is based upon "openhow" */
      switch (arg_OPEN4.openhow.opentype)
        {
        case OPEN4_CREATE:
          /* a new file is to be created */

          /* Does a file with this name already exist ? */
          pentry_lookup = cache_inode_lookup(pentry_parent,
                                             &filename,
                                             &attr_newfile,
                                             data->ht,
                                             data->pclient,
                                             data->pcontext, &cache_status);

          if(cache_status != CACHE_INODE_NOT_FOUND)
            {
              /* if open is UNCHECKED, return NFS4_OK (RFC3530 page 172) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4
                 && (cache_status == CACHE_INODE_SUCCESS))
                {
                  /* If the file is opened for write, OPEN4 while deny share write access,
                   * in this case, check caller has write access to the file */
                  if(arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE)
                    {
                      if(cache_inode_access(pentry_lookup,
                                            FSAL_W_OK,
                                            data->ht,
                                            data->pclient,
                                            data->pcontext,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          return res_OPEN4.status;
                        }
                      openflags = FSAL_O_WRONLY;
                    }

                  /* Same check on read: check for readability of a file before opening it for read */
                  if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ)
                    {
                      if(cache_inode_access(pentry_lookup,
                                            FSAL_R_OK,
                                            data->ht,
                                            data->pclient,
                                            data->pcontext,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          return res_OPEN4.status;
                        }
                      openflags = FSAL_O_RDONLY;
                    }

                  if(AttrProvided == TRUE)      /* Set the attribute if provided */
                    {
                      if((cache_status = cache_inode_setattr(pentry_lookup,
                                                             &sattr,
                                                             data->ht,
                                                             data->pclient,
                                                             data->pcontext,
                                                             &cache_status)) !=
                         CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = nfs4_Errno(cache_status);
                          return res_OPEN4.status;
                        }

                      res_OPEN4.OPEN4res_u.resok4.attrset =
                          arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs.
                          attrmask;
                    }
                  else
                    res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0;

                  /* Same check on write */
                  if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE)
                    {
                      if(cache_inode_access(pentry_lookup,
                                            FSAL_W_OK,
                                            data->ht,
                                            data->pclient,
                                            data->pcontext,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          return res_OPEN4.status;
                        }
                      openflags = FSAL_O_RDWR;
                    }

                  /* Set the state for the related file */

                  /* Prepare state management structure */
                  candidate_type = CACHE_INODE_STATE_SHARE;
                  candidate_data.share.share_deny = arg_OPEN4.share_deny;
                  candidate_data.share.share_access = arg_OPEN4.share_access;

                  if(cache_inode_add_state(pentry_lookup,
                                           candidate_type,
                                           &candidate_data,
                                           powner,
                                           data->pclient,
                                           data->pcontext,
                                           &pfile_state,
                                           &cache_status) != CACHE_INODE_SUCCESS)
                    {
                      /* Seqid has to be incremented even in this case */
                      P(powner->lock);
                      powner->seqid += 1;
                      V(powner->lock);

                      res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                      return res_OPEN4.status;
                    }

                  /* Open the file */
                  if(cache_inode_open_by_name(pentry_parent,
                                              &filename,
                                              pentry_lookup,
                                              data->pclient,
                                              openflags,
                                              data->pcontext,
                                              &cache_status) != CACHE_INODE_SUCCESS)
                    {
                      /* Seqid has to be incremented even in this case */
                      P(powner->lock);
                      powner->seqid += 1;
                      V(powner->lock);

                      res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                      res_OPEN4.status = NFS4ERR_ACCESS;
                      return res_OPEN4.status;
                    }

                  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 2;
                  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
                      (uint32_t *) Mem_Alloc(res_OPEN4.OPEN4res_u.resok4.attrset.
                                             bitmap4_len * sizeof(uint32_t))) == NULL)
                    {
                      res_OPEN4.status = NFS4ERR_SERVERFAULT;
                      return res_OPEN4.status;
                    }

                  memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.after), 0,
                         sizeof(changeid4));
                  res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                      (changeid4) pentry_parent->internal_md.mod_time;
                  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = TRUE;

                  res_OPEN4.OPEN4res_u.resok4.stateid.seqid = pfile_state->seqid;
                  memcpy(res_OPEN4.OPEN4res_u.resok4.stateid.other,
                         pfile_state->stateid_other, 12);

                  /* No delegation */
                  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                      OPEN_DELEGATE_NONE;
                  res_OPEN4.OPEN4res_u.resok4.rflags = OPEN4_RESULT_LOCKTYPE_POSIX;

                  /* Now produce the filehandle to this file */
                  if((pnewfsal_handle =
                      cache_inode_get_fsal_handle(pentry_lookup, &cache_status)) == NULL)
                    {
                      res_OPEN4.status = nfs4_Errno(cache_status);
                      return res_OPEN4.status;
                    }

                  /* Allocation of a new file handle */
                  if((rc = nfs4_AllocateFH(&newfh4)) != NFS4_OK)
                    {
                      res_OPEN4.status = rc;
                      return res_OPEN4.status;
                    }

                  /* Building a new fh */
                  if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data))
                    {
                      res_OPEN4.status = NFS4ERR_SERVERFAULT;
                      return res_OPEN4.status;
                    }

                  /* This new fh replaces the current FH */
                  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
                  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
                         newfh4.nfs_fh4_len);

                  data->current_entry = pentry_lookup;
                  data->current_filetype = REGULAR_FILE;

                  res_OPEN4.status = NFS4_OK;
                  return res_OPEN4.status;
                }

              /* if open is EXCLUSIVE, but verifier is the same, return NFS4_OK (RFC3530 page 173) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
                {
                  if((pentry_lookup != NULL)
                     && (pentry_lookup->internal_md.type == REGULAR_FILE))
                    {
                      pstate_found_iterate = NULL;
                      pstate_previous_iterate = NULL;

                      do
                        {
                          cache_inode_state_iterate(pentry_lookup,
                                                    &pstate_found_iterate,
                                                    pstate_previous_iterate,
                                                    data->pclient,
                                                    data->pcontext, &cache_status);
                          if(cache_status == CACHE_INODE_STATE_ERROR)
                            break;

                          if(cache_status == CACHE_INODE_INVALID_ARGUMENT)
                            {
                              /* Seqid has to be incremented even in this case */
                              P(powner->lock);
                              powner->seqid += 1;
                              V(powner->lock);

                              res_OPEN4.status = NFS4ERR_INVAL;
                              return res_OPEN4.status;
                            }

                          /* Check is open_owner is the same */
                          if(pstate_found_iterate != NULL)
                            {
                              if((pstate_found_iterate->state_type ==
                                  CACHE_INODE_STATE_SHARE)
                                 && !memcmp(arg_OPEN4.owner.owner.owner_val,
                                            pstate_found_iterate->powner->owner_val,
                                            pstate_found_iterate->powner->owner_len)
                                 && !memcmp(pstate_found_iterate->state_data.share.
                                            oexcl_verifier,
                                            arg_OPEN4.openhow.openflag4_u.how.
                                            createhow4_u.createverf, NFS4_VERIFIER_SIZE))
                                {

                                  /* A former open EXCLUSIVE with same owner and verifier was found, resend it */
                                  res_OPEN4.OPEN4res_u.resok4.stateid.seqid =
                                      pstate_found_iterate->seqid;
                                  memcpy(res_OPEN4.OPEN4res_u.resok4.stateid.other,
                                         pstate_found_iterate->stateid_other, 12);

                                  memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.after), 0,
                                         sizeof(changeid4));
                                  res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                                      (changeid4) pentry_parent->internal_md.mod_time;
                                  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = TRUE;

                                  /* No delegation */
                                  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                                      OPEN_DELEGATE_NONE;
                                  res_OPEN4.OPEN4res_u.resok4.rflags =
                                      OPEN4_RESULT_LOCKTYPE_POSIX;

                                  /* Now produce the filehandle to this file */
                                  if((pnewfsal_handle =
                                      cache_inode_get_fsal_handle(pentry_lookup,
                                                                  &cache_status)) == NULL)
                                    {
                                      res_OPEN4.status = nfs4_Errno(cache_status);
                                      return res_OPEN4.status;
                                    }

                                  /* Allocation of a new file handle */
                                  if((rc = nfs4_AllocateFH(&newfh4)) != NFS4_OK)
                                    {
                                      res_OPEN4.status = rc;
                                      return res_OPEN4.status;
                                    }

                                  /* Building a new fh */
                                  if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data))
                                    {
                                      res_OPEN4.status = NFS4ERR_SERVERFAULT;
                                      return res_OPEN4.status;
                                    }

                                  /* This new fh replaces the current FH */
                                  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
                                  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
                                         newfh4.nfs_fh4_len);

                                  data->current_entry = pentry_lookup;
                                  data->current_filetype = REGULAR_FILE;

                                  /* regular exit */
                                  res_OPEN4.status = NFS4_OK;
                                  return res_OPEN4.status;
                                }

                            }
                          /* if( pstate_found_iterate != NULL ) */
                          pstate_previous_iterate = pstate_found_iterate;
                        }
                      while(pstate_found_iterate != NULL);
                    }
                }

              /* Managing GUARDED4 mode */
              if(cache_status != CACHE_INODE_SUCCESS)
                res_OPEN4.status = nfs4_Errno(cache_status);
              else
                res_OPEN4.status = NFS4ERR_EXIST;       /* File already exists */
              return res_OPEN4.status;
            }

          /*  if( cache_status != CACHE_INODE_NOT_FOUND ), if file already exists basically */
          LogFullDebug(COMPONENT_NFS_V4, "    OPEN open.how = %d\n", arg_OPEN4.openhow.openflag4_u.how.mode);

          create_arg.use_pnfs = FALSE;
#ifdef _USE_PNFS
          /*  set the file has "managed via pNFS" */
          if(data->pexport->options & EXPORT_OPTION_USE_PNFS)
            create_arg.use_pnfs = TRUE;
#endif                          /* _USE_PNFS */

          /* Create the file, if we reach this point, it does not exist, we can create it */
          if((pentry_newfile = cache_inode_create(pentry_parent,
                                                  &filename,
                                                  REGULAR_FILE,
                                                  mode,
                                                  &create_arg,
                                                  &attr_newfile,
                                                  data->ht,
                                                  data->pclient,
                                                  data->pcontext, &cache_status)) == NULL)
            {
              /* If the file already exists, this is not an error if open mode is UNCHECKED */
              if(cache_status != CACHE_INODE_ENTRY_EXISTS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  return res_OPEN4.status;
                }
              else
                {
                  /* If this point is reached, then the file already exists, cache_status == CACHE_INODE_ENTRY_EXISTS and pentry_newfile == NULL 
                     This probably means EXCLUSIVE4 mode is used and verifier matches. pentry_newfile is then set to pentry_lookup */
                  pentry_newfile = pentry_lookup;
                }
            }

          /* Prepare state management structure */
          candidate_type = CACHE_INODE_STATE_SHARE;
          candidate_data.share.share_deny = arg_OPEN4.share_deny;
          candidate_data.share.share_access = arg_OPEN4.share_access;
          candidate_data.share.lockheld = 0;

          /* If file is opened under mode EXCLUSIVE4, open verifier should be kept to detect non vicious double open */
          if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
            {
              strncpy(candidate_data.share.oexcl_verifier,
                      arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createverf,
                      NFS4_VERIFIER_SIZE);
            }

          if(cache_inode_add_state(pentry_newfile,
                                   candidate_type,
                                   &candidate_data,
                                   powner,
                                   data->pclient,
                                   data->pcontext,
                                   &pfile_state, &cache_status) != CACHE_INODE_SUCCESS)
            {
              /* Seqid has to be incremented even in this case */
              P(powner->lock);
              powner->seqid += 1;
              V(powner->lock);

              res_OPEN4.status = NFS4ERR_SHARE_DENIED;
              return res_OPEN4.status;
            }

          if(AttrProvided == TRUE)      /* Set the attribute if provided */
            {
              if((cache_status = cache_inode_setattr(pentry_newfile,
                                                     &sattr,
                                                     data->ht,
                                                     data->pclient,
                                                     data->pcontext,
                                                     &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  return res_OPEN4.status;
                }

            }

          /* Set the openflags variable */
          if(arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE)
            openflags |= FSAL_O_RDONLY;
          if(arg_OPEN4.share_deny & OPEN4_SHARE_DENY_READ)
            openflags |= FSAL_O_WRONLY;
          if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE)
            openflags = FSAL_O_RDWR;
          if(arg_OPEN4.share_access != 0)
            openflags = FSAL_O_RDWR;    /* @todo : BUGAZOMEU : Something better later */

          /* Open the file */
          if(cache_inode_open_by_name(pentry_parent,
                                      &filename,
                                      pentry_newfile,
                                      data->pclient,
                                      openflags,
                                      data->pcontext,
                                      &cache_status) != CACHE_INODE_SUCCESS)
            {
              /* Seqid has to be incremented even in this case */
              P(powner->lock);
              powner->seqid += 1;
              V(powner->lock);

              res_OPEN4.status = NFS4ERR_ACCESS;
              return res_OPEN4.status;
            }

          break;

        case OPEN4_NOCREATE:
          /* It was not a creation, but a regular open */
          /* The filehandle to the new file replaces the current filehandle */
          if(pentry_newfile == NULL)
            {
              if((pentry_newfile = cache_inode_lookup(pentry_parent,
                                                      &filename,
                                                      &attr_newfile,
                                                      data->ht,
                                                      data->pclient,
                                                      data->pcontext,
                                                      &cache_status)) == NULL)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  return res_OPEN4.status;
                }
            }

          /* OPEN4 is to be done on a file */
          if(pentry_newfile->internal_md.type != REGULAR_FILE)
            {
              if(pentry_newfile->internal_md.type == DIR_BEGINNING
                 || pentry_newfile->internal_md.type == DIR_CONTINUE)
                {
                  res_OPEN4.status = NFS4ERR_ISDIR;
                  return res_OPEN4.status;
                }
              else if(pentry_newfile->internal_md.type == SYMBOLIC_LINK)
                {
                  res_OPEN4.status = NFS4ERR_SYMLINK;
                  return res_OPEN4.status;
                }
              else
                {
                  res_OPEN4.status = NFS4ERR_INVAL;
                  return res_OPEN4.status;
                }
            }

          /* If the file is opened for write, OPEN4 while deny share write access,
           * in this case, check caller has write access to the file */
          if(arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE)
            {
              if(cache_inode_access(pentry_newfile,
                                    FSAL_W_OK,
                                    data->ht,
                                    data->pclient,
                                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  return res_OPEN4.status;
                }
              openflags = FSAL_O_WRONLY;
            }

          /* Same check on read: check for readability of a file before opening it for read */
          if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ)
            {
              if(cache_inode_access(pentry_newfile,
                                    FSAL_R_OK,
                                    data->ht,
                                    data->pclient,
                                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  return res_OPEN4.status;
                }
              openflags = FSAL_O_RDONLY;
            }

          /* Same check on write */
          if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE)
            {
              if(cache_inode_access(pentry_newfile,
                                    FSAL_W_OK,
                                    data->ht,
                                    data->pclient,
                                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  return res_OPEN4.status;
                }
              openflags = FSAL_O_RDWR;
            }

          /* Try to find if the same open_owner already has acquired a stateid for this file */
          pstate_found_iterate = NULL;
          pstate_previous_iterate = NULL;
          do
            {
              cache_inode_state_iterate(pentry_newfile,
                                        &pstate_found_iterate,
                                        pstate_previous_iterate,
                                        data->pclient, data->pcontext, &cache_status);
              if(cache_status == CACHE_INODE_STATE_ERROR)
                break;          /* Get out of the loop */

              if(cache_status == CACHE_INODE_INVALID_ARGUMENT)
                {
                  res_OPEN4.status = NFS4ERR_INVAL;
                  return res_OPEN4.status;
                }

              /* Check is open_owner is the same */
              if(pstate_found_iterate != NULL)
                {
                  if((pstate_found_iterate->state_type == CACHE_INODE_STATE_SHARE) &&
                     (pstate_found_iterate->powner->clientid == arg_OPEN4.owner.clientid)
                     &&
                     ((pstate_found_iterate->powner->owner_len ==
                       arg_OPEN4.owner.owner.owner_len)
                      &&
                      (!memcmp
                       (arg_OPEN4.owner.owner.owner_val,
                        pstate_found_iterate->powner->owner_val,
                        pstate_found_iterate->powner->owner_len))))
                    {
                      /* We'll be re-using the found state */
                      pstate_found_same_owner = pstate_found_iterate;
                    }
                  else
                    {

                      /* This is a different owner, check for possible conflicts */

                      if(memcmp(arg_OPEN4.owner.owner.owner_val,
                                pstate_found_iterate->powner->owner_val,
                                pstate_found_iterate->powner->owner_len))
                        {
                          switch (pstate_found_iterate->state_type)
                            {
                            case CACHE_INODE_STATE_SHARE:
                              if((pstate_found_iterate->state_data.share.
                                  share_access & OPEN4_SHARE_ACCESS_WRITE)
                                 && (arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE))
                                {
                                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                                  return res_OPEN4.status;
                                }

                              break;
                            }
                        }

                    }

                  /* In all cases opening in read access a read denied file or write access to a write denied file 
                   * should fail, even if the owner is the same, see discussion in 14.2.16 and 8.9 */
                  if(pstate_found_iterate->state_type == CACHE_INODE_STATE_SHARE)
                    {
                      /* deny read access on read denied file */
                      if((pstate_found_iterate->state_data.share.
                          share_deny & OPEN4_SHARE_DENY_READ)
                         && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ))
                        {
                          /* Seqid has to be incremented even in this case */
                          P(powner->lock);
                          powner->seqid += 1;
                          V(powner->lock);

                          powner->seqid += 1;
                          res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                          return res_OPEN4.status;
                        }

                      /* deny write access on write denied file */
                      if((pstate_found_iterate->state_data.share.
                          share_deny & OPEN4_SHARE_DENY_WRITE)
                         && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE))
                        {
                          /* Seqid has to be incremented even in this case */
                          P(powner->lock);
                          powner->seqid += 1;
                          V(powner->lock);

                          res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                          return res_OPEN4.status;
                        }

                    }

                }
              /*  if( pstate_found_iterate != NULL ) */
              pstate_previous_iterate = pstate_found_iterate;
            }
          while(pstate_found_iterate != NULL);

          if(pstate_found_same_owner != NULL)
            {
              pfile_state = pstate_found_same_owner;
              pfile_state->seqid += 1;

              P(powner->lock);
              powner->seqid += 1;
              V(powner->lock);

            }
          else
            {
              /* Set the state for the related file */
              /* Prepare state management structure */
              candidate_type = CACHE_INODE_STATE_SHARE;
              candidate_data.share.share_deny = arg_OPEN4.share_deny;
              candidate_data.share.share_access = arg_OPEN4.share_access;

              if(cache_inode_add_state(pentry_newfile,
                                       candidate_type,
                                       &candidate_data,
                                       powner,
                                       data->pclient,
                                       data->pcontext,
                                       &pfile_state,
                                       &cache_status) != CACHE_INODE_SUCCESS)
                {
                  /* Seqid has to be incremented even in this case */
                  P(powner->lock);
                  powner->seqid += 1;
                  V(powner->lock);

                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                  return res_OPEN4.status;
                }
            }

          /* Open the file */
          if(cache_inode_open_by_name(pentry_parent,
                                      &filename,
                                      pentry_newfile,
                                      data->pclient,
                                      openflags,
                                      data->pcontext,
                                      &cache_status) != CACHE_INODE_SUCCESS)
            {
              /* Seqid has to be incremented even in this case */
              P(powner->lock);
              powner->seqid += 1;
              V(powner->lock);

              res_OPEN4.status = NFS4ERR_ACCESS;
              return res_OPEN4.status;
            }
          break;

        default:
          /* Seqid has to be incremented even in this case */
          if(powner != NULL)
            {
              P(powner->lock);
              powner->seqid += 1;
              V(powner->lock);
            }

          res_OPEN4.status = NFS4ERR_INVAL;
          return res_OPEN4.status;
          break;
        }                       /* switch( arg_OPEN4.openhow.opentype ) */

      break;

    case CLAIM_PREVIOUS:
      break;

    default:
      /* Seqid has to be incremented even in this case */
      if(powner != NULL)
        {
          P(powner->lock);
          powner->seqid += 1;
          V(powner->lock);
        }

      res_OPEN4.status = NFS4ERR_INVAL;
      return res_OPEN4.status;
      break;
    }                           /*  switch(  arg_OPEN4.claim.claim ) */

  /* Now produce the filehandle to this file */
  if((pnewfsal_handle =
      cache_inode_get_fsal_handle(pentry_newfile, &cache_status)) == NULL)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      return res_OPEN4.status;
    }

  /* Allocation of a new file handle */
  if((rc = nfs4_AllocateFH(&newfh4)) != NFS4_OK)
    {
      res_OPEN4.status = rc;
      return res_OPEN4.status;
    }

  /* Building a new fh */
  if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data))
    {
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      return res_OPEN4.status;
    }

  /* This new fh replaces the current FH */
  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val, newfh4.nfs_fh4_len);

  data->current_entry = pentry_newfile;
  data->current_filetype = REGULAR_FILE;

  /* No do not need newfh any more */
  Mem_Free((char *)newfh4.nfs_fh4_val);

  /* Status of parent directory after the operation */
  if((cache_status = cache_inode_getattr(pentry_parent,
                                         &attr_parent,
                                         data->ht,
                                         data->pclient,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      return res_OPEN4.status;
    }

  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 2;
  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
      (uint32_t *) Mem_Alloc(res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len *
                             sizeof(uint32_t))) == NULL)
    {
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      return res_OPEN4.status;
    }
  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val[0] = 0;       /* No Attributes set */
  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val[1] = 0;       /* No Attributes set */

  if(arg_OPEN4.openhow.opentype == OPEN4_CREATE)
    {
      tmp_int = 2;
      tmp_attr[0] = FATTR4_SIZE;
      tmp_attr[1] = FATTR4_MODE;
      nfs4_list_to_bitmap4(&(res_OPEN4.OPEN4res_u.resok4.attrset), &tmp_int, tmp_attr);
      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 2;
    }

  res_OPEN4.OPEN4res_u.resok4.cinfo.after =
      (changeid4) pentry_parent->internal_md.mod_time;
  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = TRUE;

  res_OPEN4.OPEN4res_u.resok4.stateid.seqid = powner->seqid;
  memcpy(res_OPEN4.OPEN4res_u.resok4.stateid.other, pfile_state->stateid_other, 12);

  /* No delegation */
  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type = OPEN_DELEGATE_NONE;

  res_OPEN4.OPEN4res_u.resok4.rflags = OPEN4_RESULT_LOCKTYPE_POSIX;

  /* regular exit */
  res_OPEN4.status = NFS4_OK;
  return res_OPEN4.status;
}                               /* nfs41_op_open */

/**
 * nfs4_op_open_Free: frees what was allocared to handle nfs4_op_open.
 * 
 * Frees what was allocared to handle nfs4_op_open.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs41_op_open_Free(OPEN4res * resp)
{
  Mem_Free((char *)resp->OPEN4res_u.resok4.attrset.bitmap4_val);
  resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;

  return;
}                               /* nfs41_op_open_Free */
