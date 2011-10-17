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
 * \author  $Author: leibovic $
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
#include "FSAL/access_check.h"
#include "fsal_convert.h"

/**
 * FSAL_rename:
 * Change name and/or parent dir of a filesystem object.
 *
 * \param old_parentdir_handle (input):
 *        Source parent directory of the object is to be moved/renamed.
 * \param p_old_name (input):
 *        Pointer to the current name of the object to be moved/renamed.
 * \param new_parentdir_handle (input):
 *        Target parent directory for the object.
 * \param p_new_name (input):
 *        Pointer to the new name for the object.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param src_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the source directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 * \param tgt_dir_attributes (optionnal input/output): 
 *        Post operation attributes for the target directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - Another error code if an error occured.
 */

fsal_status_t VFSFSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          fsal_handle_t * p_new_parentdir_handle,       /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          fsal_attrib_list_t * p_src_dir_attributes,    /* [ IN/OUT ] */
                          fsal_attrib_list_t * p_tgt_dir_attributes     /* [ IN/OUT ] */
    )
{

  int rc, errsv;
  fsal_status_t status;
  struct stat old_parent_buffstat, new_parent_buffstat, buffstat;
  int old_parent_fd, new_parent_fd;
  int src_equal_tgt = FALSE;
  uid_t user = ((vfsfsal_op_context_t *)p_context)->credential.user;

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!p_old_parentdir_handle || !p_new_parentdir_handle
     || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  /* Get directory access path by fid */

  TakeTokenFSCall();
  status = fsal_internal_handle2fd(p_context, p_old_parentdir_handle,
                                   &old_parent_fd,
                                   O_RDONLY | O_DIRECTORY);
  ReleaseTokenFSCall();

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_rename);

  /* retrieve directory metadata for checking access rights */

  TakeTokenFSCall();
  rc = fstat(old_parent_fd, &old_parent_buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      close(old_parent_fd);
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_rename);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
    }

  /* optimisation : don't do the job twice if source dir = dest dir  */
  if(!FSAL_handlecmp(p_old_parentdir_handle, p_new_parentdir_handle, &status))
    {
      new_parent_fd = old_parent_fd;
      src_equal_tgt = TRUE;
      new_parent_buffstat = old_parent_buffstat;
    }
  else
    {
      TakeTokenFSCall();
      status = fsal_internal_handle2fd(p_context, p_new_parentdir_handle,
                                       &new_parent_fd,
                                       O_RDONLY | O_DIRECTORY);
      ReleaseTokenFSCall();

      if(FSAL_IS_ERROR(status))
        {
          close(old_parent_fd);
          ReturnStatus(status, INDEX_FSAL_rename);
        }
      /* retrieve destination attrs */
      TakeTokenFSCall();
      rc = fstat(new_parent_fd, &new_parent_buffstat);
      errsv = errno;
      ReleaseTokenFSCall();

      if(rc)
        {
          /* close old and new parent fd */
          close(old_parent_fd);
          close(new_parent_fd);
          if(errsv == ENOENT)
            Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_rename);
          else
            Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
        }

    }

  /* check access rights */

  status = fsal_check_access(p_context, FSAL_W_OK | FSAL_X_OK,
                                    &old_parent_buffstat,
                                    NULL);
  if(FSAL_IS_ERROR(status)) {
    close(old_parent_fd);
    if (!src_equal_tgt)
      close(new_parent_fd);
    ReturnStatus(status, INDEX_FSAL_rename);
  }
  if(!src_equal_tgt)
    {
      status =  fsal_check_access(p_context, FSAL_W_OK | FSAL_X_OK,
                                         &new_parent_buffstat,
                                         NULL);
      if(FSAL_IS_ERROR(status)) {
        close(old_parent_fd);
        close(new_parent_fd);
        ReturnStatus(status, INDEX_FSAL_rename);
      }
    }

  /* build file paths */
  TakeTokenFSCall();
  rc = fstatat(old_parent_fd, p_old_name->name, &buffstat, AT_SYMLINK_NOFOLLOW);
  errsv = errno;
  ReleaseTokenFSCall();
  if(rc) {
    close(old_parent_fd);
    if (!src_equal_tgt)
      close(new_parent_fd);
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
  }

  /* Check sticky bits */

  /* Sticky bit on the source directory => the user who wants to delete the file must own it or its parent dir */
  if((old_parent_buffstat.st_mode & S_ISVTX) &&
     old_parent_buffstat.st_uid != user &&
     buffstat.st_uid != user && user != 0) {
    close(old_parent_fd);
    if (!src_equal_tgt)
      close(new_parent_fd);
    Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_rename);
  }

  /* Sticky bit on the target directory => the user who wants to create the file must own it or its parent dir */
  if(new_parent_buffstat.st_mode & S_ISVTX)
    {
      TakeTokenFSCall();
      rc = fstatat(new_parent_fd, p_new_name->name, &buffstat, AT_SYMLINK_NOFOLLOW);
      errsv = errno;
      ReleaseTokenFSCall();

      if(rc < 0)
        {
          if(errsv != ENOENT)
            {
              close(old_parent_fd);
              if (!src_equal_tgt)
                close(new_parent_fd);
              Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
            }
        }
      else
        {

          if(new_parent_buffstat.st_uid != user
             && buffstat.st_uid != user
             && user != 0)
            {
              close(old_parent_fd);
              if (!src_equal_tgt)
                close(new_parent_fd);
              Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_rename);
            }
        }
    }

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
  TakeTokenFSCall();
  rc = renameat(old_parent_fd, p_old_name->name, new_parent_fd, p_new_name->name);
  errsv = errno;
  ReleaseTokenFSCall();
  close(old_parent_fd);
  if (!src_equal_tgt)
    close(new_parent_fd);

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);

  /***********************
   * Fill the attributes *
   ***********************/

  if(p_src_dir_attributes)
    {

      status = VFSFSAL_getattrs(p_old_parentdir_handle, p_context, p_src_dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_src_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_src_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  if(p_tgt_dir_attributes)
    {

      status = VFSFSAL_getattrs(p_new_parentdir_handle, p_context, p_tgt_dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_tgt_dir_attributes->asked_attributes);
          FSAL_SET_MASK(p_tgt_dir_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
