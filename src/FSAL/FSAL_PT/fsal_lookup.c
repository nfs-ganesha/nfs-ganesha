// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_lookup.c
// Description: FSAL lookup operations implementation
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
 * \file    fsal_lookup.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.17 $
 * \brief   Lookup operations.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "pt_ganesha.h"



/**
 * FSAL_lookup :
 * Looks up for an object into a directory.
 *
 * Note : if parent handle and filename are NULL,
 *        this retrieves root's handle.
 *
 * \param parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param filename (input)
 *        The name of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *
 */
fsal_status_t 
PTFSAL_lookup(fsal_handle_t * p_parent_directory_handle,    /* IN */
              fsal_name_t * p_filename,                     /* IN */
              fsal_op_context_t * p_context,                /* IN */
              fsal_handle_t * object_handle,                /* OUT */
              fsal_attrib_list_t * p_object_attributes      /* [ IN/OUT ] */)
{
  fsal_status_t status;
  fsal_accessflags_t access_mask = 0;
  fsal_attrib_list_t parent_dir_attrs;
  ptfsal_handle_t *p_object_handle = (ptfsal_handle_t *)object_handle;
  fsi_stat_struct buffstat;
  int rc;

  FSI_TRACE(FSI_DEBUG, "Begin##################################\n");
  if (p_filename != NULL)
    FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup file [%s]\n", p_filename->name);

  if (p_parent_directory_handle != NULL)
    FSI_TRACE(FSI_DEBUG, "FSI - fsal_lookup parent dir\n");

  /* sanity checks
   * note : object_attributes is optional
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!p_object_handle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* filename AND parent handle are NULL => lookup "/" */
  if((p_parent_directory_handle && !p_filename)
     || (!p_parent_directory_handle && p_filename))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  /* get information about root */
  if(!p_parent_directory_handle)
    {

      FSI_TRACE(FSI_DEBUG, "Parent directory handle is NULL\n");
      ptfsal_handle_t *root_handle = 
        &((ptfsal_op_context_t *)p_context)->export_context->mount_root_handle;

      /* get handle for the mount point  */
      memcpy(p_object_handle->data.handle.f_handle, 
             root_handle->data.handle.f_handle, 
             sizeof(p_object_handle->data.handle.f_handle));
      p_object_handle->data.handle.handle_size = 
        root_handle->data.handle.handle_size;
      p_object_handle->data.handle.handle_key_size = 
        root_handle->data.handle.handle_key_size;
      p_object_handle->data.handle.handle_type = 
        root_handle->data.handle.handle_type;
      p_object_handle->data.handle.handle_version = 
        root_handle->data.handle.handle_version;

      /* get attributes, if asked */
      if(p_object_attributes)
        {
          status = PTFSAL_getattrs(object_handle, p_context, 
                                   p_object_attributes);
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
              FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                            FSAL_ATTR_RDATTR_ERR);
            }
        }

      /* Done */
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
    }

  /* get directory metadata */

  parent_dir_attrs.asked_attributes = PTFS_SUPPORTED_ATTRIBUTES;
  status = PTFSAL_getattrs(p_parent_directory_handle, p_context, 
                           &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  FSI_TRACE(FSI_DEBUG, "FSI - lookup parent directory type = %d\n", 
            parent_dir_attrs.type);

  /* Be careful about junction crossing, symlinks, hardlinks,... */
  switch (parent_dir_attrs.type)
    {
    case FSAL_TYPE_DIR:
      // OK
      break;

    case FSAL_TYPE_JUNCTION:
      // This is a junction
      Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

    case FSAL_TYPE_FILE:
    case FSAL_TYPE_LNK:
    case FSAL_TYPE_XATTR:
      // not a directory
      Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

    default:
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
    }

  /* check rights to enter into the directory */

  /* Set both mode and ace4 mask */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK | FSAL_X_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);

  if(!p_context->export_context->fe_static_fs_info->accesscheck_support)
    status = fsal_internal_testAccess(p_context, access_mask, NULL, 
                                      &parent_dir_attrs);
  else
    status = fsal_internal_access(p_context, p_parent_directory_handle, 
                                  access_mask,
                                  &parent_dir_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  /* get file handle, it it exists */
  /* This might be a race, but it's the best we can currently do */
  rc = ptfsal_stat_by_parent_name(p_context, p_parent_directory_handle, 
                                  p_filename->name, &buffstat);
  if(rc < 0)
  {
    ReturnCode(ERR_FSAL_NOENT, errno);
  }
  memcpy(&p_object_handle->data.handle.f_handle, 
         &buffstat.st_persistentHandle.handle, 
         FSI_PERSISTENT_HANDLE_N_BYTES); 
  p_object_handle->data.handle.handle_size = FSI_PERSISTENT_HANDLE_N_BYTES;
  p_object_handle->data.handle.handle_type = posix2fsal_type(buffstat.st_mode);
  p_object_handle->data.handle.handle_key_size = OPENHANDLE_KEY_LEN;
  p_object_handle->data.handle.handle_version  = OPENHANDLE_VERSION;



  /* get object attributes */
  if(p_object_attributes)
    {
      status = PTFSAL_getattrs(object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                        FSAL_ATTR_RDATTR_ERR);
        }
    }

  FSI_TRACE(FSI_DEBUG, "End##################################\n");
  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupPath :
 * Looks up for an object into the namespace.
 *
 * Note : if path equals "/",
 *        this retrieves root's handle.
 *
 * \param path (input)
 *        The path of the object to find.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param object_handle (output)
 *        The handle of the object corresponding to filename.
 * \param object_attributes (optional input/output)
 *        Pointer to the attributes of the object we found.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 */

fsal_status_t 
PTFSAL_lookupPath(fsal_path_t * p_path,     /* IN */
                  fsal_op_context_t * p_context,    /* IN */
                  fsal_handle_t * object_handle,    /* OUT */
                  fsal_attrib_list_t * p_object_attributes  /* [ IN/OUT ] */)
{
  fsal_status_t status;
  ptfsal_handle_t *p_fsi_handle;

  FSI_TRACE(FSI_DEBUG, "Begin-------------------------------------\n");

  /* sanity checks
   * note : object_attributes is optionnal.
   */
  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */
  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* directly call the lookup function */
  FSI_TRACE(FSI_DEBUG, "FSI - lookupPath [%s] entered\n",p_path->path);

  status = fsal_internal_get_handle(p_context, p_path, object_handle);

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookupPath);

  p_fsi_handle = (ptfsal_handle_t *)object_handle;
  ptfsal_print_handle(p_fsi_handle->data.handle.f_handle);

  /* get object attributes */
  if(p_object_attributes)
    {
      status = PTFSAL_getattrs(object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                        FSAL_ATTR_RDATTR_ERR);
        }
    }

  FSI_TRACE(FSI_DEBUG, "End--------------------------------------\n");
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupPath);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param p_context (input)
 *        Authentication context for the operation (user,...).
 * \param p_fsroot_handle (output)
 *        The handle of root directory of the fileset.
 * \param p_fsroot_attributes (optional input/output)
 *        Pointer to the attributes of the root directory
 *        for the fileset.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        It can be NULL (increases performances).
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *
 */
fsal_status_t 
PTFSAL_lookupJunction(fsal_handle_t * p_junction_handle,    /* IN */
                      fsal_op_context_t * p_context,        /* IN */
                      fsal_handle_t * p_fsoot_handle,       /* OUT */
                      fsal_attrib_list_t * p_fsroot_attributes /*[IN/OUT] */)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}
