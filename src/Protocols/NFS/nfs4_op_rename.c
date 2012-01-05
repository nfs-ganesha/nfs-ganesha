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
 * \file    nfs4_op_rename.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:51 $
 * \version $Revision: 1.15 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_rename.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs4_op_rename: The NFS4_OP_RENAME operation.
 * 
 * This functions handles the NFS4_OP_RENAME operation in NFSv4. This function can be called only from nfs4_Compound
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_RENAME4 op->nfs_argop4_u.oprename
#define res_RENAME4 resp->nfs_resop4_u.oprename

int nfs4_op_rename(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t *dst_entry;
  cache_entry_t *src_entry;
  cache_entry_t *tst_entry_dst;
  cache_entry_t *tst_entry_src;

  fsal_attrib_list_t attr_dst;
  fsal_attrib_list_t attr_src;
  fsal_attrib_list_t attr_tst_dst;
  fsal_attrib_list_t attr_tst_src;

  cache_inode_status_t cache_status;

  fsal_status_t fsal_status;

  fsal_handle_t *handlenew;
  fsal_handle_t *handleold;

  fsal_name_t oldname;
  fsal_name_t newname;

  nfsstat4 error;

  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_rename";

  resp->resop = NFS4_OP_RENAME;
  res_RENAME4.status = NFS4_OK;

  /* Read oldname and newname from uft8 strings, if one is empty then returns NFS4ERR_INVAL */
  if((arg_RENAME4.oldname.utf8string_len == 0)
     || (arg_RENAME4.newname.utf8string_len == 0))
    {
      res_RENAME4.status = NFS4ERR_INVAL;
      return NFS4ERR_INVAL;
    }

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_RENAME4.status = NFS4ERR_NOFILEHANDLE;
      return res_RENAME4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_RENAME4.status = NFS4ERR_BADHANDLE;
      return res_RENAME4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_RENAME4.status = NFS4ERR_FHEXPIRED;
      return res_RENAME4.status;
    }

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->savedFH)))
    {
      res_RENAME4.status = NFS4ERR_NOFILEHANDLE;
      return res_RENAME4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->savedFH)))
    {
      res_RENAME4.status = NFS4ERR_BADHANDLE;
      return res_RENAME4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->savedFH)))
    {
      res_RENAME4.status = NFS4ERR_FHEXPIRED;
      return res_RENAME4.status;
    }

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_RENAME4.status = NFS4ERR_ROFS;
      return res_RENAME4.status;
    }

  /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
  if(data->pexport == NULL)
    {
      if((error = nfs4_SetCompoundExport(data)) != NFS4_OK)
        {
          res_RENAME4.status = error;
          return res_RENAME4.status;
        }
    }

  /* Read oldname and newname from uft8 strings, if one is empty then returns NFS4ERR_INVAL */
  if((arg_RENAME4.oldname.utf8string_len > FSAL_MAX_NAME_LEN)
     || (arg_RENAME4.newname.utf8string_len > FSAL_MAX_NAME_LEN))
    {
      res_RENAME4.status = NFS4ERR_NAMETOOLONG;
      return NFS4ERR_INVAL;
    }

  /* get the names from the RPC input */
  if((cache_status =
      cache_inode_error_convert(FSAL_buffdesc2name
                                ((fsal_buffdesc_t *) & arg_RENAME4.oldname,
                                 &oldname))) != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = NFS4ERR_INVAL;
      return res_RENAME4.status;
    }

  if((cache_status =
      cache_inode_error_convert(FSAL_buffdesc2name
                                ((fsal_buffdesc_t *) & arg_RENAME4.newname,
                                 &newname))) != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = NFS4ERR_INVAL;
      return res_RENAME4.status;
    }

  /* Sanuty check: never rename to '.' or '..' */
  if(!FSAL_namecmp(&newname, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&newname, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_RENAME4.status = NFS4ERR_BADNAME;
      return res_RENAME4.status;
    }

  /* Sanuty check: never rename to '.' or '..' */
  if(!FSAL_namecmp(&oldname, (fsal_name_t *) & FSAL_DOT)
     || !FSAL_namecmp(&oldname, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      res_RENAME4.status = NFS4ERR_BADNAME;
      return res_RENAME4.status;
    }

  /*
   * This operation renames 
   *             - the object in directory pointed by savedFH, named arg_RENAME4.oldname
   *       into
   *             - an object in directory pointed by currentFH, named arg_RENAME4.newname
   *
   * Because of this, we will use 2 entry and we have verified both currentFH and savedFH */

  /* No Cross Device */
  if(((file_handle_v4_t *) (data->currentFH.nfs_fh4_val))->exportid !=
     ((file_handle_v4_t *) (data->savedFH.nfs_fh4_val))->exportid)
    {
      res_RENAME4.status = NFS4ERR_XDEV;
      return res_RENAME4.status;
    }

  /* destination must be a directory */
  dst_entry = data->current_entry;

  if(data->current_filetype != DIRECTORY)
    {
      res_RENAME4.status = NFS4ERR_NOTDIR;
      return res_RENAME4.status;
    }

  /* Convert saved FH into a vnode */
  src_entry = data->saved_entry;

  /* Source must be a directory */
  if(data->saved_filetype != DIRECTORY)
    {
      res_RENAME4.status = NFS4ERR_NOTDIR;
      return res_RENAME4.status;
    }

  /* Renaming a file to himself is allowed, returns NFS4_OK */
  if(src_entry == dst_entry)
    {
      if(!FSAL_namecmp(&oldname, &newname))
        {
          res_RENAME4.status = NFS4_OK;
          return res_RENAME4.status;
        }
    }

  /* For the change_info4, get the 'change' attributes for both directories */
  if((cache_status = cache_inode_getattr(src_entry,
                                         &attr_src,
                                         data->ht,
                                         data->pclient,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      return res_RENAME4.status;
    }
#ifdef BUGAZOMEU
  /* Ne devrait pas se produire dans le cas de exportid differents */

  /* Both object must resides on the same filesystem, return NFS4ERR_XDEV if not */
  if(attr_src.va_rdev != attr_dst.va_rdev)
    {
      res_RENAME4.status = NFS4ERR_XDEV;
      return res_RENAME4.status;
    }
#endif
  /* Lookup oldfile to see if it exists */
  if((tst_entry_src = cache_inode_lookup(src_entry,
                                         &oldname,
                                         data->pexport->cache_inode_policy,
                                         &attr_tst_src,
                                         data->ht,
                                         data->pclient,
                                         data->pcontext, &cache_status)) == NULL)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      return res_RENAME4.status;
    }

  /* Lookup file with new name to see if it already exists,
   * I expect to get NO_ERROR or ENOENT, anything else means an error */
  tst_entry_dst = cache_inode_lookup(dst_entry,
                                     &newname,
                                     data->pexport->cache_inode_policy,
                                     &attr_tst_dst,
                                     data->ht,
                                     data->pclient, data->pcontext, &cache_status);
  if((cache_status != CACHE_INODE_SUCCESS) && (cache_status != CACHE_INODE_NOT_FOUND))
    {
      /* Unexpected status at this step, exit with an error */
      res_RENAME4.status = nfs4_Errno(cache_status);
      return res_RENAME4.status;
    }

  if(cache_status == CACHE_INODE_NOT_FOUND)
    tst_entry_dst = NULL;       /* Just to make sure */

  /* Renaming a file to one of its own hardlink is allowed, return NFS4_OK */
  if(tst_entry_src == tst_entry_dst)
    {
      res_RENAME4.status = NFS4_OK;
      return res_RENAME4.status;
    }

  /* Renaming dir into existing file should return NFS4ERR_EXIST */
  if ((tst_entry_src->internal_md.type == DIRECTORY) &&
      ((tst_entry_dst != NULL) &&
       (tst_entry_dst-> internal_md.type == REGULAR_FILE)))
    {
      res_RENAME4.status = NFS4ERR_EXIST;
      return res_RENAME4.status;
    }

  /* Renaming file into existing dir should return NFS4ERR_EXIST */
  if(tst_entry_src->internal_md.type == REGULAR_FILE)
    {
      if(tst_entry_dst != NULL)
        {
	  if(tst_entry_dst->internal_md.type == DIRECTORY)
            {
              res_RENAME4.status = NFS4ERR_EXIST;
              return res_RENAME4.status;
            }
        }
    }

  /* Renaming dir1 into existing, nonempty dir2 should return NFS4ERR_EXIST
   * Renaming file into existing, nonempty dir should return NFS4ERR_EXIST */
  if(tst_entry_dst != NULL)
    {
	if((tst_entry_dst->internal_md.type == DIRECTORY)
	   && ((tst_entry_src->internal_md.type == DIRECTORY)
                || (tst_entry_src->internal_md.type == REGULAR_FILE)))
        {
          if(cache_inode_is_dir_empty_WithLock(tst_entry_dst) ==
	     CACHE_INODE_DIR_NOT_EMPTY)
            {
              res_RENAME4.status = NFS4ERR_EXIST;
              return res_RENAME4.status;
            }
        }
    }

  /* Client cache coherency information */
  memset(&(res_RENAME4.RENAME4res_u.resok4.source_cinfo.before), 0, sizeof(changeid4));
  memset(&(res_RENAME4.RENAME4res_u.resok4.source_cinfo.after), 0, sizeof(changeid4));

  res_RENAME4.RENAME4res_u.resok4.source_cinfo.before =
      (changeid4) src_entry->internal_md.mod_time;
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.before =
      (changeid4) dst_entry->internal_md.mod_time;

  if(cache_status == CACHE_INODE_SUCCESS)
    {
      /* New entry already exists, its attributes are in attr_tst_*, check for old entry to see if types are compatible */
      handlenew = cache_inode_get_fsal_handle(tst_entry_dst, &cache_status);
      if(cache_status != CACHE_INODE_SUCCESS)
        {
          /* Unexpected status at this step, exit with an error */
          res_RENAME4.status = nfs4_Errno(cache_status);
          return res_RENAME4.status;
        }

      handleold = cache_inode_get_fsal_handle(tst_entry_src, &cache_status);
      if(cache_status != CACHE_INODE_SUCCESS)
        {
          /* Unexpected status at this step, exit with an error */
          res_RENAME4.status = nfs4_Errno(cache_status);
          return res_RENAME4.status;
        }

      if(!FSAL_handlecmp(handlenew, handleold, &fsal_status))
        {
          /* For the change_info4, get the 'change' attributes for both directories */
          res_RENAME4.RENAME4res_u.resok4.source_cinfo.before =
              (changeid4) src_entry->internal_md.mod_time;
          res_RENAME4.RENAME4res_u.resok4.target_cinfo.before =
              (changeid4) dst_entry->internal_md.mod_time;
          res_RENAME4.RENAME4res_u.resok4.target_cinfo.atomic = TRUE;
          res_RENAME4.RENAME4res_u.resok4.source_cinfo.atomic = TRUE;

          res_RENAME4.status = NFS4_OK;
          return NFS4_OK;
        }
      else
        {
          /* Destination exists and is something different from source */
          if( tst_entry_src->internal_md.type == REGULAR_FILE &&
              tst_entry_dst->internal_md.type == REGULAR_FILE )
            {
              if(cache_inode_rename(src_entry,
                                    &oldname,
                                    dst_entry,
                                    &newname,
                                    &attr_src,
                                    &attr_dst,
                                    data->ht,
                                    data->pclient,
                                    data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
               {

                 res_RENAME4.status = nfs4_Errno(cache_status);
                 return res_RENAME4.status;
               }
            }
          else
            { 
              res_RENAME4.status = NFS4ERR_EXIST;
              return NFS4ERR_EXIST;
            }
        }
    }
  else
    {
      /* New entry does not already exist, call cache_entry_rename */
      if(cache_inode_rename(src_entry,
                            &oldname,
                            dst_entry,
                            &newname,
                            &attr_src,
                            &attr_dst,
                            data->ht,
                            data->pclient,
                            data->pcontext, &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_RENAME4.status = nfs4_Errno(cache_status);
          return res_RENAME4.status;
        }
    }

  /* If you reach this point, then everything was alright */
  /* For the change_info4, get the 'change' attributes for both directories */
  res_RENAME4.RENAME4res_u.resok4.source_cinfo.before =
      (changeid4) src_entry->internal_md.mod_time;
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.before =
      (changeid4) dst_entry->internal_md.mod_time;
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.atomic = TRUE;
  res_RENAME4.RENAME4res_u.resok4.source_cinfo.atomic = TRUE;
  res_RENAME4.status = nfs4_Errno(cache_status);

  return res_RENAME4.status;
}                               /* nfs4_op_rename */

/**
 * nfs4_op_rename_Free: frees what was allocared to handle nfs4_op_rename.
 * 
 * Frees what was allocared to handle nfs4_op_rename.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_rename_Free(RENAME4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_rename_Free */
