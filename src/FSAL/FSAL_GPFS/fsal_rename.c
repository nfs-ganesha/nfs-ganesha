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
 * ------------- 
 */

/**
 *
 * \file    fsal_rename.c
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object renaming/moving function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "gpfs_methods.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_hdl (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_hdl (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t GPFSFSAL_rename(struct fsal_obj_handle *old_hdl,    /* IN */
                          const char * p_old_name,                /* IN */
                          struct fsal_obj_handle *new_hdl,        /* IN */
                          const char * p_new_name,                /* IN */
                          const struct req_op_context * p_context) /* IN */
{

  fsal_status_t status;
  struct stat buffstat;
  int src_equal_tgt = false;
  fsal_accessflags_t access_mask = 0;
  struct attrlist src_dir_attrs, tgt_dir_attrs;
  int mount_fd;
  struct gpfs_fsal_obj_handle *old_gpfs_hdl, *new_gpfs_hdl;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_hdl || !new_hdl || !p_old_name || !p_new_name || !p_context)
    return fsalstat(ERR_FSAL_FAULT, 0);

  old_gpfs_hdl = container_of(old_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  new_gpfs_hdl = container_of(new_hdl, struct gpfs_fsal_obj_handle, obj_handle);
  mount_fd = gpfs_get_root_fd(old_hdl->export);

  /* retrieve directory metadata for checking access rights */

  src_dir_attrs.mask = old_hdl->export->ops->fs_supported_attrs(old_hdl->export);
  status = GPFSFSAL_getattrs(old_hdl->export, p_context, old_gpfs_hdl->handle,
                             &src_dir_attrs);
  if(FSAL_IS_ERROR(status))
    return(status);

  /* optimisation : don't do the job twice if source dir = dest dir  */
    if(old_hdl->ops->compare(old_hdl, new_hdl))
    {
      src_equal_tgt = true;
      tgt_dir_attrs = src_dir_attrs;
    }
  else
    {
      /* retrieve destination attrs */
      tgt_dir_attrs.mask = new_hdl->export->ops->fs_supported_attrs(new_hdl->export);
      status = GPFSFSAL_getattrs(old_hdl->export, p_context, new_gpfs_hdl->handle,
                                 &tgt_dir_attrs);
      if(FSAL_IS_ERROR(status))
        return(status);
    }

  /* check access rights */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);

  if(!old_hdl->export->ops->fs_supports(old_hdl->export, fso_accesscheck_support))
    status = fsal_internal_testAccess(p_context, access_mask, &src_dir_attrs);
  else
    status = fsal_internal_access(mount_fd, p_context, old_gpfs_hdl->handle,
                                  access_mask, &src_dir_attrs);
  if(FSAL_IS_ERROR(status)) {
    return(status);
  }

  if(!src_equal_tgt)
    {
      access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                                   FSAL_ACE_PERM_ADD_SUBDIRECTORY);

          if(!old_hdl->export->ops->fs_supports(old_hdl->export,
                                                       fso_accesscheck_support))
            status = fsal_internal_testAccess(p_context, access_mask, &tgt_dir_attrs);
	  else
	    status = fsal_internal_access(mount_fd, p_context,
	                     new_gpfs_hdl->handle, access_mask, &tgt_dir_attrs);
      if(FSAL_IS_ERROR(status)) {
        return(status);
      }
    }

  /* build file paths */
  status = fsal_internal_stat_name(mount_fd, old_gpfs_hdl->handle, p_old_name, &buffstat);
  if(FSAL_IS_ERROR(status))
    return(status);

  /* Check sticky bits */

  /* Sticky bit on the source directory => the user who wants to delete the file must own it or its parent dir */
  if((fsal2unix_mode(src_dir_attrs.mode) & S_ISVTX) &&
     src_dir_attrs.owner != p_context->creds->caller_uid &&
     buffstat.st_uid != p_context->creds->caller_uid && p_context->creds->caller_uid != 0) {
    return fsalstat(ERR_FSAL_ACCESS, 0);
  }

  /* Sticky bit on the target directory => the user who wants to create the file must own it or its parent dir */
  if(fsal2unix_mode(tgt_dir_attrs.mode) & S_ISVTX)
    {
      status = fsal_internal_stat_name(mount_fd, new_gpfs_hdl->handle, p_new_name, &buffstat);

      if(FSAL_IS_ERROR(status))
        {
          if(status.major != ERR_FSAL_NOENT)
            {
              return(status);
            }
        }
      else
        {

          if(tgt_dir_attrs.owner != p_context->creds->caller_uid
             && buffstat.st_uid != p_context->creds->caller_uid
             && p_context->creds->caller_uid != 0)
            {
              return fsalstat(ERR_FSAL_ACCESS, 0);
            }
        }
    }

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
  status = fsal_internal_rename_fh(mount_fd, old_gpfs_hdl->handle,
                                   new_gpfs_hdl->handle, p_old_name,
                                   p_new_name);

  if(FSAL_IS_ERROR(status))
    return(status);

  /* OK */
  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
