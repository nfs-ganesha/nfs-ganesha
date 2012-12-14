// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_unlink.c
// Description: FSAL unlink operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------

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
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object removing function.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <unistd.h>

#include "pt_ganesha.h"

/**
 * FSAL_unlink:
 * Remove a filesystem object .
 *
 * \param parentdir_handle (input):
 *        Handle of the parent directory of the object to be deleted.
 * \param p_object_name (input):
 *        Name of the object to be removed.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param parentdir_attributes (optionnal input/output):
 *        Post operation attributes of the parent directory.
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

fsal_status_t 
PTFSAL_unlink(fsal_handle_t      * p_parent_directory_handle,    /* IN */
              fsal_name_t        * p_object_name,                /* IN */
              fsal_op_context_t  * p_context,                    /* IN */
              fsal_attrib_list_t * p_parent_directory_attributes /*[IN/OU ]*/)
{

  fsal_status_t status;
  int rc, errsv;
  fsi_stat_struct buffstat;
  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t parent_dir_attrs;

  /* sanity checks. */
  if(!p_parent_directory_handle || !p_context || !p_object_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_unlink);

  FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink [%s] entry\n",p_object_name->name);

  /* get directory metadata */
  parent_dir_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
  status = PTFSAL_getattrs(p_parent_directory_handle, p_context, 
                           &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_unlink);

  /* build the child path */

  FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink [%s] build child path\n",
            p_object_name->name);

  /* get file metadata */
  rc = ptfsal_stat_by_parent_name(p_context, p_parent_directory_handle, 
                                  p_object_name->name, &buffstat);
  if (rc) {
      FSI_TRACE(FSI_DEBUG, "FSI - PTFSAL_unlink stat [%s] rc %d\n",
                p_object_name->name, rc);
      Return(posix2fsal_error(errno), errno, INDEX_FSAL_unlink);
  }

  /* check access rights */

  /* Sticky bit on the directory => the user who wants to delete the file 
   * must own it or its parent dir 
   */
  if((fsal2unix_mode(parent_dir_attrs.mode) & S_ISVTX)
     && parent_dir_attrs.owner != p_context->credential.user
     && buffstat.st_uid != p_context->credential.user 
     && p_context->credential.user != 0) {
    Return(ERR_FSAL_ACCESS, 0, INDEX_FSAL_unlink);
  }

  /* client must be able to lookup the parent directory and modify it */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK  | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);

  if(!p_context->export_context->fe_static_fs_info->accesscheck_support)
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &parent_dir_attrs);
  else
    status = fsal_internal_access(p_context, p_parent_directory_handle, 
                                  access_mask,
                                  &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_unlink);

  /******************************
   * DELETE FROM THE FILESYSTEM *
   ******************************/

  /* If the object to delete is a directory, use 'rmdir' to delete the object, 
   * else use 'unlink' 
   */
  if (S_ISDIR(buffstat.st_mode)) {
    FSI_TRACE(FSI_DEBUG, "Deleting directory %s",p_object_name->name);
    rc = ptfsal_rmdir(p_context, p_parent_directory_handle, 
                      p_object_name->name);

  } else {
    FSI_TRACE(FSI_DEBUG, "Deleting file %s", p_object_name->name);
    rc = ptfsal_unlink(p_context, p_parent_directory_handle, 
                       p_object_name->name);
  }
  if(rc) {
    errsv = errno;
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_unlink);
  }

  /***********************
   * FILL THE ATTRIBUTES *
   ***********************/

  if(p_parent_directory_attributes) {
    status =
      PTFSAL_getattrs(p_parent_directory_handle, p_context,
                      p_parent_directory_attributes);
    if(FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_parent_directory_attributes->asked_attributes);
      FSAL_SET_MASK(p_parent_directory_attributes->asked_attributes,
                    FSAL_ATTR_RDATTR_ERR);
    }
  }
  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlink);

}
