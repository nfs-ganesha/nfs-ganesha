// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_rename.c
// Description: Common FSI IPC Client and Server definitions
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_ganesha.h"
#include "FSAL/access_check.h"

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

fsal_status_t 
PTFSAL_rename(fsal_handle_t * p_old_parentdir_handle,       /* IN */
              fsal_name_t   * p_old_name,                   /* IN */
              fsal_handle_t * p_new_parentdir_handle,       /* IN */
              fsal_name_t   * p_new_name,                   /* IN */
              fsal_op_context_t  * p_context,               /* IN */
              fsal_attrib_list_t * p_src_dir_attributes,    /* [ IN/OUT ] */
              fsal_attrib_list_t * p_tgt_dir_attributes     /* [ IN/OUT ] */)
{

  int rc, errsv;
  fsal_status_t status;
  fsi_stat_struct old_bufstat, new_bufstat;
  int stat_rc;

  FSI_TRACE(FSI_DEBUG, "FSI Rename--------------\n");

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!p_old_parentdir_handle || !p_new_parentdir_handle
     || !p_old_name || !p_new_name || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  /* build file paths */
  stat_rc = ptfsal_stat_by_parent_name(p_context, p_old_parentdir_handle, 
                                       p_old_name->name, &old_bufstat); 
  errsv = errno;
  if(stat_rc) {
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);
  }

  /*************************************
   * Rename the file on the filesystem *
   *************************************/
  rc = ptfsal_rename(p_context, p_old_parentdir_handle, p_old_name->name, 
                     p_new_parentdir_handle, p_new_name->name);  
  errsv = errno;

  if(rc)
    Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_rename);

  /***********************
   * Fill the attributes *
   ***********************/

  if(p_src_dir_attributes) {

    status = PTFSAL_getattrs(p_old_parentdir_handle, p_context, 
                             p_src_dir_attributes);

    if(FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_src_dir_attributes->asked_attributes);
      FSAL_SET_MASK(p_src_dir_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }

  if(p_tgt_dir_attributes) {

    status = PTFSAL_getattrs(p_new_parentdir_handle, 
                             p_context, p_tgt_dir_attributes);

    if(FSAL_IS_ERROR(status)) {
      FSAL_CLEAR_MASK(p_tgt_dir_attributes->asked_attributes);
      FSAL_SET_MASK(p_tgt_dir_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);

}
