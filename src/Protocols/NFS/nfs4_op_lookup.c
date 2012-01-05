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
 * \file    nfs4_op_lookup.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.18 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_lookup.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_proto_tools.h"

/**
 * nfs4_op_lookup: looks up into theFSAL.
 * 
 * looks up into the FSAL. If a junction is crossed, does what is necessary.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_name_t name;
  char strname[MAXNAMLEN];
#ifndef _NO_XATTRD
  char objname[MAXNAMLEN];
#endif
  unsigned int xattr_found = FALSE;
  cache_entry_t *dir_pentry = NULL;
  cache_entry_t *file_pentry = NULL;
  fsal_attrib_list_t attrlookup;
  cache_inode_status_t cache_status;

  fsal_handle_t *pfsal_handle = NULL;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_lookup";

  resp->resop = NFS4_OP_LOOKUP;
  res_LOOKUP4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_LOOKUP4.status = NFS4ERR_NOFILEHANDLE;
      return res_LOOKUP4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_LOOKUP4.status = NFS4ERR_BADHANDLE;
      return res_LOOKUP4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_LOOKUP4.status = NFS4ERR_FHEXPIRED;
      return res_LOOKUP4.status;
    }

  /* Check for empty name */
  if(op->nfs_argop4_u.oplookup.objname.utf8string_len == 0 ||
     op->nfs_argop4_u.oplookup.objname.utf8string_val == NULL)
    {
      res_LOOKUP4.status = NFS4ERR_INVAL;
      return res_LOOKUP4.status;
    }

  /* Check for name to long */
  if(op->nfs_argop4_u.oplookup.objname.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_LOOKUP4.status = NFS4ERR_NAMETOOLONG;
      return res_LOOKUP4.status;
    }

  /* If Filehandle points to a pseudo fs entry, manage it via pseudofs specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_lookup_pseudo(op, data, resp);

#ifndef _NO_XATTRD
  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_lookup_xattr(op, data, resp);
#endif

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(strname, sizeof(strname), &arg_LOOKUP4.objname);

#ifndef _NO_XATTRD
  /* Is this a .xattr.d.<object> name ? */
  if(nfs_XattrD_Name(strname, objname))
    {
      strcpy(strname, objname);
      xattr_found = TRUE;
    }
#endif

  if((cache_status = cache_inode_error_convert(FSAL_str2name(strname,
                                                             MAXNAMLEN,
                                                             &name))) !=
     CACHE_INODE_SUCCESS)
    {
      res_LOOKUP4.status = nfs4_Errno(cache_status);
      return res_LOOKUP4.status;
    }

  /* No 'cd .' is allowed return NFS4ERR_BADNAME in this case */
  /* No 'cd .. is allowed, return EINVAL in this case. NFS4_OP_LOOKUPP should be use instead */
  if(!FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_LOOKUP4.status = NFS4ERR_BADNAME;
      return res_LOOKUP4.status;
    }

  /* Do the lookup in the HPSS Namespace */
  file_pentry = NULL;
  dir_pentry = data->current_entry;

  /* Sanity check: dir_pentry should be ACTUALLY a directory */
  if(dir_pentry->internal_md.type != DIRECTORY)
    {
      /* This is not a directory */
      if(dir_pentry->internal_md.type == SYMBOLIC_LINK)
        res_LOOKUP4.status = NFS4ERR_SYMLINK;
      else
        res_LOOKUP4.status = NFS4ERR_NOTDIR;

      /* Return failed status */
      return res_LOOKUP4.status;
    }

  /* BUGAZOMEU: Faire la gestion des cross junction traverse */
  if((file_pentry = cache_inode_lookup(dir_pentry,
                                       &name,
                                       data->pexport->cache_inode_policy,
                                       &attrlookup,
                                       data->ht,
                                       data->pclient,
                                       data->pcontext, &cache_status)) != NULL)
    {
      /* Extract the fsal attributes from the cache inode pentry */
      pfsal_handle = cache_inode_get_fsal_handle(file_pentry, &cache_status);

      if(cache_status != CACHE_INODE_SUCCESS)
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Convert it to a file handle */
      if(!nfs4_FSALToFhandle(&data->currentFH, pfsal_handle, data))
        {
          res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
          return res_LOOKUP4.status;
        }

      /* Copy this to the mounted on FH (if no junction is traversed */
      memcpy((char *)(data->mounted_on_FH.nfs_fh4_val),
             (char *)(data->currentFH.nfs_fh4_val), data->currentFH.nfs_fh4_len);
      data->mounted_on_FH.nfs_fh4_len = data->currentFH.nfs_fh4_len;

#if 0
      print_buff((char *)cache_inode_get_fsal_handle(file_pentry, &cache_status),
                 sizeof(fsal_handle_t));
      print_buff((char *)cache_inode_get_fsal_handle(dir_pentry, &cache_status),
                 sizeof(fsal_handle_t));
#endif
      if(isFullDebug(COMPONENT_NFS_V4))
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "----> nfs4_op_lookup: name=%s  dir_pentry=%p  looked up pentry=%p",
                       strname, dir_pentry, file_pentry);
          LogFullDebug(COMPONENT_NFS_V4,
                       "----> FSAL handle parent puis fils dans nfs4_op_lookup");
          print_buff(COMPONENT_NFS_V4,
                     (char *)cache_inode_get_fsal_handle(file_pentry, &cache_status),
                     sizeof(fsal_handle_t));
          print_buff(COMPONENT_NFS_V4,
                     (char *)cache_inode_get_fsal_handle(dir_pentry, &cache_status),
                     sizeof(fsal_handle_t));
        }
      LogHandleNFS4("NFS4 LOOKUP CURRENT FH: ", &data->currentFH);

      /* Keep the pointer within the compound data */
      data->current_entry = file_pentry;
      data->current_filetype = file_pentry->internal_md.type;

      /* Return successfully */
      res_LOOKUP4.status = NFS4_OK;

#ifndef _NO_XATTRD
      /* If this is a xattr ghost directory name, update the FH */
      if(xattr_found == TRUE)
        res_LOOKUP4.status = nfs4_fh_to_xattrfh(&(data->currentFH), &(data->currentFH));
#endif

      if((data->current_entry->internal_md.type == DIRECTORY) &&
         (data->current_entry->object.dir.referral != NULL))
        {
          if(!nfs4_Set_Fh_Referral(&(data->currentFH)))
            {
              res_LOOKUP4.status = NFS4ERR_SERVERFAULT;
              return res_LOOKUP4.status;
            }
        }

      return NFS4_OK;

    }

  /* If the part of the code is reached, then something wrong occured in the lookup process, status is not HPSS_E_NOERROR 
   * and contains the code for the error */

  res_LOOKUP4.status = nfs4_Errno(cache_status);

  return res_LOOKUP4.status;
}                               /* nfs4_op_lookup */

/**
 * nfs4_op_lookup_Free: frees what was allocared to handle nfs4_op_lookup.
 * 
 * Frees what was allocared to handle nfs4_op_lookup.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_lookup_Free(LOOKUP4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_lookup_Free */
