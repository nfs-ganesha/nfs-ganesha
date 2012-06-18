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
 * @file    nfs41_op_open.c
 * @brief   Routines used for managing the NFS41 COMPOUND functions.
 *
 * Routines used for managing the NFS41 COMPOUND functions.
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
#include "cache_inode_lru.h"

/**
 * @brief Implement NFS4_OP_OPEN0
 *
 * This function potentially creates and opens a regular file.
 *
 * @param[in]     op    Arguments for nfs4_op
 * @param[in,out] data  Compound request's data
 * @param[out]    resp  Results for nfs4_op
 *
 * @return per RFC5661 pp. 369-70
 *
 */

#define arg_OPEN4 op->nfs_argop4_u.opopen
#define res_OPEN4 resp->nfs_resop4_u.opopen

int nfs41_op_open(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t             * entry_parent = NULL;
  cache_entry_t             * entry_lookup = NULL;
  cache_entry_t             * entry_newfile = NULL;
  fsal_attrib_list_t          attr_parent;
  fsal_attrib_list_t          attr;
  fsal_attrib_list_t          attr_newfile;
  fsal_attrib_list_t          sattr;
  fsal_openflags_t            openflags = 0;
  cache_inode_status_t        cache_status = CACHE_INODE_SUCCESS;
  state_status_t              state_status;
  int                         retval;
  fsal_name_t                 filename;
  bool_t                      AttrProvided = FALSE;
  bool_t                      ReuseState = FALSE;
  fsal_accessmode_t           mode = 0600;
  nfs_fh4                     newfh4;
  struct alloc_file_handle_v4 new_handle;
  state_data_t                candidate_data;
  state_type_t                candidate_type;
  state_t                   * file_state = NULL;
  state_t                   * state_iterate;
  state_nfs4_owner_name_t     owner_name;
  state_owner_t             * owner = NULL;
  const char                * tag = "OPEN";
  const char                * cause = "OOPS";
  const char                * cause2 = "";
  struct glist_head         * glist;
#ifdef _USE_QUOTA
  fsal_status_t               fsal_status;
#endif

  LogDebug(COMPONENT_STATE,
           "Entering NFS v4.1 OPEN handler -----------------------------------------------------");

  newfh4.nfs_fh4_val = (caddr_t) &new_handle;
  newfh4.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);

  fsal_accessflags_t write_access = FSAL_WRITE_ACCESS;
  fsal_accessflags_t read_access = FSAL_READ_ACCESS;

  resp->resop = NFS4_OP_OPEN;
  res_OPEN4.status = NFS4_OK;

  uint32_t tmp_attr[2];

  cache_inode_create_arg_t create_arg = {
       .newly_created_dir = FALSE
  };

  memset(&create_arg, 0, sizeof(create_arg));

  /*
   * Do basic checks on a filehandle
   */
  res_OPEN4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_OPEN4.status != NFS4_OK)
    goto out;

  /* This can't be done on the pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_ROFS;
      LogDebug(COMPONENT_STATE,
               "NFS41 OPEN returning NFS4ERR_ROFS");
      goto out;
    }

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH))) {
    res_OPEN4.status = nfs4_op_open_xattr(op, data, resp);
    goto out;
  }

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
                                                   data->pexport,
                                                   &retval)) == NULL)
        {
          res_OPEN4.status = NFS4ERR_RESOURCE;
          LogDebug(COMPONENT_STATE,
                   "NFS41 OPEN returning NFS4ERR_RESOURCE after trying to repopulate cache");
          goto out;
        }
    }

  /* Set parent */
  entry_parent = data->current_entry;

  /* First switch is based upon claim type */
  switch (arg_OPEN4.claim.claim)
    {
    case CLAIM_DELEGATE_CUR:
    case CLAIM_DELEGATE_PREV:
      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          LogDebug(COMPONENT_STATE,
                   "NFS41 OPEN returning NFS4ERR_NAMETOOLONG for CLAIM_DELEGATE");
          goto out;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          LogDebug(COMPONENT_STATE,
                   "NFS41 OPEN returning NFS4ERR_INVAL for CLAIM_DELEGATE");
          goto out;
        }

      res_OPEN4.status = NFS4ERR_NOTSUPP;
      LogDebug(COMPONENT_STATE,
               "NFS41 OPEN returning NFS4ERR_NOTSUPP for CLAIM_DELEGATE");
      goto out;

    case CLAIM_NULL:
      cause = "CLAIM_NULL";

      /* Is this open_owner known? If so, get it so we can use replay cache */
      convert_nfs4_open_owner(&arg_OPEN4.owner,
                              &owner_name,
                              data->psession->clientid);

      if(!nfs4_owner_Get_Pointer(&owner_name, &owner))
        {
          LogFullDebug(COMPONENT_STATE,
                       "OPEN new owner");
        }
      else
        {
          if(isFullDebug(COMPONENT_STATE))
            {
              char str[HASHTABLE_DISPLAY_STRLEN];

              display_nfs4_owner(owner, str);

              LogFullDebug(COMPONENT_STATE,
                           "A previously known open_owner is used:%p %s arg_OPEN4.seqid=%u",
                           owner, str, arg_OPEN4.seqid);
            }
        }

      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          goto out;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          cause2 = " (empty filename)";
          goto out;
        }

      /* Check if asked attributes are correct */
      if(arg_OPEN4.openhow.openflag4_u.how.mode == GUARDED4 ||
         arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4)
        {
          if(!nfs4_Fattr_Supported
             (&arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs))
            {
              res_OPEN4.status = NFS4ERR_ATTRNOTSUPP;
              goto out;
            }

          /* Do not use READ attr, use WRITE attr */
          if(!nfs4_Fattr_Check_Access
             (&arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs,
              FATTR4_ATTR_WRITE))
            {
              res_OPEN4.status = NFS4ERR_INVAL;
              cause2 = " (bad attr)";
              goto out;
            }
        }

      /* Check if filename is correct */
      if((cache_status =
          cache_inode_error_convert(FSAL_buffdesc2name
                                    ((fsal_buffdesc_t *) & arg_OPEN4.claim.open_claim4_u.
                                     file, &filename))) != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          cause2 = " FSAL_buffdesc2name";
          goto out;
        }

      /* Check parent */
      entry_parent = data->current_entry;

      /* Parent must be a directory */
      if(entry_parent->type != DIRECTORY)
        {
          /* Parent object is not a directory... */
          if(entry_parent->type == SYMBOLIC_LINK)
            res_OPEN4.status = NFS4ERR_SYMLINK;
          else
            res_OPEN4.status = NFS4ERR_NOTDIR;

          cause2 = " (parent not directory)";
          goto out;
        }

      /* What kind of open is it ? */
      LogFullDebug(COMPONENT_STATE,
                   "OPEN: Claim type = %d, Open Type = %d, Share Deny = %d, "
                   "Share Access = %d ",
                   arg_OPEN4.claim.claim,
                   arg_OPEN4.openhow.opentype,
                   arg_OPEN4.share_deny,
                   arg_OPEN4.share_access);

      /* It this a known client id ? */
      LogDebug(COMPONENT_STATE,
               "OPEN Client id = %llx",
               (unsigned long long)arg_OPEN4.owner.clientid);

      /* Is this open_owner known ? */
      if(owner == NULL)
        {
          /* This open owner is not known yet, allocated and set up a
             new one */
          owner = create_nfs4_owner(&owner_name,
                                    data->psession->pclientid_record,
                                    STATE_OPEN_OWNER_NFSV4,
                                    NULL,
                                    1); /* NFSv4.1 specific, initial
                                           seqid is 1 */

          if(owner == NULL)
            {
              res_OPEN4.status = NFS4ERR_RESOURCE;
              LogDebug(COMPONENT_STATE,
                       "NFS41 OPEN returning NFS4ERR_RESOURCE for CLAIM_NULL "
                       " (could not create NFS41 Owner");
              goto out;
            }
        }

      /* Status of parent directory before the operation */
      if(cache_inode_getattr(entry_parent,
                             &attr_parent,
                             &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          cause2 = " cache_inode_getattr";
          goto out;
        }

      res_OPEN4.OPEN4res_u.resok4.cinfo.before
           = cache_inode_get_changeid4(entry_parent);

      /* CLient may have provided fattr4 to set attributes at creation time */
      if(arg_OPEN4.openhow.openflag4_u.how.mode == GUARDED4 ||
         arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4)
        {
          if(arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createattrs.attrmask.
             bitmap4_len != 0)
            {
              /* Convert fattr4 so nfs4_sattr */
              res_OPEN4.status =
                  nfs4_Fattr_To_FSAL_attr(&sattr,
                                          &(arg_OPEN4.openhow.openflag4_u.how.
                                            createhow4_u.createattrs));

              if(res_OPEN4.status != NFS4_OK)
                {
                  cause2 = " (nfs4_Fattr_To_FSAL_attr failed)";
                  goto out;
                }

              AttrProvided = TRUE;
            }
        }

      /* Second switch is based upon "openhow" */
      switch (arg_OPEN4.openhow.opentype)
        {
        case OPEN4_CREATE:
          /* a new file is to be created */
#ifdef _USE_QUOTA
          /* if quota support is active, then we should check is the FSAL allows inode creation or not */
	  fsal_status = data->pexport->export_hdl->ops->check_quota(data->pexport->export_hdl,
								    data->pexport->fullpath, 
								    FSAL_QUOTA_INODES,
								    &data->user_credentials);
          if( FSAL_IS_ERROR( fsal_status ) )
            {
              res_OPEN4.status = NFS4ERR_DQUOT ;
              return res_OPEN4.status;
            }
#endif /* _USE_QUOTA */

          if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
            cause = "OPEN4_CREATE EXCLUSIVE";
          else
            cause = "OPEN4_CREATE";

          /* Does a file with this name already exist ? */
          entry_lookup = cache_inode_lookup(entry_parent,
                                            &filename,
                                            &attr_newfile,
                                            &data->user_credentials,
                                            &cache_status);

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
                      if(cache_inode_access(entry_lookup,
                                            write_access,
                                            &data->user_credentials,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          goto out;
                        }
                      openflags = FSAL_O_WRONLY;
                    }

                  /* Same check on read: check for readability of a file before opening it for read */
                  if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ)
                    {
                      if(cache_inode_access(entry_lookup,
                                            read_access,
                                            &data->user_credentials,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          goto out;
                        }
                      openflags = FSAL_O_RDONLY;
                    }

                  if(AttrProvided == TRUE)      /* Set the attribute if provided */
                    {
                      if(cache_inode_setattr(entry_lookup,
                                             &sattr,
                                             &data->user_credentials,
                                             &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = nfs4_Errno(cache_status);
                          cause2 = " cache_inode_setattr";
                          goto out;
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
                      if(cache_inode_access(entry_lookup,
                                            write_access,
                                            &data->user_credentials,
                                            &cache_status) != CACHE_INODE_SUCCESS)
                        {
                          res_OPEN4.status = NFS4ERR_ACCESS;
                          cause2 = " cache_inode_access";
                          goto out;
                        }
                      openflags = FSAL_O_RDWR;
                    }

                  /* Set the state for the related file */

                  /* Prepare state management structure */
                  candidate_type                    = STATE_TYPE_SHARE;
                  candidate_data.share.share_deny   = arg_OPEN4.share_deny;
                  candidate_data.share.share_access = arg_OPEN4.share_access;

                  if(state_add(entry_lookup,
                               candidate_type,
                               &candidate_data,
                               owner,
                               &file_state,
                               &state_status) != STATE_SUCCESS)
                    {
                      res_OPEN4.status = nfs4_Errno_state(state_status);
                      cause2 = " (state_add failed)";
                      goto out;
                    }
                  /* Attach this open to an export */
                  file_state->state_pexport = data->pexport;
                  P(data->pexport->exp_state_mutex);
                  glist_add_tail(&data->pexport->exp_state_list,
                                 &file_state->state_export_list);
                  V(data->pexport->exp_state_mutex);

                  init_glist(&file_state->state_data.share.share_lockstates);

                  /* Attach this open to an export */
                  file_state->state_pexport = data->pexport;
                  P(data->pexport->exp_state_mutex);
                  glist_add_tail(&data->pexport->exp_state_list, &file_state->state_export_list);
                  V(data->pexport->exp_state_mutex);

                  /* Open the file */
                  if(cache_inode_open(entry_lookup,
                                      openflags,
				      &data->user_credentials,
                                      0,
                                      &cache_status) != CACHE_INODE_SUCCESS)
                    {
                      // TODO FSF: huh????
                      res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                      res_OPEN4.status = NFS4ERR_ACCESS;
                      cause2 = " cache_inode_open";
                      goto out;
                    }

                  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 3;
                  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
                      gsh_calloc(res_OPEN4.OPEN4res_u.resok4.attrset.
                                 bitmap4_len, sizeof(uint32_t))) == NULL)
                    {
                      res_OPEN4.status = NFS4ERR_RESOURCE;
                      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0;
                      cause2 = " (allocation of bitmap failed)";
                      goto out;
                    }

                  res_OPEN4.OPEN4res_u.resok4.cinfo.after
                       = cache_inode_get_changeid4(entry_parent);
                  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

                  /* No delegation */
                  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                      OPEN_DELEGATE_NONE;

                  res_OPEN4.OPEN4res_u.resok4.rflags = OPEN4_RESULT_LOCKTYPE_POSIX;

                  /* Building a new fh */
                  if(!nfs4_FSALToFhandle(&newfh4,
                                         entry_lookup->obj_handle,
                                         data))
                    {
                      res_OPEN4.status = NFS4ERR_SERVERFAULT;
                      cause2 = " (nfs4_FSALToFhandle failed)";
                      goto out;
                    }

                  /* This new fh replaces the current FH */
                  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
                  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
                         newfh4.nfs_fh4_len);

                  data->current_entry = entry_lookup;
                  if (cache_inode_lru_ref(data->current_entry,
                                          0)
                      != CACHE_INODE_SUCCESS)
                    {
                      LogFatal(COMPONENT_CACHE_INODE_LRU,
                               "Inconsistency found in LRU management.");
                    }
                  data->current_filetype = REGULAR_FILE;

                  /* regular exit */
                  goto out_success;
                }

              /* if open is EXCLUSIVE, but verifier is the same, return NFS4_OK (RFC3530 page 173) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
                {
                  if((entry_lookup != NULL)
                     && (entry_lookup->type == REGULAR_FILE))
                    {
                      pthread_rwlock_rdlock(&entry_lookup->state_lock);
                      glist_for_each(glist, &entry_lookup->state_list)
                        {
                          state_iterate = glist_entry(glist, state_t, state_list);

                          /* Check is open_owner is the same */
                          if((state_iterate->state_type == STATE_TYPE_SHARE)
                             && !memcmp(arg_OPEN4.owner.owner.owner_val,
                                        state_iterate->state_powner->so_owner_val,
                                        state_iterate->state_powner->so_owner_len)
                             && !memcmp(state_iterate->state_data.share.
                                        share_oexcl_verifier,
                                        arg_OPEN4.openhow.openflag4_u.how.
                                        createhow4_u.createverf, NFS4_VERIFIER_SIZE))
                            {

                              /* A former open EXCLUSIVE with same owner and verifier was found, resend it */
                              memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.after), 0,
                                     sizeof(changeid4));
                              res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                                   cache_inode_get_changeid4(entry_parent);
                              res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

                              /* No delegation */
                              res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                                  OPEN_DELEGATE_NONE;

                              res_OPEN4.OPEN4res_u.resok4.rflags =
                                  OPEN4_RESULT_LOCKTYPE_POSIX;

                              /* Building a new fh */
                              if(!nfs4_FSALToFhandle(&newfh4,
                                                     entry_lookup->obj_handle,
                                                     data))
                                {
                                  res_OPEN4.status = NFS4ERR_SERVERFAULT;
                                  cause2 = " nfs4_FSALToFhandle failed";
                                  pthread_rwlock_unlock(&entry_lookup
                                                        ->state_lock);
                                  goto out;
                                }

                              /* This new fh replaces the current FH */
                              data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
                              memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
                                     newfh4.nfs_fh4_len);

                              data->current_entry = entry_lookup;
                              if (cache_inode_lru_ref(data->current_entry, 0)
                                  != CACHE_INODE_SUCCESS)
                                {
                                  LogFatal(COMPONENT_CACHE_INODE_LRU,
                                           "Inconsistency found in LRU "
                                           "management.");
                                }
                              data->current_filetype = REGULAR_FILE;

                              pthread_rwlock_unlock(&entry_lookup
                                                    ->state_lock);
                              /* regular exit */
                              goto out_success;
                            }
                        }
                    }
                }

              /* Managing GUARDED4 mode */
              if(cache_status != CACHE_INODE_SUCCESS)
                res_OPEN4.status = nfs4_Errno(cache_status);
              else
                res_OPEN4.status = NFS4ERR_EXIST;       /* File already exists */

              cause2 = "GUARDED4";
              goto out;
            }

          /*  if( cache_status != CACHE_INODE_NOT_FOUND ), if file already exists basically */
          LogFullDebug(COMPONENT_STATE,
                       "    OPEN open.how = %d",
                       arg_OPEN4.openhow.openflag4_u.how.mode);

          /* Create the file, if we reach this point, it does not exist, we can create it */
          if((entry_newfile = cache_inode_create(entry_parent,
                                                  &filename,
                                                  REGULAR_FILE,
                                                  mode,
                                                  &create_arg,
                                                  &attr_newfile,
                                                  &data->user_credentials, &cache_status)) == NULL)
            {
              /* If the file already exists, this is not an error if open mode is UNCHECKED */
              if(cache_status != CACHE_INODE_ENTRY_EXISTS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = " UNCHECKED cache_inode_create";
                  goto out;
                }
              else
                {
                  /* If this point is reached, then the file already
                     exists, cache_status == CACHE_INODE_ENTRY_EXISTS
                     and entry_newfile == NULL This probably means
                     EXCLUSIVE4 mode is used and verifier
                     matches. entry_newfile is then set to
                     entry_lookup */
                  entry_newfile = entry_lookup;
                }
            }

          /* Prepare state management structure */
          candidate_type                    = STATE_TYPE_SHARE;
          candidate_data.share.share_deny   = arg_OPEN4.share_deny;
          candidate_data.share.share_access = arg_OPEN4.share_access;

          /* If file is opened under mode EXCLUSIVE4, open verifier
             should be kept to detect non vicious double open */
          if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
            {
              strncpy(candidate_data.share.share_oexcl_verifier,
                      arg_OPEN4.openhow.openflag4_u.how.createhow4_u.createverf,
                      NFS4_VERIFIER_SIZE);
            }

          if(state_add(entry_newfile,
                       candidate_type,
                       &candidate_data,
                       owner,
                       &file_state, &state_status) != STATE_SUCCESS)
            {
              res_OPEN4.status = nfs4_Errno_state(state_status);
              cause2 = " state_add failed";
              goto out;
            }

          /* Attach this open to an export */
          file_state->state_pexport = data->pexport;
          P(data->pexport->exp_state_mutex);
          glist_add_tail(&data->pexport->exp_state_list,
                         &file_state->state_export_list);
          V(data->pexport->exp_state_mutex);

          init_glist(&file_state->state_data.share.share_lockstates);

          /* Attach this open to an export */
          file_state->state_pexport = data->pexport;
          P(data->pexport->exp_state_mutex);
          glist_add_tail(&data->pexport->exp_state_list, &file_state->state_export_list);
          V(data->pexport->exp_state_mutex);

          cache_status = CACHE_INODE_SUCCESS;

          if(AttrProvided == TRUE)      /* Set the attribute if provided */
            {
              if((cache_status = cache_inode_setattr(entry_newfile,
                                                     &sattr,
                                                     &data->user_credentials,
                                                     &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = " cache_inode_setattr";
                  goto out;
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
          if(cache_inode_open(entry_newfile,
                              openflags,
			      &data->user_credentials,
                              0,
                              &cache_status) != CACHE_INODE_SUCCESS)
            {
              res_OPEN4.status = NFS4ERR_ACCESS;
              cause2 = " cache_inode_open";
              goto out;
            }

          break;

        case OPEN4_NOCREATE:
          /* It was not a creation, but a regular open */
          cause = "OPEN4_NOCREATE";

          /* The filehandle to the new file replaces the current filehandle */
          if(entry_newfile == NULL)
            {
              if((entry_newfile = cache_inode_lookup(entry_parent,
                                                     &filename,
                                                     &attr_newfile,
                                                     &data->user_credentials,
                                                     &cache_status)) == NULL)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = " cache_inode_lookup";
                  goto out;
                }
            }

          /* OPEN4 is to be done on a file */
          if(entry_newfile->type != REGULAR_FILE)
            {
              if(entry_newfile->type == DIRECTORY)
                {
                  res_OPEN4.status = NFS4ERR_ISDIR;
                  goto out;
                }
              else if(entry_newfile->type == SYMBOLIC_LINK)
                {
                  res_OPEN4.status = NFS4ERR_SYMLINK;
                  goto out;
                }
              else
                {
                  res_OPEN4.status = NFS4ERR_INVAL;
                  cause2 = " (not REGULAR_FILE)";
                  goto out;
                }
            }

          /* If the file is opened for write, OPEN4 while deny share write access,
           * in this case, check caller has write access to the file */
          if(arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE)
            {
              if(cache_inode_access(entry_newfile,
                                    write_access,
                                    &data->user_credentials, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  cause2 = " OPEN4_SHARE_DENY_WRITE cache_inode_access";
                  goto out;
                }
              openflags = FSAL_O_WRONLY;
            }

          /* Same check on read: check for readability of a file before opening it for read */
          if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ)
            {
              if(cache_inode_access(entry_newfile,
                                    read_access,
                                    &data->user_credentials, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  cause2 = " OPEN4_SHARE_ACCESS_READ cache_inode_access";
                  goto out;
                }
              openflags = FSAL_O_RDONLY;
            }

          /* Same check on write */
          if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE)
            {
              if(cache_inode_access(entry_newfile,
                                    write_access,
                                    &data->user_credentials, &cache_status) != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = NFS4ERR_ACCESS;
                  cause2 = " OPEN4_SHARE_ACCESS_WRITE cache_inode_access";
                  goto out;
                }
              openflags = FSAL_O_RDWR;
            }

          /* Try to find if the same open_owner already has acquired a
             stateid for this file */
          pthread_rwlock_wrlock(&entry_newfile->state_lock);
          glist_for_each(glist, &entry_newfile->state_list)
            {
              state_iterate = glist_entry(glist, state_t, state_list);

              // TODO FSF: currently only care about share types
              if(state_iterate->state_type != STATE_TYPE_SHARE)
                continue;

              /* Check is open_owner is the same */
              if((state_iterate->state_powner->so_owner.so_nfs4_owner.so_clientid == arg_OPEN4.owner.clientid) &&
                 ((state_iterate->state_powner->so_owner_len == arg_OPEN4.owner.owner.owner_len) &&
                  (!memcmp(arg_OPEN4.owner.owner.owner_val,
                           state_iterate->state_powner->so_owner_val,
                           state_iterate->state_powner->so_owner_len))))
                {
                  /* We'll be re-using the found state */
                  file_state = state_iterate;
                  ReuseState  = TRUE;
                }
              else
                {
                  /* This is a different owner, check for possible conflicts */
                  if((state_iterate->state_data.share.share_access & OPEN4_SHARE_ACCESS_WRITE)
                     && (arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE))
                    {
                      res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                      cause2 = " (OPEN4_SHARE_DENY_WRITE)";
                      pthread_rwlock_unlock(&entry_newfile->state_lock);
                      goto out;
                    }
                }

              /* In all cases opening in read access a read denied file or write access to a write denied file 
               * should fail, even if the owner is the same, see discussion in 14.2.16 and 8.9
               */

              /* deny read access on read denied file */
              if((state_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_READ)
                 && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ))
                {
                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                  cause2 = " (OPEN4_SHARE_ACCESS_READ)";
                  pthread_rwlock_unlock(&entry_newfile->state_lock);
                  goto out;
                }

              /* deny write access on write denied file */
              if((state_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE)
                 && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE))
                {
                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                  cause2 = " (OPEN4_SHARE_ACCESS_WRITE)";
                  pthread_rwlock_unlock(&entry_newfile->state_lock);
                  goto out;
                }
            }

          if(file_state == NULL)
            {
              /* Set the state for the related file */
              /* Prepare state management structure */
              candidate_type                    = STATE_TYPE_SHARE;
              candidate_data.share.share_deny   = arg_OPEN4.share_deny;
              candidate_data.share.share_access = arg_OPEN4.share_access;

              if(state_add_impl(entry_newfile,
                                candidate_type,
                                &candidate_data,
                                owner,
                                &file_state,
                                &state_status) != STATE_SUCCESS)
                {
                  res_OPEN4.status = nfs4_Errno_state(state_status);
                  cause2 = " (state_add failed)";
                  pthread_rwlock_unlock(&entry_newfile->state_lock);
                  goto out;
                }

              /* Attach this open to an export */
              file_state->state_pexport = data->pexport;
              P(data->pexport->exp_state_mutex);
              glist_add_tail(&data->pexport->exp_state_list,
                             &file_state->state_export_list);
              V(data->pexport->exp_state_mutex);
              init_glist(&file_state->state_data.share.share_lockstates);

              /* Attach this open to an export */
              file_state->state_pexport = data->pexport;
              P(data->pexport->exp_state_mutex);
              glist_add_tail(&data->pexport->exp_state_list, &file_state->state_export_list);
              V(data->pexport->exp_state_mutex);
            }
          else 
            {
              /* Check if open from another export */
              if(file_state->state_pexport != data->pexport)
                {
                  LogEvent(COMPONENT_STATE,
                           "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
                           file_state->state_pexport->id,
                           file_state->state_pexport->fullpath,
                           data->pexport->id,
                           data->pexport->fullpath);
                  return STATE_INVALID_ARGUMENT;
                }
            }
          pthread_rwlock_unlock(&entry_newfile->state_lock);

          /* Open the file */
          if(cache_inode_open(entry_newfile,
                              openflags,
			      &data->user_credentials,
                              0,
                              &cache_status) != CACHE_INODE_SUCCESS)
            {
              res_OPEN4.status = NFS4ERR_ACCESS;
              cause2 = " cache_inode_open";
              goto out;
            }
          break;

        default:
          cause = "INVALID OPEN TYPE";
          res_OPEN4.status = NFS4ERR_INVAL;
          goto out;
        }                       /* switch( arg_OPEN4.openhow.opentype ) */

      break;

    case CLAIM_PREVIOUS:
      // TODO FSF: doesn't this need to do something to re-establish state?
      cause = "CLAIM_PREVIOUS";
      owner = NULL;
      break;

    default:
      /* Invalid claim type */
      cause = "INVALID CLAIM";
      res_OPEN4.status = NFS4ERR_INVAL;
      goto out;
    }                           /*  switch(  arg_OPEN4.claim.claim ) */

  /* Building a new fh */
  if(!nfs4_FSALToFhandle(&newfh4, entry_newfile->obj_handle, data))
    {
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      cause2 = " (nfs4_FSALToFhandle failed)";
      goto out;
    }

  /* This new fh replaces the current FH */
  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val, newfh4.nfs_fh4_len);

  data->current_entry = entry_newfile;
  if (cache_inode_lru_ref(data->current_entry, 0)
      != CACHE_INODE_SUCCESS)
    {
      LogFatal(COMPONENT_CACHE_INODE_LRU,
               "Inconsistency found in LRU management.");
    }
  data->current_filetype = REGULAR_FILE;

  /* Status of parent directory after the operation */
  if((cache_status = cache_inode_getattr(entry_parent,
                                         &attr_parent,
                                         &cache_status))
     != CACHE_INODE_SUCCESS)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      cause2 = " cache_inode_getattr";
      goto out;
    }

  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 3;
  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
      gsh_calloc(res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len,
                 sizeof(uint32_t))) == NULL)
    {
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0;
      cause2 = " (allocation of attr failed)";
      goto out;
    }

  if(arg_OPEN4.openhow.opentype == OPEN4_CREATE)
    {
      tmp_attr[0] = FATTR4_SIZE;
      tmp_attr[1] = FATTR4_MODE;
      nfs4_list_to_bitmap4(&(res_OPEN4.OPEN4res_u.resok4.attrset), 2, tmp_attr);
    }

  res_OPEN4.OPEN4res_u.resok4.cinfo.after
       = cache_inode_get_changeid4(entry_parent);
  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

  /* No delegation */
  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type = OPEN_DELEGATE_NONE;

  res_OPEN4.OPEN4res_u.resok4.rflags = OPEN4_RESULT_LOCKTYPE_POSIX;

 out_success:

  LogFullDebug(COMPONENT_STATE, "NFS41 OPEN returning NFS4_OK");

  /* regular exit */
  res_OPEN4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(file_state,
                 &res_OPEN4.OPEN4res_u.resok4.stateid,
                 data,
                 tag);

  /* If we are re-using stateid, then release extra reference to open owner */
  if(ReuseState)
    dec_state_owner_ref(owner);

 out:

  if(res_OPEN4.status != NFS4_OK)
    {
      const char *cause3 = "", *cause4 = "";

      if(cache_status != CACHE_INODE_SUCCESS)
        {
          cause3 = " returned ";
          cause4 = cache_inode_err_str(cache_status);
        }

      LogDebug(COMPONENT_STATE,
               "NFS41 OPEN returning %s for %s%s%s%s",
               nfsstat4_to_str(res_OPEN4.status),
               cause, cause2, cause3, cause4);

      /* Clean up if we have an error exit */
      if(file_state != NULL && !ReuseState)
        {
          /* Need to destroy open owner and state */
          if(state_del(file_state,
                       &state_status) != STATE_SUCCESS)
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "state_del failed with status %s",
                     state_err_str(state_status));
        }
      else if(owner != NULL)
        {
          /* Need to release the open owner */
          dec_state_owner_ref(owner);
        }
    }

  /* return cache entry references */
  if (entry_parent)
      cache_inode_put(entry_parent);

  if (entry_lookup)
      cache_inode_put(entry_lookup);

  if (entry_newfile)
      cache_inode_put(entry_newfile);

  return res_OPEN4.status;
}                               /* nfs41_op_open */

/**
 * @brief Free memory allocated for OPEN result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_OPEN operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs41_op_open_Free(OPEN4res *resp)
{
  gsh_free(resp->OPEN4res_u.resok4.attrset.bitmap4_val);
  resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;

  return;
}                               /* nfs41_op_open_Free */
