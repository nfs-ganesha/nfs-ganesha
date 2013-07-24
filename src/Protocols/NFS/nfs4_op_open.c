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
 * \file    nfs4_op_open.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.18 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_open.c : Routines used for managing the NFS4 COMPOUND functions.
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

static nfsstat4 nfs4_do_open(struct nfs_argop4  * op,
                             compound_data_t    * data,
                             cache_entry_t      * pentry_newfile,
                             cache_entry_t      * pentry_parent,
                             state_owner_t      * powner,
                             state_t           ** statep,
                             fsal_name_t        * filename,
                             fsal_openflags_t     openflags,
                             char              ** cause2);

static nfsstat4 nfs4_create_fh(compound_data_t *, cache_entry_t *, char **);

static state_t * find_existing_state(struct nfs_argop4 *op,
                                     compound_data_t *data,
                                     cache_entry_t *pentry_newfile);

/**
 * nfs4_op_open: NFS4_OP_OPEN, opens and eventually creates a regular file.
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

#define STATE_ADD " (state_add failed)"
#define STATE_SHARE_ADD  " (state_share_add failed)"
#define STATE_SHARE_UP   " (state_share_upgrade failed)"
#define CACHE_INODE_OPEN " cache_inode_open"
#define arg_OPEN4 op->nfs_argop4_u.opopen
#define res_OPEN4 resp->nfs_resop4_u.opopen

int nfs4_op_open(struct nfs_argop4 *op, compound_data_t *data,
    struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_open";

  cache_entry_t           * pentry_parent = NULL;
  cache_entry_t           * pentry_lookup = NULL;
  cache_entry_t           * pentry_newfile = NULL;
  fsal_attrib_list_t        sattr;
  fsal_openflags_t          openflags = 0;
  cache_inode_status_t      cache_status = CACHE_INODE_SUCCESS;
  state_status_t            state_status;
  int                       retval;
  fsal_name_t               filename;
  bool_t                    AttrProvided = FALSE;
  bool_t                    ReuseState = FALSE;
  fsal_accessmode_t         mode = 0600;
  nfs_client_id_t         * pclientid;
  state_t                 * pfile_state = NULL;
  state_nfs4_owner_name_t   owner_name;
  state_owner_t           * powner = NULL;
  const char              * tag = "OPEN";
  const char              * cause = "OOPS";
  const char              * cause2 = "";
  open_claim_type4          claim = arg_OPEN4.claim.claim;
  nfsstat4                  status4;
  uint32_t                  tmp_attr[2];
#ifdef _USE_QUOTA
  fsal_status_t            fsal_status;
#endif
  char                    * text = "";
  bool_t                    isnew;

  LogDebug(COMPONENT_STATE,
           "Entering NFS v4 OPEN handler -----------------------------------------------------");

  /* What kind of open is it ? */
  LogFullDebug(COMPONENT_STATE,
               "OPEN: Claim type = %d   Open Type = %d  Share Deny = %d   Share Access = %d ",
               arg_OPEN4.claim.claim,
               arg_OPEN4.openhow.opentype,
               arg_OPEN4.share_deny,
               arg_OPEN4.share_access);

  resp->resop = NFS4_OP_OPEN;
  res_OPEN4.status = NFS4_OK;
  res_OPEN4.OPEN4res_u.resok4.rflags = 0 ;

  /* Check export permissions if OPEN4_CREATE */
  if((arg_OPEN4.openhow.opentype == OPEN4_CREATE) &&
     ((data->export_perms.options & EXPORT_OPTION_MD_WRITE_ACCESS) == 0))
    {
      res_OPEN4.status = NFS4ERR_ROFS;

      LogDebug(COMPONENT_NFS_V4,
               "Status of OP_OPEN due to export permissions = %s",
               nfsstat4_to_str(res_OPEN4.status));

      return res_OPEN4.status;
    }

  /* Check export permissions if OPEN4_SHARE_ACCESS_WRITE */
  if(((arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE) != 0) &&
     ((data->export_perms.options & EXPORT_OPTION_WRITE_ACCESS) == 0))
    {
      res_OPEN4.status = NFS4ERR_ROFS;

      LogDebug(COMPONENT_NFS_V4,
               "Status of OP_OPEN due to export permissions = %s",
               nfsstat4_to_str(res_OPEN4.status));

      return res_OPEN4.status;
    }

  /* Do basic checks on a filehandle */
  res_OPEN4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_OPEN4.status != NFS4_OK)
    return res_OPEN4.status;

  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_PERM;

      LogDebug(COMPONENT_NFS_V4,
               "Status of OP_OPEN due to PseudoFS handle = %s",
               nfsstat4_to_str(res_OPEN4.status));

      return res_OPEN4.status;
    }

  /*
   * If Filehandle points to a xattr object, manage it via the xattrs
   * specific functions
   */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_open_xattr(op, data, resp);

  /* If data->current_entry is empty, repopulate it */
  if(data->current_entry == NULL)
    {
      /* refcount +1 */
      if((data->current_entry = nfs_FhandleToCache(NFS_V4,
                                                   NULL,
                                                   NULL,
                                                   &(data->currentFH),
                                                   NULL,
                                                   NULL,
                                                   &(res_OPEN4.status),
                                                   NULL,
                                                   data->pcontext,
                                                   &retval)) == NULL)
        {
          LogCrit(COMPONENT_STATE,
                   "NFS4 OPEN returning %s after "
                   "trying to repopulate cache",
                   nfsstat4_to_str(res_OPEN4.status));
          return res_OPEN4.status;
        }
    }

  if (claim == CLAIM_PREVIOUS)
      cause = "CLAIM_PREVIOUS";
  else
      cause = "CLAIM_NULL";

  /* Set parent */
  /* for CLAIM_PREVIOUS, currentFH is the file being reclaimed, not a dir */
  pentry_parent = data->current_entry;
  /* take an additional refcount for the local variable pentry_parent */
  if (cache_inode_lru_ref(pentry_parent, 0)
      != CACHE_INODE_SUCCESS)
    {
      LogMajor(COMPONENT_CACHE_INODE_LRU,
               "Inconsistency found in LRU management while getting a reference"
               " on a new file.");
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      goto out3;
    }


  /* It this a known client id ? */
  LogDebug(COMPONENT_STATE,
           "OPEN Client id = %llx",
           (unsigned long long)arg_OPEN4.owner.clientid);

  retval = nfs_client_id_get_confirmed(arg_OPEN4.owner.clientid, &pclientid);

  if(retval != CLIENT_ID_SUCCESS)
    {
      res_OPEN4.status = clientid_error_to_nfsstat(retval);
      cause2 = " (failed to find confirmed clientid)";
      goto out3;
    }

  /* Check if lease is expired and reserve it */
  P(pclientid->cid_mutex);

  if(!reserve_lease(pclientid))
    {
      V(pclientid->cid_mutex);

      dec_client_id_ref(pclientid);

      res_OPEN4.status = NFS4ERR_EXPIRED;
      cause2 = " (clientid expired)";
      goto out3;
    }

  V(pclientid->cid_mutex);

  if (arg_OPEN4.openhow.opentype == OPEN4_CREATE && claim != CLAIM_NULL) {
      res_OPEN4.status = NFS4ERR_INVAL;
      cause2 = " (create without claim type null)";
      goto out2;
  }

  /* Is this open_owner known? If so, get it so we can use replay cache */
  convert_nfs4_open_owner(&arg_OPEN4.owner, &owner_name);

  /* If this open owner is not known yet, allocated and set up a new one */
  powner = create_nfs4_owner(&owner_name,
                             pclientid,
                             STATE_OPEN_OWNER_NFSV4,
                             NULL,
                             0,
                             &isnew,
                             CARE_ALWAYS);

  if(powner == NULL)
    {
      res_OPEN4.status = NFS4ERR_RESOURCE;
      LogEvent(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_RESOURCE for CLAIM_NULL (could not create NFS4 Owner");
      dec_client_id_ref(pclientid);
      return res_OPEN4.status;
    }

  if(!isnew)
    {
      if(arg_OPEN4.seqid == 0)
        {
          LogDebug(COMPONENT_STATE,
                   "Previously known open_owner is used with seqid=0, ask the client to confirm it again");
          powner->so_owner.so_nfs4_owner.so_confirmed = FALSE;
        }
      else
        {
          /* Check for replay */
          if(!Check_nfs4_seqid(powner,
                               arg_OPEN4.seqid,
                               op,
                               data->current_entry,
                               resp,
                               tag))
            {
              /* Response is setup for us and LogDebug told what was wrong */
              /* Or if this is a seqid replay, find the file entry */
              /* and update currentFH */
              if(res_OPEN4.status == NFS4_OK)
                {
                  /* Check if filename is correct */
                  cache_status = utf8_to_name(&arg_OPEN4.claim.open_claim4_u.file,
                                  &filename);

                  if(cache_status != CACHE_INODE_SUCCESS)
                    {
                      res_OPEN4.status = nfs4_Errno(cache_status);
                      cause2 = " FSAL_buffdesc2name";
                      goto out1;
                    }
                  pentry_lookup = cache_inode_lookup(pentry_parent,
                                             &filename,
                                             NULL,
                                             data->pcontext,
                                             &cache_status);
                  if(pentry_lookup == NULL)
                    {
                      res_OPEN4.status = nfs4_Errno(cache_status);
                      cause2 = "lookup failed";
                      goto out1;
                    }
                  status4 = nfs4_create_fh(data, pentry_lookup, &text);
                  if(status4 != NFS4_OK)
                    {
                      cause2 = text;
                      res_OPEN4.status = status4;
                    }
                }
              goto out1;
            }
        }
    }

  if (nfs_in_grace() && claim != CLAIM_PREVIOUS)
    {
       cause2 = " (in grace period)";
       res_OPEN4.status = NFS4ERR_GRACE;
       goto out;
    }
  if (nfs_in_grace() && claim == CLAIM_PREVIOUS &&
     pclientid->cid_allow_reclaim != 1)
    {
       cause2 = " (client cannot reclaim)";
       res_OPEN4.status = NFS4ERR_NO_GRACE;
       goto out;
    }
  if (!nfs_in_grace() && claim == CLAIM_PREVIOUS)
    {
       cause2 = " (not in grace period)";
       res_OPEN4.status = NFS4ERR_NO_GRACE;
       goto out;
    }

  /*
   * check if share_access does not have any access set, or has invalid bits
   * that are set.  check that share_deny doesn't have any invalid bits set.
   */
  if (!(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_BOTH) ||
      (arg_OPEN4.share_access & ~OPEN4_SHARE_ACCESS_BOTH) ||
      (arg_OPEN4.share_deny & ~OPEN4_SHARE_DENY_BOTH))
   {
       res_OPEN4.status = NFS4ERR_INVAL;
       cause2 = " (invalid share_access or share_deny)";
       goto out;
   }

  /* Set openflags. */
  if(arg_OPEN4.share_access == OPEN4_SHARE_ACCESS_BOTH)
    openflags = FSAL_O_RDWR;
  else if(arg_OPEN4.share_access == OPEN4_SHARE_ACCESS_READ)
    openflags = FSAL_O_RDONLY;
  else if(arg_OPEN4.share_access == OPEN4_SHARE_ACCESS_WRITE)
    openflags = FSAL_O_WRONLY;

  /* First switch is based upon claim type */
  switch (claim)
    {
    case CLAIM_NULL:
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
      cache_status = utf8_to_name(&arg_OPEN4.claim.open_claim4_u.file,
                                  &filename);

      if(cache_status != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          cause2 = " FSAL_buffdesc2name";
          goto out;
        }

      /* Parent must be a directory */
      if((pentry_parent->type != DIRECTORY))
        {
          /* Parent object is not a directory... */
          if(pentry_parent->type == SYMBOLIC_LINK)
            res_OPEN4.status = NFS4ERR_SYMLINK;
          else
            res_OPEN4.status = NFS4ERR_NOTDIR;

          cause2 = " (parent not directory)";
          goto out;
        }

      /* Generate change info for before */
      res_OPEN4.OPEN4res_u.resok4.cinfo.before
           = cache_inode_get_changeid4(pentry_parent, data->pcontext);

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
                                            createhow4_u.createattrs),
                                          data->export_perms.anonymous_uid,
                                          data->export_perms.anonymous_gid);

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
          fsal_status = FSAL_check_quota( data->pexport->fullpath, 
                                          FSAL_QUOTA_INODES,
                                          FSAL_OP_CONTEXT_TO_UID( data->pcontext ) ) ;
          if( FSAL_IS_ERROR( fsal_status ) )
            {
              res_OPEN4.status = NFS4ERR_DQUOT ;
              dec_client_id_ref(pclientid);
              return res_OPEN4.status;
            }
#endif /* _USE_QUOTA */

          if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
            cause = "OPEN4_CREATE EXCLUSIVE";
          else
            cause = "OPEN4_CREATE";

          /* Does a file with this name already exist ? */
          pentry_lookup = cache_inode_lookup(pentry_parent,
                                             &filename,
                                             NULL,
                                             data->pcontext,
                                             &cache_status);
          if(cache_status != CACHE_INODE_NOT_FOUND)
            {
              if(cache_status != CACHE_INODE_SUCCESS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = "lookup failed";
                  goto out;
                }
              /* if open is UNCHECKED, return NFS4_OK (RFC3530 page 172) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == UNCHECKED4)
                {

                  /*
                   * we are not creating the file, attributes on recreate
                   * should be ignored except for size = 0.
                   */
                  if (AttrProvided && (sattr.asked_attributes & FSAL_ATTR_SIZE)
                     && (sattr.filesize == 0))
                    {
                      sattr.asked_attributes = FSAL_ATTR_SIZE;
                    }
                  else
                    {
                      AttrProvided = FALSE;
                    }
                  PTHREAD_RWLOCK_WRLOCK(&pentry_lookup->state_lock);

                  pfile_state = find_existing_state(op, data, pentry_lookup);
                  ReuseState = pfile_state != NULL;

                  status4 = nfs4_chk_shrdny(op, data, pentry_lookup,
                      openflags, AttrProvided,
                      &sattr, resp);
                  if (status4 != NFS4_OK)
                    {
                      PTHREAD_RWLOCK_UNLOCK(&pentry_lookup->state_lock);
                      cause2 = " cache_inode_access";
                      res_OPEN4.status = status4;
                      cache_inode_put(pentry_lookup);
                      goto out;
                    }

                  status4 = nfs4_do_open(op, data, pentry_lookup, pentry_parent,
                      powner, &pfile_state, &filename, openflags, &text);

                  PTHREAD_RWLOCK_UNLOCK(&pentry_lookup->state_lock);

                  if (status4 != NFS4_OK)
                    {
                      cause2 = (const char *)text;
                      res_OPEN4.status = status4;
                      cache_inode_put(pentry_lookup);
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
                      cache_inode_put(pentry_lookup);
                      goto out;
                    }

                  /* Generate change info for after */
                  res_OPEN4.OPEN4res_u.resok4.cinfo.after
                       = cache_inode_get_changeid4(pentry_parent, data->pcontext);
                  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

                  /* No delegation */
                  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                      OPEN_DELEGATE_NONE;

                  /* If server use OPEN_CONFIRM4, set the correct flag */
                  P(powner->so_mutex);
                  if(powner->so_owner.so_nfs4_owner.so_confirmed == FALSE)
                    {
                        res_OPEN4.OPEN4res_u.resok4.rflags |=
                            OPEN4_RESULT_CONFIRM | OPEN4_RESULT_LOCKTYPE_POSIX;
                    }
                  else
                    {
                        res_OPEN4.OPEN4res_u.resok4.rflags |= OPEN4_RESULT_LOCKTYPE_POSIX ;
                    }
                  V(powner->so_mutex);

                  status4 = nfs4_create_fh(data, pentry_lookup, &text);
                  if(status4 != NFS4_OK)
                    {
                      cause2 = text;
                      res_OPEN4.status = status4;
                      goto out;
                    }

                  /* regular exit */
                  goto out_success;
                }

              /* if open is EXCLUSIVE, but verifier is the same,
                 return NFS4_OK (RFC3530 page 173) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
                {
                  if((pentry_lookup != NULL)
                     && (pentry_lookup->type == REGULAR_FILE))
                    {
                      /* Acquire lock to enter critical section on
                         this entry */
                      PTHREAD_RWLOCK_RDLOCK(&pentry_lookup->state_lock);

                      pfile_state = find_existing_state(op, data, pentry_lookup);
                      ReuseState = pfile_state != NULL;

                      if(pfile_state != NULL)
                        {
                          /* Check if open_owner is the same */
                          if(   !memcmp(pfile_state->state_data.share.
                                        share_oexcl_verifier,
                                        arg_OPEN4.openhow.openflag4_u.how.
                                        createhow4_u.createverf,
                                        NFS4_VERIFIER_SIZE))
                            {

                              /* A former open EXCLUSIVE with same
                                 owner and verifier was found, resend
                                 it */

                              /* Generate change info for after */
                              res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                                   cache_inode_get_changeid4(pentry_parent, data->pcontext);
                              res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

                              /* No delegation */
                              res_OPEN4.OPEN4res_u.resok4.delegation
                                   .delegation_type = OPEN_DELEGATE_NONE;

                              /* If server use OPEN_CONFIRM4, set the
                                 correct flag */
                              P(powner->so_mutex);
                              if(powner->so_owner.so_nfs4_owner.so_confirmed
                                 == FALSE)
                                {
                                  res_OPEN4.OPEN4res_u.resok4.rflags |=
                                    OPEN4_RESULT_CONFIRM |
                                    OPEN4_RESULT_LOCKTYPE_POSIX;
                                }
                              else
                                {
                                  res_OPEN4.OPEN4res_u.resok4.rflags |=
                                    OPEN4_RESULT_LOCKTYPE_POSIX;
                                }
                              V(powner->so_mutex);

                              status4 = nfs4_create_fh(data,
                                                       pentry_lookup,
                                                       &text);
                              if(status4 != NFS4_OK)
                                {
                                  cause2 = text;
                                  res_OPEN4.status = status4;
                                  PTHREAD_RWLOCK_UNLOCK(&pentry_lookup
                                                        ->state_lock);
                                  goto out;
                                }

                              /* regular exit */
                              PTHREAD_RWLOCK_UNLOCK(&pentry_lookup
                                                    ->state_lock);
                              goto out_success;
                            }
                        }
                      PTHREAD_RWLOCK_UNLOCK(&pentry_lookup->state_lock);
                    }
                }

              /* Managing GUARDED4 mode */
              res_OPEN4.status = NFS4ERR_EXIST; /* File already exists */
              cause2 = "GUARDED4";
              if(pentry_lookup != NULL)
                cache_inode_put(pentry_lookup);
              goto out;
            }

          LogFullDebug(COMPONENT_STATE,
                       "    OPEN open.how = %d",
                       arg_OPEN4.openhow.openflag4_u.how.mode);

          /* Create the file, if we reach this point, it does not
             exist, we can create it */
          if((pentry_newfile
              = cache_inode_create(pentry_parent,
                                   &filename,
                                   REGULAR_FILE,
                                   mode,
                                   NULL,
                                   NULL,
                                   data->pcontext,
                                   &cache_status)) == NULL)
            {
              LogFullDebug(COMPONENT_STATE,
                           "create failed with %s",
                           cache_inode_err_str(cache_status));

              /* If the file already exists, this is not an error if
                 open mode is UNCHECKED */
              if(cache_status != CACHE_INODE_ENTRY_EXISTS)
                {
                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = " UNCHECKED cache_inode_create";
                  goto out;
                }
              else
                {
                  /* If this point is reached, then something has
                     gone wrong.  The only way cache_inode_create()
                     returns NULL with CACHE_INODE_ENTRY_EXISTS
                     is if the entry->type != REGULAR_FILE. */
                  res_OPEN4.status = NFS4ERR_EXIST;
                  cause2 = " not a REGULAR file";
                  goto out;
                }
            }

          LogFullDebug(COMPONENT_STATE,
                       "create succeeded");

          cache_status = CACHE_INODE_SUCCESS;

          if(AttrProvided == TRUE)      /* Set the attribute if provided */
            {
              /* If owner or owner_group are set, and the credential was
               * squashed, then we must squash the set owner and owner_group.
               */
              squash_setattr(&data->export_perms,
                             &data->pworker->user_credentials,
                             &sattr);

              if((cache_status
                  = cache_inode_setattr(pentry_newfile,
                                        &sattr,
                                        data->pcontext,
                                        (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE) != 0,
                                        &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  LogFullDebug(COMPONENT_STATE,
                               "setattr failed with %s",
                               cache_inode_err_str(cache_status));

                  res_OPEN4.status = nfs4_Errno(cache_status);
                  cause2 = " cache_inode_setattr";
                  cache_inode_put(pentry_newfile);
                  goto out;
                }
            }

          PTHREAD_RWLOCK_WRLOCK(&pentry_newfile->state_lock);
          status4 = nfs4_do_open(op, data, pentry_newfile, pentry_parent,
              powner, &pfile_state, &filename, openflags, &text);
          PTHREAD_RWLOCK_UNLOCK(&pentry_newfile->state_lock);
          if (status4 != NFS4_OK)
            {
              LogFullDebug(COMPONENT_STATE,
                           "nfs4_do_open failed");
              cause2 = text;
              res_OPEN4.status = status4;
              cache_inode_put(pentry_newfile);
              goto out;
            }

          break;

        case OPEN4_NOCREATE:
          /* It was not a creation, but a regular open */
          cause = "OPEN4_NOCREATE";

          /* Does a file with this name already exist ? */
          if((pentry_newfile
              = cache_inode_lookup(pentry_parent,
                                   &filename,
                                   NULL,
                                   data->pcontext,
                                   &cache_status)) == NULL)
            {
               res_OPEN4.status = nfs4_Errno(cache_status);
               cause2 = " cache_inode_lookup";
               goto out;
            }

          /* OPEN4 is to be done on a file */
          if(pentry_newfile->type != REGULAR_FILE)
            {
              cache_inode_put(pentry_newfile);
              if(pentry_newfile->type == DIRECTORY)
                {
                  res_OPEN4.status = NFS4ERR_ISDIR;
                  goto out;
                }
              else
                {
                  res_OPEN4.status = NFS4ERR_SYMLINK;
                  goto out;
                }
            }

          status4 = nfs4_chk_shrdny(op, data, pentry_newfile,
              openflags, FALSE, NULL, resp);
          if (status4 != NFS4_OK)
            {
              cause2 = " cache_inode_access";
              res_OPEN4.status = status4;
              cache_inode_put(pentry_newfile);
              goto out;
            }

          PTHREAD_RWLOCK_WRLOCK(&pentry_newfile->state_lock);

          pfile_state = find_existing_state(op, data, pentry_newfile);
          ReuseState = pfile_state != NULL;

          status4 = nfs4_do_open(op, data, pentry_newfile, pentry_parent,
              powner, &pfile_state, &filename, openflags, &text);

          PTHREAD_RWLOCK_UNLOCK(&pentry_newfile->state_lock);

          if (status4 != NFS4_OK)
            {
              cause2 = text;
              res_OPEN4.status = status4;
              cache_inode_put(pentry_newfile);
              goto out;
            }
          break;

        default:
          cause = "INVALID OPEN TYPE";
          res_OPEN4.status = NFS4ERR_INVAL;
          goto out;
        }

      break;

    case CLAIM_PREVIOUS:
      powner->so_owner.so_nfs4_owner.so_confirmed = TRUE;

      /* pentry_parent is actually the file to be reclaimed, not the parent */
      pentry_newfile = pentry_parent;
      PTHREAD_RWLOCK_WRLOCK(&pentry_newfile->state_lock);
      status4 = nfs4_do_open(op, data, pentry_newfile, NULL, powner,
          &pfile_state, NULL, openflags, &text);
      PTHREAD_RWLOCK_UNLOCK(&pentry_newfile->state_lock);
      if (status4 != NFS4_OK)
        {
          cause2 = text;
          res_OPEN4.status = status4;
          goto out;
        }
      goto out_prev;

    case CLAIM_DELEGATE_PREV:
      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file_delegate_prev.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_NAMETOOLONG "
                   "for CLAIM_DELEGATE");
          dec_client_id_ref(pclientid);
          return res_OPEN4.status;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file_delegate_prev.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_INVAL for CLAIM_DELEGATE");
          dec_client_id_ref(pclientid);
          return res_OPEN4.status;
        }

      /* Fall through */

    case CLAIM_DELEGATE_CUR:
      res_OPEN4.status = NFS4ERR_NOTSUPP;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_NOTSUPP for CLAIM_DELEGATE");
      dec_client_id_ref(pclientid);
      return res_OPEN4.status;

    default:
      /* Invalid claim type */
      cause = "INVALID CLAIM";
      res_OPEN4.status = NFS4ERR_INVAL;
      goto out;
    }                           /*  switch(  arg_OPEN4.claim.claim ) */

  status4 = nfs4_create_fh(data, pentry_newfile, &text);
  if(status4 != NFS4_OK)
    {
      cause2 = text;
      res_OPEN4.status = status4;
      goto out;
    }

out_prev:

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

  /* Generate change info for after */
  res_OPEN4.OPEN4res_u.resok4.cinfo.after
       = cache_inode_get_changeid4(pentry_parent, data->pcontext);
  res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

  /* No delegation */
  res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type |= OPEN_DELEGATE_NONE;

  /* If server use OPEN_CONFIRM4, set the correct flag */
  if(powner->so_owner.so_nfs4_owner.so_confirmed == FALSE)
    {
      res_OPEN4.OPEN4res_u.resok4.rflags |=
        OPEN4_RESULT_CONFIRM | OPEN4_RESULT_LOCKTYPE_POSIX;
    }
  else
    {
      res_OPEN4.OPEN4res_u.resok4.rflags |= OPEN4_RESULT_LOCKTYPE_POSIX ;
    }

 out_success:

  LogFullDebug(COMPONENT_STATE, "NFS4 OPEN returning NFS4_OK");

  /* regular exit */
  res_OPEN4.status = NFS4_OK;

  /* Handle stateid/seqid for success */
  update_stateid(pfile_state,
                 &res_OPEN4.OPEN4res_u.resok4.stateid,
                 data,
                 tag);

 out:

  /* Save the response in the lock or open owner.
   * pentry_parent is either the parent directory or for a CLAIM_PREV is
   * the entry itself. In either case, it's the right entry to use in saving
   * the request results.
   */
  Copy_nfs4_state_req(powner, arg_OPEN4.seqid, op, pentry_parent, resp, tag);

 out1:

  /* Release the owner reference from create_nfs4_owner */
  dec_state_owner_ref(powner);

 out2:

  /* Update the lease before exit */
  P(pclientid->cid_mutex);

  update_lease(pclientid);

  V(pclientid->cid_mutex);

  dec_client_id_ref(pclientid);

 out3:
  if (pentry_parent)
    cache_inode_put(pentry_parent);

  if(res_OPEN4.status != NFS4_OK)
    {
      const char *cause3 = "", *cause4 = "";

      if(cache_status != CACHE_INODE_SUCCESS)
        {
          cause3 = " returned ";
          cause4 = cache_inode_err_str(cache_status);
        }

      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning %s for %s%s%s%s",
               nfsstat4_to_str(res_OPEN4.status),
               cause, cause2, cause3, cause4);

      /* Clean up if we have an error exit */
      if(pfile_state != NULL && !ReuseState)
        {
          /* Need to destroy open owner and state */
          if(state_del(pfile_state,
                       &state_status) != STATE_SUCCESS)
            LogDebug(COMPONENT_STATE,
                     "state_del failed with status %s",
                     state_err_str(state_status));
        }
    }

  return res_OPEN4.status;
}                               /* nfs4_op_open */

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
void nfs4_op_open_Free(OPEN4res * resp)
{
  if(resp->OPEN4res_u.resok4.attrset.bitmap4_val != NULL)
    gsh_free(resp->OPEN4res_u.resok4.attrset.bitmap4_val);
  resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
}                               /* nfs4_op_open_Free */

void nfs4_op_open_CopyRes(OPEN4res * resp_dst, OPEN4res * resp_src)
{
  if(resp_src->OPEN4res_u.resok4.attrset.bitmap4_val != NULL)
    {
      if((resp_dst->OPEN4res_u.resok4.attrset.bitmap4_val =
          gsh_calloc(resp_dst->OPEN4res_u.resok4.attrset.bitmap4_len,
                     sizeof(uint32_t))) == NULL)
        {
          resp_dst->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
        }
    }
}
int copy_bitmap4(bitmap4 *src, bitmap4 *dst)
{
        if ( dst->bitmap4_val != NULL ) {
       /* With multiple mallocs this is possible.
        * Should check everywhere but definitely in this multipurpose routine.
        * &ing maybe the proper response but fail it for now.
        */
                return(-1);    /* XXX */
        }
        if (( dst->bitmap4_val = gsh_calloc(src->bitmap4_len, sizeof(uint32_t)))
 == NULL) {
                return(-2);
        }
        memcpy((void *) dst->bitmap4_val, (void *) src->bitmap4_val,
                ((size_t) (src->bitmap4_len * sizeof(uint32_t))));
        dst->bitmap4_len = src->bitmap4_len;
        return(0);
}

nfsstat4
nfs4_chk_shrdny(struct nfs_argop4 *op, compound_data_t *data,
    cache_entry_t *pentry, fsal_accessflags_t rd_acc,
    bool_t AttrProvided, fsal_attrib_list_t *sattr, struct nfs_resop4 *resop)
{
        int rc = 0;
        OPEN4args *args = &op->nfs_argop4_u.opopen;
        OPEN4res *resp = &resop->nfs_resop4_u.opopen;
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        fsal_accessflags_t access_mask = 0;

        if(args->share_access & OPEN4_SHARE_ACCESS_READ) {
                access_mask |= FSAL_READ_ACCESS;
        }
        if(args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
                access_mask |= FSAL_WRITE_ACCESS;
        }

        if(cache_inode_access(pentry,
                              access_mask,
                              data->pcontext,
                              &cache_status) != CACHE_INODE_SUCCESS) {
                /* If non-permission error, return it.*/
                if(cache_status != CACHE_INODE_FSAL_EACCESS) {
                        LogDebug(COMPONENT_STATE,
                                 "cache_inode_access returned %s",
                                 cache_inode_err_str(cache_status));
                        return nfs4_Errno(cache_status);
                }

                /* If WRITE access is requested, return permission error */
                if(args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
                        LogDebug(COMPONENT_STATE,
                                 "cache_inode_access returned %s with ACCESS_WRITE",
                                 cache_inode_err_str(cache_status));
                        return nfs4_Errno(cache_status);
                }

                /* If just a permission error and file was opend read only,
                 * try execute permission.
                 */
                if(cache_inode_access(pentry,
                                      FSAL_MODE_MASK_SET(FSAL_X_OK) |
                                      FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE),
                                      data->pcontext,
                                      &cache_status) != CACHE_INODE_SUCCESS) {
                        LogDebug(COMPONENT_STATE,
                                 "cache_inode_access returned %s after checking for executer permission",
                                 cache_inode_err_str(cache_status));
                        return nfs4_Errno(cache_status);
                }
        }

        if(AttrProvided == TRUE) {      /* Set the attribute if provided */
                /* If owner or owner_group are set, and the credential was
                 * squashed, then we must squash the set owner and owner_group.
                 */
                squash_setattr(&data->export_perms,
                               &data->pworker->user_credentials,
                               sattr);

                if(cache_inode_setattr(pentry, sattr,
                    data->pcontext,
                    (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE) != 0,
                    &cache_status) != CACHE_INODE_SUCCESS) {
                        return nfs4_Errno(cache_status);
                }
                rc = copy_bitmap4(&args->openhow.openflag4_u.how.createhow4_u.createattrs.attrmask,
                        &resp->OPEN4res_u.resok4.attrset);
                if ( rc != 0 ) {
                        LogEvent(COMPONENT_STATE,
                                "copy_bitmap4 returned %d", rc);
                        return NFS4ERR_SERVERFAULT;
                }
        }
        else
                resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;

        return NFS4_OK;
}

static nfsstat4 nfs4_do_open(struct nfs_argop4  * op,
                             compound_data_t    * data,
                             cache_entry_t      * pentry_newfile,
                             cache_entry_t      * pentry_parent,
                             state_owner_t      * powner,
                             state_t           ** statep,
                             fsal_name_t        * filename,
                             fsal_openflags_t     openflags,
                             char              ** cause2)
{
        OPEN4args *args = &op->nfs_argop4_u.opopen;
        state_data_t candidate_data;
        state_type_t candidate_type;
        state_status_t state_status;
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
        int new_state = 0;

        /* Set the state for the related file */
        /* Prepare state management structure */
        candidate_type                         = STATE_TYPE_SHARE;
        candidate_data.share.share_access      = args->share_access;
        candidate_data.share.share_deny        = args->share_deny;
        candidate_data.share.share_access_prev = 0;
        candidate_data.share.share_deny_prev   = 0;

        /* Quick exit if there is any share conflict */
        if(state_share_check_conflict(pentry_newfile,
                                      candidate_data.share.share_access,
                                      candidate_data.share.share_deny,
                                      &state_status) != STATE_SUCCESS)
          {
            *cause2 = " (share conflict)";
            return nfs4_Errno_state(state_status);
          }

        if(*statep == NULL) {
                new_state = 1;

                /* If file is opened under mode EXCLUSIVE4, open verifier
                 * should be kept to detect non vicious double open */
                if(args->openhow.openflag4_u.how.mode == EXCLUSIVE4) {
                        memcpy(candidate_data.share.share_oexcl_verifier,
                               args->openhow.openflag4_u.how.createhow4_u.createverf,
                               NFS4_VERIFIER_SIZE);
                }

                if(state_add_impl(pentry_newfile, candidate_type, &candidate_data,
                    powner, data->pcontext, statep,
                    &state_status) != STATE_SUCCESS) {
                        *cause2 = STATE_ADD;
                        return nfs4_Errno_state(state_status);
                }

                init_glist(&((*statep)->state_data.share.share_lockstates));

                /* Attach this open to an export */
                (*statep)->state_pexport = data->pexport;
                P(data->pexport->exp_state_mutex);
                glist_add_tail(&data->pexport->exp_state_list,
                               &(*statep)->state_export_list);
                V(data->pexport->exp_state_mutex);
        } else {
          /* Check if open from another export */
          if((*statep)->state_pexport != data->pexport)
            {
              LogEvent(COMPONENT_STATE,
                       "Lock Owner Export Conflict, Lock held for export %d (%s), request for export %d (%s)",
                       (*statep)->state_pexport->id,
                       (*statep)->state_pexport->fullpath,
                       data->pexport->id,
                       data->pexport->fullpath);
              return STATE_INVALID_ARGUMENT;
            }
        }

        /* Open the file */
        if(cache_inode_open(pentry_newfile, openflags, data->pcontext,
                            0, &cache_status) != CACHE_INODE_SUCCESS) {
          *cause2 = CACHE_INODE_OPEN;
          return nfs4_Errno(cache_status);
        }

        /* Push share state to SAL (and FSAL) and update the union of file
         * share state.
         */
        if(new_state)
          {
            if(state_share_add(pentry_newfile, data->pcontext, powner, *statep,
                               &state_status) != STATE_SUCCESS)
              {
                state_status_t del_state_status;

                del_state_status = state_del_locked(*statep, pentry_newfile);
                if(del_state_status != STATE_SUCCESS)
                  LogDebug(COMPONENT_STATE,
                           "state_dell_locked failed with status %s",
                           state_err_str(del_state_status));
                *statep = NULL;

                if(cache_inode_close(pentry_newfile,
                                     data->pcontext,
                                     CACHE_INODE_FLAG_REALLYCLOSE, &cache_status) != CACHE_INODE_SUCCESS)
                  {
                    /* Log bad close and continue. */
                    LogEvent(COMPONENT_STATE, "Failed to close cache inode: status=%d",
                             cache_status);
                  }
                *cause2 = STATE_SHARE_ADD;
                return nfs4_Errno_state(state_status);
              }
          }
        else
          {
            /* If we find the previous share state, update share state. */
            if((candidate_type == STATE_TYPE_SHARE) &&
               ((*statep)->state_type == STATE_TYPE_SHARE))
              {
                LogFullDebug(COMPONENT_STATE, "Update existing share state");
                if(state_share_upgrade(pentry_newfile, data->pcontext,
                                       &candidate_data, powner, *statep,
                                       &state_status) != STATE_SUCCESS)
                  {
                    if(cache_inode_close(pentry_newfile,
                                         data->pcontext,
                                         0, &cache_status) != CACHE_INODE_SUCCESS)
                      {
                        /* Log bad close and continue. */
                        LogEvent(COMPONENT_STATE, "Failed to close cache inode: status=%d",
                                 cache_status);
                      }
                    LogEvent(COMPONENT_STATE, "Failed to update existing "
                                 "share state");
                    *cause2 = STATE_SHARE_UP;
                    return nfs4_Errno_state(state_status);
                  }
              }
          }

        return NFS4_OK;
}

static nfsstat4
nfs4_create_fh(compound_data_t *data, cache_entry_t *pentry, char **cause2)
{
        fsal_handle_t *pnewfsal_handle = NULL;
        nfs_fh4 newfh4;
        struct alloc_file_handle_v4 new_handle;

        newfh4.nfs_fh4_val = (caddr_t) &new_handle;
        newfh4.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);

        /* Now produce the filehandle to this file */
        pnewfsal_handle = &pentry->handle;

        /* Building a new fh */
        if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data)) {
                LogFullDebug(COMPONENT_FILEHANDLE,
                             "failed");
                *cause2 = " (nfs4_FSALToFhandle failed)";
                cache_inode_put(pentry);
                return NFS4ERR_SERVERFAULT;
        }

        /* This new fh replaces the current FH */
        data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
        memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
            newfh4.nfs_fh4_len);

        LogFullDebugOpaque(COMPONENT_FILEHANDLE,
                           "Create NFS4 Handle %s",
                           LEN_FH_STR,
                           data->currentFH.nfs_fh4_val,
                           data->currentFH.nfs_fh4_len);

        /* Decrement the current entry here, because nfs4_create_fh
           replaces the current fh. */
        if (data->current_entry) {
            cache_inode_put(data->current_entry);
        }

        /* Update stuff on compound data, do not have to call nfs4_SetCompoundExport
         * because the new file is on the same export, so data->pexport and
         * data->export_perms will not change.
         */
        data->current_entry = pentry;
        data->current_filetype = REGULAR_FILE;

        return NFS4_OK;
}

static state_t * find_existing_state(struct nfs_argop4 *op,
                                     compound_data_t *data,
                                     cache_entry_t *pentry_newfile)
{
  state_t           * pstate_iterate;
  struct glist_head * glist;

  /* Try to find if the same open_owner already has acquired a
   * stateid for this file
   */
  glist_for_each(glist, &pentry_newfile->state_list)
    {
      pstate_iterate = glist_entry(glist, state_t, state_list);

      // TODO FSF: currently only care about share types
      if(pstate_iterate->state_type != STATE_TYPE_SHARE)
        continue;

      if(isFullDebug(COMPONENT_STATE))
        {
          char                  str1[LOG_BUFF_LEN / 2];
          char                  str2[LOG_BUFF_LEN / 2];
          struct display_buffer dspbuf1 = {sizeof(str1), str1, str1};
          struct display_buffer dspbuf2 = {sizeof(str2), str2, str2};

          (void) display_owner(&dspbuf1, pstate_iterate->state_powner);
          (void) display_opaque_value(&dspbuf2,
                                      arg_OPEN4.owner.owner.owner_val,
                                      arg_OPEN4.owner.owner.owner_len);
          LogFullDebug(COMPONENT_STATE,
                       "Comparing state %p owner %s to open owner %s",
                       pstate_iterate, str1, str2);
        }

      /* Check is open_owner is the same */
      if((pstate_iterate->state_powner->so_owner.so_nfs4_owner
          .so_clientid == arg_OPEN4.owner.clientid) &&
         ((pstate_iterate->state_powner->so_owner_len ==
           arg_OPEN4.owner.owner.owner_len) &&
          (!memcmp(arg_OPEN4.owner.owner.owner_val,
                   pstate_iterate->state_powner->so_owner_val,
                   pstate_iterate->state_powner->so_owner_len))))
        {
          /* We'll be re-using the found state */
          LogFullDebug(COMPONENT_STATE,
                       "Returning state %p since owner is same",
                       pstate_iterate);
          return pstate_iterate;
        }
    }

  return NULL;
}
