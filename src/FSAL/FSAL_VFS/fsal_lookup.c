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
#include "FSAL/access_check.h"
#include "fsal_convert.h"

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
 *
 * \return - ERR_FSAL_NO_ERROR, if no error.
 *         - Another error code else.
 *          
 */
fsal_status_t VFSFSAL_lookup(fsal_handle_t * p_parent_directory_handle,      /* IN */
                             fsal_name_t * p_filename,  /* IN */
                             fsal_op_context_t * p_context,  /* IN */
                             fsal_handle_t * p_object_handle,        /* OUT */
                             fsal_attrib_list_t * p_object_attributes   /* [ IN/OUT ] */
    )
{
  vfsfsal_handle_t * vfs_handle = (vfsfsal_handle_t *)p_object_handle;
  vfsfsal_op_context_t *vfs_context = (vfsfsal_op_context_t *)p_context;
  int rc, errsv;
  fsal_status_t status;
  struct stat buffstat;
  int parentfd;

  /* sanity checks
   * note : object_attributes is optionnal
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
      /* Copy the root handle */
      memcpy( (char *)&vfs_handle->data.vfs_handle,
	     (char *)&vfs_context->export_context->root_handle,
	      sizeof( vfs_file_handle_t ) ) ;
       
      /* get attributes, if asked */
      if(p_object_attributes)
        {
          status = VFSFSAL_getattrs(p_object_handle, p_context, p_object_attributes);
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
              FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
            }
        }
      /* Done */
      Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
    }

  /* retrieve directory attributes */
  TakeTokenFSCall();
  status =
      fsal_internal_handle2fd(p_context, p_parent_directory_handle, &parentfd, O_RDONLY);
  ReleaseTokenFSCall();
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

  /* get directory metadata */
  TakeTokenFSCall();
  rc = fstat(parentfd, &buffstat);
  errsv = errno;
  ReleaseTokenFSCall();

  if(rc)
    {
      close( parentfd ) ;

      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_lookup);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_lookup);
    }

  /* Be careful about junction crossing, symlinks, hardlinks,... */
  switch (posix2fsal_type(buffstat.st_mode))
    {
    case FSAL_TYPE_DIR:
      // OK
      break;

    case FSAL_TYPE_JUNCTION:
      // This is a junction
      close( parentfd ) ;
      Return(ERR_FSAL_XDEV, 0, INDEX_FSAL_lookup);

    case FSAL_TYPE_FILE:
    case FSAL_TYPE_LNK:
    case FSAL_TYPE_XATTR:
      // not a directory 
      close( parentfd ) ;
      Return(ERR_FSAL_NOTDIR, 0, INDEX_FSAL_lookup);

    default:
      close( parentfd ) ;
      Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
    }

  LogFullDebug(COMPONENT_FSAL, "lookup of inode=%"PRIu64"/%s", buffstat.st_ino,
          p_filename->name);

  /* check rights to enter into the directory */
  status = fsal_check_access(p_context, FSAL_X_OK, &buffstat, NULL);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookup);

   /* get file handle, it it exists */
  TakeTokenFSCall();

  vfs_handle->data.vfs_handle.handle_bytes = VFS_HANDLE_LEN ;
  if( vfs_name_by_handle_at( parentfd,  p_filename->name,
			     &vfs_handle->data.vfs_handle) != 0 )
   {
      errsv = errno;
      ReleaseTokenFSCall();
      close( parentfd ) ;
      Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_lookup);
   }

  ReleaseTokenFSCall();
  close( parentfd ) ;


  /* get object attributes */
  if(p_object_attributes)
    {
      status = VFSFSAL_getattrs(p_object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

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

fsal_status_t VFSFSAL_lookupPath(fsal_path_t * p_path,  /* IN */
                                 fsal_op_context_t * p_context,      /* IN */
                                 fsal_handle_t * object_handle,      /* OUT */
                                 fsal_attrib_list_t * p_object_attributes       /* [ IN/OUT ] */
    )
{
  fsal_status_t status;

  /* sanity checks
   * note : object_attributes is optional.
   */

  if(!object_handle || !p_context || !p_path)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  /* test whether the path begins with a slash */

  if(p_path->path[0] != '/')
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_lookupPath);

  /* directly call the lookup function */

  status = fsal_internal_Path2Handle(p_context, p_path, object_handle);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_lookupPath);

  /* get object attributes */
  if(p_object_attributes)
    {
      status = VFSFSAL_getattrs(object_handle, p_context, p_object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
          FSAL_SET_MASK(p_object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

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
fsal_status_t VFSFSAL_lookupJunction(fsal_handle_t * p_junction_handle,      /* IN */
                                     fsal_op_context_t * p_context,  /* IN */
                                     fsal_handle_t * p_fsoot_handle, /* OUT */
                                     fsal_attrib_list_t * p_fsroot_attributes   /* [ IN/OUT ] */
    )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookupJunction);
}
