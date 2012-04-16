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
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"

static nfsstat4 nfs4_chk_shrdny(struct nfs_argop4 *, compound_data_t *,
    cache_entry_t *, fsal_accessflags_t, fsal_accessflags_t, fsal_openflags_t *,
    bool_t , fsal_attrib_list_t *, struct nfs_resop4 *);

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
  fsal_attrib_list_t        attr_parent;
  fsal_attrib_list_t        attr;
  fsal_attrib_list_t        attr_newfile;
  fsal_attrib_list_t        sattr;
  fsal_openflags_t          openflags = 0;
  cache_inode_status_t      cache_status = CACHE_INODE_SUCCESS;
  state_status_t            state_status;
  int                       retval;
  fsal_name_t               filename;
  bool_t                    AttrProvided = FALSE;
  bool_t                    ReuseState = FALSE;
  fsal_accessmode_t         mode = 0600;
  nfs_client_id_t         * nfs_clientid;
  nfs_worker_data_t       * pworker __attribute__((unused)) = NULL;
  state_t                 * pfile_state = NULL;
  state_t                 * pstate_iterate;
  state_nfs4_owner_name_t   owner_name;
  state_owner_t           * powner = NULL;
  const char              * tag = "OPEN";
  const char              * cause = "OOPS";
  const char              * cause2 = "";
  struct glist_head       * glist;
  open_claim_type4          claim = arg_OPEN4.claim.claim;
  nfsstat4                  status4;
  uint32_t                  tmp_attr[2];
  uint_t                    tmp_int = 2;
#ifdef _USE_QUOTA
  fsal_status_t            fsal_status;
#endif
  char                    * text = "";

  LogDebug(COMPONENT_STATE,
           "Entering NFS v4 OPEN handler -----------------------------------------------------");

  /* What kind of open is it ? */
  LogFullDebug(COMPONENT_STATE,
               "OPEN: Claim type = %d   Open Type = %d  Share Deny = %d   Share Access = %d ",
               arg_OPEN4.claim.claim,
               arg_OPEN4.openhow.opentype,
               arg_OPEN4.share_deny,
               arg_OPEN4.share_access);

  fsal_accessflags_t write_access = FSAL_WRITE_ACCESS;
  fsal_accessflags_t read_access = FSAL_READ_ACCESS;

  resp->resop = NFS4_OP_OPEN;
  res_OPEN4.status = NFS4_OK;
  res_OPEN4.OPEN4res_u.resok4.rflags = 0 ;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_NOFILEHANDLE;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_NOFILEHANDLE");
      return res_OPEN4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_BADHANDLE;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_BADHANDLE");
      return res_OPEN4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_FHEXPIRED;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_FHEXPIRED");
      return res_OPEN4.status;
    }

  /* This can't be done on the pseudofs */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_OPEN4.status = NFS4ERR_ROFS;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_ROFS");
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
          res_OPEN4.status = NFS4ERR_RESOURCE;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_RESOURCE after trying to repopulate cache");
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

  /* It this a known client id ? */
  LogDebug(COMPONENT_STATE,
           "OPEN Client id = %llx",
           (unsigned long long)arg_OPEN4.owner.clientid);
  if(nfs_client_id_Get_Pointer(arg_OPEN4.owner.clientid, &nfs_clientid) !=
      CLIENT_ID_SUCCESS)
    {
      res_OPEN4.status = NFS4ERR_STALE_CLIENTID;
      goto out2;
    }

  /* The client id should be confirmed */
  if(nfs_clientid->confirmed != CONFIRMED_CLIENT_ID)
    {
      res_OPEN4.status = NFS4ERR_STALE_CLIENTID;
      cause2 = " (not confirmed)";
      goto out2;
    }

  if (nfs4_is_lease_expired(nfs_clientid))
    {
      nfs_client_id_expire(nfs_clientid);
      res_OPEN4.status = NFS4ERR_EXPIRED;
      goto out2;
    }
  nfs4_update_lease(nfs_clientid);

  if (arg_OPEN4.openhow.opentype == OPEN4_CREATE && claim != CLAIM_NULL) {
      res_OPEN4.status = NFS4ERR_INVAL;
      cause2 = " (create without claim type null)";
      goto out2;
  }

  /* Is this open_owner known? If so, get it so we can use replay cache */
  convert_nfs4_open_owner(&arg_OPEN4.owner, &owner_name, 0LL);

  if(!nfs4_owner_Get_Pointer(&owner_name, &powner))
    {
      LogFullDebug(COMPONENT_STATE, "OPEN new owner");
    }
  else
    {
      if(isFullDebug(COMPONENT_STATE))
        {
          char str[HASHTABLE_DISPLAY_STRLEN];

          display_nfs4_owner(powner, str);

          LogFullDebug(COMPONENT_STATE,
                       "A previously known open_owner is used:%p %s arg_OPEN4.seqid=%u",
                       powner, str, arg_OPEN4.seqid);
        }

      if(arg_OPEN4.seqid == 0)
        {
          LogDebug(COMPONENT_STATE,
                   "Previously known open_owner is used with seqid=0, ask the client to confirm it again");
          powner->so_owner.so_nfs4_owner.so_confirmed = FALSE;
        }
      else
        {
          /* Check for replay */
          if(!Check_nfs4_seqid(powner, arg_OPEN4.seqid, op, data, resp, tag))
            {
              /* Response is setup for us and LogDebug told what was wrong */
              goto out2;
            }
        }
    }
  /* Is this open_owner known ? */
  if(powner == NULL)
    {
      /* This open owner is not known yet, allocated and set up a new one */
      powner = create_nfs4_owner(data->pclient,
                                 &owner_name,
                                 STATE_OPEN_OWNER_NFSV4,
                                 NULL,
                                 0);

      if(powner == NULL)
        {
          res_OPEN4.status = NFS4ERR_RESOURCE;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_RESOURCE for CLAIM_NULL (could not create NFS4 Owner");
          return res_OPEN4.status;
        }
      LogDebug(COMPONENT_STATE,
                       "NFS4 OPEN adding owners to client record ");


      P(nfs_clientid->clientid_mutex);
      glist_add_tail(&nfs_clientid->clientid_openowners, &powner->so_owner.so_nfs4_owner.so_perclient);
      V(nfs_clientid->clientid_mutex);
    }

  if (nfs_in_grace() && claim != CLAIM_PREVIOUS)
    {
       cause2 = " (in grace period)";
       res_OPEN4.status = NFS4ERR_GRACE;
       goto out;
    }
  if (nfs_in_grace() && claim == CLAIM_PREVIOUS &&
     nfs_clientid->allow_reclaim != 1)
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

  /* First switch is based upon claim type */
  switch (claim)
    {
    case CLAIM_NULL:
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
      pentry_parent = data->current_entry;

      /* Parent must be a directory */
      if((pentry_parent->internal_md.type != DIRECTORY))
        {
          /* Parent object is not a directory... */
          if(pentry_parent->internal_md.type == SYMBOLIC_LINK)
            res_OPEN4.status = NFS4ERR_SYMLINK;
          else
            res_OPEN4.status = NFS4ERR_NOTDIR;

          cause2 = " (parent not directory)";
          goto out;
        }

      /* Status of parent directory before the operation */
      if(cache_inode_getattr(pentry_parent,
                             &attr_parent,
                             data->ht,
                             data->pclient,
                             data->pcontext,
                             &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_OPEN4.status = nfs4_Errno(cache_status);
          cause2 = " cache_inode_getattr";
          goto out;
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
          fsal_status = FSAL_check_quota( data->pexport->fullpath, 
                                          FSAL_QUOTA_INODES,
                                          FSAL_OP_CONTEXT_TO_UID( data->pcontext ) ) ;
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
          pentry_lookup = cache_inode_lookup(pentry_parent,
                                             &filename,
                                             data->pexport->cache_inode_policy,
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
                  status4 = nfs4_chk_shrdny(op, data, pentry_lookup,
                      read_access, write_access, &openflags, AttrProvided,
                      &sattr, resp);
                  if (status4 != NFS4_OK)
                    {
                      cause2 = " cache_inode_access";
                      res_OPEN4.status = status4;
                      goto out;
                    }

                  status4 = nfs4_do_open(op, data, pentry_lookup, pentry_parent,
                      powner, &pfile_state, &filename, openflags, &text);
                  if (status4 != NFS4_OK)
                    {
                      cause2 = (const char *)text;
                      res_OPEN4.status = status4;
                      goto out;
                    }

                  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 3;
                  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
                      (uint32_t *) Mem_Alloc(res_OPEN4.OPEN4res_u.resok4.attrset.
                                             bitmap4_len * sizeof(uint32_t))) == NULL)
                    {
                      res_OPEN4.status = NFS4ERR_RESOURCE;
                      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0;
                      cause2 = " (Mem_Alloc of bitmap failed)";
                      goto out;
                    }

                  memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.after), 0,
                         sizeof(changeid4));
                  res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                      (changeid4) pentry_parent->internal_md.mod_time;
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

              /* if open is EXCLUSIVE, but verifier is the same, return NFS4_OK (RFC3530 page 173) */
              if(arg_OPEN4.openhow.openflag4_u.how.mode == EXCLUSIVE4)
                {
                  if((pentry_lookup != NULL)
                     && (pentry_lookup->internal_md.type == REGULAR_FILE))
                    {
                      /* Acquire lock to enter critical section on this entry */
                      P_r(&pentry_lookup->lock);

                      glist_for_each(glist, &pentry_lookup->object.file.state_list)
                        {
                          pstate_iterate = glist_entry(glist, state_t, state_list);

                          /* Check is open_owner is the same */
                          if((pstate_iterate->state_type == STATE_TYPE_SHARE)
                             && !memcmp(arg_OPEN4.owner.owner.owner_val,
                                        pstate_iterate->state_powner->so_owner_val,
                                        pstate_iterate->state_powner->so_owner_len)
                             && !memcmp(pstate_iterate->state_data.share.
                                        share_oexcl_verifier,
                                        arg_OPEN4.openhow.openflag4_u.how.
                                        createhow4_u.createverf, NFS4_VERIFIER_SIZE))
                            {

                              /* A former open EXCLUSIVE with same owner and verifier was found, resend it */

                              // res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0 ; /* No attributes set */
                              //if( ( res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val = 
                              //    (uint32_t *)Mem_Alloc( res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len  * sizeof( uint32_t ) ) ) == NULL )
                              // {
                              //          res_OPEN4.status = NFS4ERR_SERVERFAULT ;
                              //          return res_OPEN4.status ;
                              //       }

                              memset(&(res_OPEN4.OPEN4res_u.resok4.cinfo.after), 0,
                                     sizeof(changeid4));
                              res_OPEN4.OPEN4res_u.resok4.cinfo.after =
                                  (changeid4) pentry_parent->internal_md.mod_time;
                              res_OPEN4.OPEN4res_u.resok4.cinfo.atomic = FALSE;

                              /* No delegation */
                              res_OPEN4.OPEN4res_u.resok4.delegation.delegation_type =
                                  OPEN_DELEGATE_NONE;

                              /* If server use OPEN_CONFIRM4, set the correct flag */
                              P(powner->so_mutex);
                              if(powner->so_owner.so_nfs4_owner.so_confirmed == FALSE)
                                {
                                    res_OPEN4.OPEN4res_u.resok4.rflags |= OPEN4_RESULT_CONFIRM | OPEN4_RESULT_LOCKTYPE_POSIX;
                                }
                              else
                                {
                                    res_OPEN4.OPEN4res_u.resok4.rflags |= OPEN4_RESULT_LOCKTYPE_POSIX;
                                }
                              V(powner->so_mutex);

                              status4 = nfs4_create_fh(data, pentry_lookup,
                                  &text);
                              if(status4 != NFS4_OK)
                                {
                                  cause2 = text;
                                  V_r(&pentry_lookup->lock);
                                  res_OPEN4.status = status4;
                                  goto out;
                                }

                              /* regular exit */
                              V_r(&pentry_lookup->lock);
                              ReuseState  = TRUE;
                              pfile_state = pstate_iterate;
                              goto out_success;
                            }
                        }

                      V_r(&pentry_lookup->lock);
                    }
                }

              /* Managing GUARDED4 mode */
              if(cache_status != CACHE_INODE_SUCCESS)
                res_OPEN4.status = nfs4_Errno(cache_status);
              else
                res_OPEN4.status = NFS4ERR_EXIST;      /* File already exists */

              cause2 = "GUARDED4";
              goto out;
            }

          /*  if( cache_status != CACHE_INODE_NOT_FOUND ), if file already exists basically */
          LogFullDebug(COMPONENT_STATE,
                       "    OPEN open.how = %d",
                       arg_OPEN4.openhow.openflag4_u.how.mode);

          /* Create the file, if we reach this point, it does not exist, we can create it */
          if((pentry_newfile = cache_inode_create(pentry_parent,
                                                  &filename,
                                                  REGULAR_FILE,
                                                  data->pexport->cache_inode_policy,
                                                  mode,
                                                  NULL,
                                                  &attr_newfile,
                                                  data->ht,
                                                  data->pclient,
                                                  data->pcontext, &cache_status)) == NULL)
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
                  /* If this point is reached, then the file already exists, cache_status == CACHE_INODE_ENTRY_EXISTS and pentry_newfile == NULL 
                     This probably means EXCLUSIVE4 mode is used and verifier matches. pentry_newfile is then set to pentry_lookup */
                  pentry_newfile = pentry_lookup;
                }
            }

          cache_status = CACHE_INODE_SUCCESS;

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

          status4 = nfs4_do_open(op, data, pentry_newfile, pentry_parent,
              powner, &pfile_state, &filename, openflags, &text);
          if (status4 != NFS4_OK)
            {
              cause2 = text;
              res_OPEN4.status = status4;
              goto out;
            }

          break;

        case OPEN4_NOCREATE:
          /* It was not a creation, but a regular open */
          cause = "OPEN4_NOCREATE";

          /* Does a file with this name already exist ? */
          if ((pentry_newfile = cache_inode_lookup(pentry_parent,
                                             &filename,
                                             data->pexport->cache_inode_policy,
                                             &attr_newfile,
                                             data->ht,
                                             data->pclient,
                                             data->pcontext,
                                             &cache_status)) == NULL)
            {
              res_OPEN4.status = nfs4_Errno(cache_status);
              cause2 = " cache_inode_lookup";
              goto out;
            }

          /* OPEN4 is to be done on a file */
          if(pentry_newfile->internal_md.type != REGULAR_FILE)
            {
              if(pentry_newfile->internal_md.type == DIRECTORY)
                {
                  res_OPEN4.status = NFS4ERR_ISDIR;
                  goto out;
                }
              else if(pentry_newfile->internal_md.type == SYMBOLIC_LINK)
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

          status4 = nfs4_chk_shrdny(op, data, pentry_newfile, read_access,
              write_access, &openflags, FALSE, NULL, resp);
          if (status4 != NFS4_OK)
            {
              cause2 = " cache_inode_access";
              res_OPEN4.status = status4;
              goto out;
            }

#ifdef WITH_MODE_0_CHECK
          /* If file mode is 000 then NFS4ERR_ACCESS should be returned for all cases and users */
          if(attr_newfile.mode == 0)
            {
              res_OPEN4.status = NFS4ERR_ACCESS;
              cause2 = " (mode is 0)";
              goto out;
            }
#endif

          /* Acquire lock to enter critical section on this entry */
          P_r(&pentry_newfile->lock);

          /* Try to find if the same open_owner already has acquired a stateid for this file */
          glist_for_each(glist, &pentry_newfile->object.file.state_list)
            {
              pstate_iterate = glist_entry(glist, state_t, state_list);

              // TODO FSF: currently only care about share types
              if(pstate_iterate->state_type != STATE_TYPE_SHARE)
                continue;

              /* Check is open_owner is the same */
              if((pstate_iterate->state_powner->so_owner.so_nfs4_owner.so_clientid == arg_OPEN4.owner.clientid) &&
                 ((pstate_iterate->state_powner->so_owner_len == arg_OPEN4.owner.owner.owner_len) &&
                  (!memcmp(arg_OPEN4.owner.owner.owner_val,
                           pstate_iterate->state_powner->so_owner_val,
                           pstate_iterate->state_powner->so_owner_len))))
                {
                  /* We'll be re-using the found state */
                  pfile_state = pstate_iterate;
                  ReuseState  = TRUE;
                }
              else
                {
                  /* This is a different owner, check for possible conflicts */
                  if((pstate_iterate->state_data.share.share_access & OPEN4_SHARE_ACCESS_WRITE)
                     && (arg_OPEN4.share_deny & OPEN4_SHARE_DENY_WRITE))
                    {
                      V_r(&pentry_newfile->lock);
                      res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                      cause2 = " (OPEN4_SHARE_DENY_WRITE)";
                      goto out;
                    }
                }

              /* In all cases opening in read access a read denied file or write access to a write denied file 
               * should fail, even if the owner is the same, see discussion in 14.2.16 and 8.9
               */

              /* deny read access on read denied file */
              if((pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_READ)
                 && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ))
                {
                  V_r(&pentry_newfile->lock);
                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                  cause2 = " (OPEN4_SHARE_ACCESS_READ)";
                  goto out;
                }

              /* deny write access on write denied file */
              if((pstate_iterate->state_data.share.share_deny & OPEN4_SHARE_DENY_WRITE)
                 && (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE))
                {
                  V_r(&pentry_newfile->lock);
                  res_OPEN4.status = NFS4ERR_SHARE_DENIED;
                  cause2 = " (OPEN4_SHARE_ACCESS_WRITE)";
                  goto out;
                }
            }

          V_r(&pentry_newfile->lock);

          status4 = nfs4_do_open(op, data, pentry_newfile, pentry_parent,
              powner, &pfile_state, &filename, openflags, &text);
          if (status4 != NFS4_OK)
            {
              cause2 = text;
              res_OPEN4.status = status4;
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
      powner->so_owner.so_nfs4_owner.so_confirmed = TRUE;

      /* Set the openflags variable */
      if((arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ) &&
          (arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_WRITE))
        openflags = FSAL_O_RDWR;
      else if(arg_OPEN4.share_access & OPEN4_SHARE_ACCESS_READ)
        openflags |= FSAL_O_RDONLY;
      else
        openflags |= FSAL_O_WRONLY;

      /* pentry_parent is actually the file to be reclaimed, not the parent */
      pentry_newfile = pentry_parent;
      status4 = nfs4_do_open(op, data, pentry_newfile, NULL, powner,
          &pfile_state, NULL, openflags, &text);
      if (status4 != NFS4_OK)
        {
          cause2 = text;
          res_OPEN4.status = status4;
          goto out;
        }
      goto out_prev;

    case CLAIM_DELEGATE_CUR:
    case CLAIM_DELEGATE_PREV:
      /* Check for name length */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len > FSAL_MAX_NAME_LEN)
        {
          res_OPEN4.status = NFS4ERR_NAMETOOLONG;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_NAMETOOLONG for CLAIM_DELEGATE");
          return res_OPEN4.status;
        }

      /* get the filename from the argument, it should not be empty */
      if(arg_OPEN4.claim.open_claim4_u.file.utf8string_len == 0)
        {
          res_OPEN4.status = NFS4ERR_INVAL;
          LogDebug(COMPONENT_STATE,
                   "NFS4 OPEN returning NFS4ERR_INVAL for CLAIM_DELEGATE");
          return res_OPEN4.status;
        }

      res_OPEN4.status = NFS4ERR_NOTSUPP;
      LogDebug(COMPONENT_STATE,
               "NFS4 OPEN returning NFS4ERR_NOTSUPP for CLAIM_DELEGATE");
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

  /* Status of parent directory after the operation */
  if((cache_status = cache_inode_getattr(pentry_parent,
                                         &attr_parent,
                                         data->ht,
                                         data->pclient,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_OPEN4.status = nfs4_Errno(cache_status);
      cause2 = " cache_inode_getattr";
      goto out;
    }

out_prev:

  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 3;
  if((res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val =
      (uint32_t *) Mem_Alloc(res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len *
                             sizeof(uint32_t))) == NULL)
    {
      res_OPEN4.status = NFS4ERR_SERVERFAULT;
      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 0;
      cause2 = " (Mem_Alloc attr failed)";
      goto out;
    }

  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val[0] = 0;       /* No Attributes set */
  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val[1] = 0;       /* No Attributes set */
  res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_val[2] = 0;       /* No Attributes set */

  if(arg_OPEN4.openhow.opentype == OPEN4_CREATE)
    {
      tmp_int = 2;
      tmp_attr[0] = FATTR4_SIZE;
      tmp_attr[1] = FATTR4_MODE;
      nfs4_list_to_bitmap4(&(res_OPEN4.OPEN4res_u.resok4.attrset), &tmp_int, tmp_attr);
      res_OPEN4.OPEN4res_u.resok4.attrset.bitmap4_len = 3;
    }

  res_OPEN4.OPEN4res_u.resok4.cinfo.after =
      (changeid4) pentry_parent->internal_md.mod_time;
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

  nfs4_update_lease(nfs_clientid);

  /* If we are re-using stateid, then release extra reference to open owner */
  if(ReuseState)
    dec_state_owner_ref(powner, data->pclient);

 out:

  /* Save the response in the lock or open owner */
  Copy_nfs4_state_req(powner, arg_OPEN4.seqid, op, data, resp, tag);

 out2:

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
                       data->pclient,
                       &state_status) != STATE_SUCCESS)
            LogDebug(COMPONENT_NFS_V4_LOCK,
                     "state_del failed with status %s",
                     state_err_str(state_status));
        }
      else if(powner != NULL)
        {
          /* Need to release the open owner */
          dec_state_owner_ref(powner, data->pclient);
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
    Mem_Free(resp->OPEN4res_u.resok4.attrset.bitmap4_val);
  resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
}                               /* nfs4_op_open_Free */

void nfs4_op_open_CopyRes(OPEN4res * resp_dst, OPEN4res * resp_src)
{
  if(resp_src->OPEN4res_u.resok4.attrset.bitmap4_val != NULL)
    {
      if((resp_dst->OPEN4res_u.resok4.attrset.bitmap4_val =
          (uint32_t *) Mem_Alloc(resp_dst->OPEN4res_u.resok4.attrset.bitmap4_len *
                                 sizeof(uint32_t))) == NULL)
        {
          resp_dst->OPEN4res_u.resok4.attrset.bitmap4_len = 0;
        }
    }
}

static nfsstat4
nfs4_chk_shrdny(struct nfs_argop4 *op, compound_data_t *data,
    cache_entry_t *pentry, fsal_accessflags_t rd_acc,
    fsal_accessflags_t wr_acc, fsal_openflags_t *openflags,
    bool_t AttrProvided, fsal_attrib_list_t *sattr, struct nfs_resop4 *resop)
{
        OPEN4args *args = &op->nfs_argop4_u.opopen;
        OPEN4res *resp = &resop->nfs_resop4_u.opopen;
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

        /* If the file is opened for write, OPEN4 while deny share write access,
         * in this case, check caller has write access to the file */
        if(args->share_deny & OPEN4_SHARE_DENY_WRITE) {
                if(cache_inode_access(pentry, wr_acc, data->ht, data->pclient,
                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS) {
                        return NFS4ERR_ACCESS;
                }
                *openflags = FSAL_O_WRONLY;
        }

        /* Same check on read: check for readability of a file before opening
         * it for read */
        if(args->share_access & OPEN4_SHARE_ACCESS_READ) {
                if(cache_inode_access(pentry, rd_acc, data->ht, data->pclient,
                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS) {
                        return NFS4ERR_ACCESS;
                }
                *openflags = FSAL_O_RDONLY;
        }

        if(AttrProvided == TRUE) {      /* Set the attribute if provided */
                if(cache_inode_setattr(pentry, sattr, data->ht, data->pclient,
                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS) {
                        return nfs4_Errno(cache_status);
                }

                resp->OPEN4res_u.resok4.attrset =
                    args->openhow.openflag4_u.how.createhow4_u.createattrs.
                    attrmask;
        }
        else
                resp->OPEN4res_u.resok4.attrset.bitmap4_len = 0;

        /* Same check on write */
        if(args->share_access & OPEN4_SHARE_ACCESS_WRITE) {
                if(cache_inode_access(pentry, wr_acc, data->ht, data->pclient,
                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS) {
                          return NFS4ERR_ACCESS;
                }
                *openflags = FSAL_O_RDWR;
        }

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

        if(*statep == NULL) {
                /* Set the state for the related file */
                /* Prepare state management structure */
                candidate_type                    = STATE_TYPE_SHARE;
                candidate_data.share.share_deny   = args->share_deny;
                candidate_data.share.share_access = args->share_access;

                /* If file is opened under mode EXCLUSIVE4, open verifier
                 * should be kept to detect non vicious double open */
                if(args->openhow.openflag4_u.how.mode == EXCLUSIVE4) {
                        strncpy(candidate_data.share.share_oexcl_verifier,
                          args->openhow.openflag4_u.how.createhow4_u.createverf,
                          NFS4_VERIFIER_SIZE);
                }

                if(state_add(pentry_newfile, candidate_type, &candidate_data,
                    powner, data->pclient, data->pcontext, statep,
                    &state_status) != STATE_SUCCESS) {
                        *cause2 = STATE_ADD;
                        return NFS4ERR_SHARE_DENIED;
                }

                init_glist(&((*statep)->state_data.share.share_lockstates));

                /* Attach this open to an export */
                (*statep)->state_pexport = data->pexport;
                P(data->pexport->exp_state_mutex);
                glist_add_tail(&data->pexport->exp_state_list, &(*statep)->state_export_list);
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

        if (pentry_parent != NULL) {    /* claim null */
                /* Open the file */
                if(cache_inode_open_by_name(pentry_parent, filename,
                    pentry_newfile, data->pclient, openflags, data->pcontext,
                    &cache_status) != CACHE_INODE_SUCCESS) {
                        *cause2 = " cache_inode_open_by_name";
                        return NFS4ERR_ACCESS;
                }
        } else { /* claim previous */
                if (cache_inode_open(pentry_newfile, data->pclient, openflags,
                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS) {
                        *cause2 = CACHE_INODE_OPEN;
                        return nfs4_Errno(cache_status);
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
        cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;

        newfh4.nfs_fh4_val = (caddr_t) &new_handle;
	newfh4.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);

        /* Now produce the filehandle to this file */
        if((pnewfsal_handle =
            cache_inode_get_fsal_handle(pentry, &cache_status)) == NULL) {
                *cause2 = " cache_inode_get_fsal_handle";
                return nfs4_Errno(cache_status);
        }

        /* Building a new fh */
        if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data)) {
                *cause2 = " (nfs4_FSALToFhandle failed)";
                return NFS4ERR_SERVERFAULT;
        }

        /* This new fh replaces the current FH */
        data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
        memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val,
            newfh4.nfs_fh4_len);

        data->current_entry = pentry;
        data->current_filetype = REGULAR_FILE;

        return NFS4_OK;
}
