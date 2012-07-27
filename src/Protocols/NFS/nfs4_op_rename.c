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
 * @file    nfs4_op_rename.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "log.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "sal_functions.h"

/**
 * @brief The NFS4_OP_RENAME operation
 *
 * This function implemenats the NFS4_OP_RENAME operation. This
 * function can be called only from nfs4_Compound
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 373
 */

#define arg_RENAME4 op->nfs_argop4_u.oprename
#define res_RENAME4 resp->nfs_resop4_u.oprename

int nfs4_op_rename(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  cache_entry_t        * dst_entry = NULL;
  cache_entry_t        * src_entry = NULL;
  cache_entry_t        * tst_entry_dst = NULL;
  cache_entry_t        * tst_entry_src = NULL;
  struct attrlist        attr_dst;
  struct attrlist        attr_src;
  struct attrlist        attr_tst_dst;
  struct attrlist        attr_tst_src;
  cache_inode_status_t   cache_status = CACHE_INODE_SUCCESS;
  char                 * oldname = NULL;
  char                 * newname = NULL;

  resp->resop = NFS4_OP_RENAME;
  res_RENAME4.status = NFS4_OK;

  /* Read and validate oldname and newname from uft8 strings. */
  res_RENAME4.status = nfs4_utf8string2dynamic(&arg_RENAME4.oldname,
					       UTF8_SCAN_ALL,
					       &oldname);
  if (res_RENAME4.status != NFS4_OK) {
      goto out;
  }
  res_RENAME4.status = nfs4_utf8string2dynamic(&arg_RENAME4.newname,
					       UTF8_SCAN_ALL,
					       &newname);
  if (res_RENAME4.status != NFS4_OK) {
      goto out;
  }

  /* Do basic checks on a filehandle */
  res_RENAME4.status = nfs4_sanity_check_FH(data, DIRECTORY, FALSE);
  if(res_RENAME4.status != NFS4_OK)
    goto out;

  res_RENAME4.status = nfs4_sanity_check_saved_FH(data, DIRECTORY, FALSE);
  if(res_RENAME4.status != NFS4_OK)
    goto out;

  /* Pseudo Fs is explictely a Read-Only File system */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    {
      res_RENAME4.status = NFS4ERR_ROFS;
      goto out;
    }

  if (nfs_in_grace())
    {
      res_RENAME4.status = NFS4ERR_GRACE;
      goto out;
    }

  /* If data->exportp is null, a junction from pseudo fs was traversed, credp and exportp have to be updated */
  if(data->pexport == NULL)
    {
      res_RENAME4.status = nfs4_SetCompoundExport(data);
      if (res_RENAME4.status != NFS4_OK)
        goto out;
    }

  /* No Cross Device */
  if(((file_handle_v4_t *) (data->currentFH.nfs_fh4_val))->exportid !=
     ((file_handle_v4_t *) (data->savedFH.nfs_fh4_val))->exportid)
    {
      res_RENAME4.status = NFS4ERR_XDEV;
      goto out;
    }

  /* destination must be a directory */
  dst_entry = data->current_entry;

  if(data->current_filetype != DIRECTORY)
    {
      res_RENAME4.status = NFS4ERR_NOTDIR;
      goto out;
    }

  /* Convert saved FH into a vnode */
  src_entry = data->saved_entry;

  /* Source must be a directory */
  if(data->saved_filetype != DIRECTORY)
    {
      res_RENAME4.status = NFS4ERR_NOTDIR;
      goto out;
    }

  /* Renaming a file to himself is allowed, returns NFS4_OK */
  if ((src_entry == dst_entry) &&
      (strcmp(oldname, newname) == 0))
    {
      res_RENAME4.status = NFS4_OK;
      goto out;
    }

  /* For the change_info4, get the 'change' attributes for both directories */
  if((cache_status = cache_inode_getattr(src_entry,
                                         &attr_src,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      goto out;
    }

  /**
   * @todo ACE: Why are we doing things?  The cache_inode_rename
   * function already does this.
   */

  /* Lookup oldfile to see if it exists (refcount +1) */
  if ((tst_entry_src = cache_inode_lookup(src_entry,
                                          oldname,
                                          &attr_tst_src,
                                          data->req_ctx,
                                          &cache_status)) == NULL)
    {
      res_RENAME4.status = nfs4_Errno(cache_status);
      goto release;
    }

  /* Lookup file with new name to see if it already exists (refcount +1),
   * I expect to get NO_ERROR or ENOENT, anything else means an error */
  tst_entry_dst = cache_inode_lookup(dst_entry,
                                     newname,
                                     &attr_tst_dst,
                                     data->req_ctx,
                                     &cache_status);
  if((cache_status != CACHE_INODE_SUCCESS) &&
     (cache_status != CACHE_INODE_NOT_FOUND))
    {
        /* Unexpected status at this step, exit with an error */
        res_RENAME4.status = nfs4_Errno(cache_status);
        goto release;
    }

  if (cache_status == CACHE_INODE_NOT_FOUND)
    tst_entry_dst = NULL;       /* Just to make sure */

  /* Renaming a file to one of its own hardlink is allowed, return NFS4_OK */
  if(tst_entry_src == tst_entry_dst) {
      res_RENAME4.status = NFS4_OK;
      goto release;
    }

  /* Renaming dir into existing file should return NFS4ERR_EXIST */
  if ((tst_entry_src->type == DIRECTORY) &&
      ((tst_entry_dst != NULL) &&
       (tst_entry_dst->type == REGULAR_FILE)))
    {
      res_RENAME4.status = NFS4ERR_EXIST;
      goto release;
    }

  /* Renaming file into existing dir should return NFS4ERR_EXIST */
  if(tst_entry_src->type == REGULAR_FILE)
    {
      if(tst_entry_dst != NULL)
        {
          if(tst_entry_dst->type == DIRECTORY)
            {
              res_RENAME4.status = NFS4ERR_EXIST;
              goto release;
            }
        }
    }

  /* Renaming dir1 into existing, nonempty dir2 should return NFS4ERR_EXIST
   * Renaming file into existing, nonempty dir should return NFS4ERR_EXIST */
  if(tst_entry_dst != NULL)
    {
      if((tst_entry_dst->type == DIRECTORY)
         && ((tst_entry_src->type == DIRECTORY)
             || (tst_entry_src->type == REGULAR_FILE)))
        {
          if(cache_inode_is_dir_empty_WithLock(tst_entry_dst) ==
             CACHE_INODE_DIR_NOT_EMPTY)
            {
              res_RENAME4.status = NFS4ERR_EXIST;
              goto release;
            }
        }
    }

  res_RENAME4.RENAME4res_u.resok4.source_cinfo.before
       = cache_inode_get_changeid4(src_entry);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.before
       = cache_inode_get_changeid4(dst_entry);

  if(cache_status == CACHE_INODE_SUCCESS)
    {
      /* New entry already exists, its attributes are in attr_tst_*,
         check for old entry to see if types are compatible */
/** @TODO do we think we are comparing for compatible types here or
 *  what is actually happening which is a bit compare of the file handle
 *  itself.  equality here means same inode.
 */

      if(tst_entry_dst->obj_handle->ops->compare(tst_entry_dst->obj_handle,
                                                   tst_entry_src->obj_handle))
        {
          /* For the change_info4, get the 'change' attributes for
             both directories */
          res_RENAME4.RENAME4res_u.resok4.source_cinfo.before
            = cache_inode_get_changeid4(src_entry);
          res_RENAME4.RENAME4res_u.resok4.target_cinfo.before
            = cache_inode_get_changeid4(dst_entry);
          res_RENAME4.RENAME4res_u.resok4.target_cinfo.atomic = FALSE;
          res_RENAME4.RENAME4res_u.resok4.source_cinfo.atomic = FALSE;

          res_RENAME4.status = NFS4_OK;
          goto release;
        }
      else
        {
          /* Destination exists and is something different from source */
          if(( tst_entry_src->type == REGULAR_FILE &&
              tst_entry_dst->type == REGULAR_FILE ) ||
              ( tst_entry_src->type == DIRECTORY &&
              tst_entry_dst->type == DIRECTORY ))
            {
              if(cache_inode_rename(src_entry,
                                    oldname,
                                    dst_entry,
                                    newname,
                                    &attr_src,
                                    &attr_dst,
                                    data->req_ctx,
                                    &cache_status) != CACHE_INODE_SUCCESS)
               {

                 res_RENAME4.status = nfs4_Errno(cache_status);
                 goto release;
               }
            }
          else
            {
              res_RENAME4.status = NFS4ERR_EXIST;
              goto release;
            }
        }
    }
  else
    {
      /* New entry does not already exist, call cache_entry_rename */
      if(cache_inode_rename(src_entry,
                            oldname,
                            dst_entry,
                            newname,
                            &attr_src,
                            &attr_dst,
                            data->req_ctx,
                            &cache_status) != CACHE_INODE_SUCCESS)
        {
          res_RENAME4.status = nfs4_Errno(cache_status);
          goto release;
        }
    }

  /* If you reach this point, then everything was alright */
  /* For the change_info4, get the 'change' attributes for both directories */

  res_RENAME4.RENAME4res_u.resok4.source_cinfo.after =
       cache_inode_get_changeid4(src_entry);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.after =
       cache_inode_get_changeid4(dst_entry);
  res_RENAME4.RENAME4res_u.resok4.target_cinfo.atomic = FALSE;
  res_RENAME4.RENAME4res_u.resok4.source_cinfo.atomic = FALSE;
  res_RENAME4.status = nfs4_Errno(cache_status);

 out:
  if (oldname) {
    gsh_free(oldname);
    oldname = NULL;
  }


  if (newname) {
    gsh_free(newname);
  }

 release:
  if (tst_entry_src)
      cache_inode_put(tst_entry_src);
  if (tst_entry_dst)
      cache_inode_put(tst_entry_dst);

  return (res_RENAME4.status);
}                               /* nfs4_op_rename */

/**
 * @brief Free memory allocated for RENAME result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_RENAME operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_rename_Free(RENAME4res *resp)
{
  /* Nothing to be done */
  return;
} /* nfs4_op_rename_Free */
