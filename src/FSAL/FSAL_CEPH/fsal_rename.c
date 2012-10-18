/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010 The Linux Box, Inc.
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Portions copyright CEA/DAM/DIF  (2008)
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
 * \brief   object renaming/moving function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
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
 * \param p_context (input):
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
 *        - ERR_FSAL_STALE        (a parent directory handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (a parent directory handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_old_name does not exist)
 *        - ERR_FSAL_NOTEMPTY     (the target object is a non empty directory)
 *        - ERR_FSAL_XDEV         (tried to move an object across different filesystems)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returnoed :
  *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
  */

fsal_status_t CEPHFSAL_rename(fsal_handle_t * extold_parent,
                              fsal_name_t * old_name,
                              fsal_handle_t * extnew_parent,
                              fsal_name_t * new_name,
                              fsal_op_context_t * extcontext,
                              fsal_attrib_list_t * src_dir_attributes,
                              fsal_attrib_list_t * tgt_dir_attributes)
{
  int rc;
  cephfsal_handle_t* old_parent = (cephfsal_handle_t*) extold_parent;
  cephfsal_handle_t* new_parent = (cephfsal_handle_t*) extnew_parent;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  char oldstr[FSAL_MAX_NAME_LEN+1];
  char newstr[FSAL_MAX_NAME_LEN+1];
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks.
   * note : src/tgt_dir_attributes are optional.
   */
  if(!old_parent || !new_parent || !old_name || !new_name || !context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_rename);

  if(src_dir_attributes)
    {
      fsal_status_t status =
        CEPHFSAL_getattrs(extold_parent, extcontext, src_dir_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(src_dir_attributes->asked_attributes);
          FSAL_SET_MASK(src_dir_attributes->asked_attributes,
                        FSAL_ATTR_RDATTR_ERR);
        }
    }

  if(tgt_dir_attributes)
    {
      fsal_status_t status;

      /* optimization when src=tgt : */

      if(!CEPHFSAL_handlecmp(extold_parent, extnew_parent, &status)
         && src_dir_attributes)
        {
          /* If source dir = target dir, we just copy the attributes.
           * to avoid doing another getattr.
           */
          (*tgt_dir_attributes) = (*src_dir_attributes);
        }
      else
        {
          status = CEPHFSAL_getattrs(extnew_parent, extcontext,
                                     tgt_dir_attributes);

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(tgt_dir_attributes->asked_attributes);
              FSAL_SET_MASK(tgt_dir_attributes->asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
            }
        }
    }

  FSAL_name2str(old_name, oldstr, FSAL_MAX_NAME_LEN);
  FSAL_name2str(new_name, newstr, FSAL_MAX_NAME_LEN);
  rc = ceph_ll_rename(context->export_context->cmount,
                      VINODE(old_parent), oldstr,
                      VINODE(new_parent), newstr,
                      uid, gid);

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_rename);

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_rename);
}
