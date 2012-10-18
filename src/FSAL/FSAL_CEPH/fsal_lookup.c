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
 * \file    fsal_lookup.c
 * \brief   Lookup operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
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
 *        It can be NULL (increases performances).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_NOENT        (the object designated by p_filename does not exist)
 *        - ERR_FSAL_XDEV         (tried to operate a lookup on a filesystem junction.
 *                                 Use FSAL_lookupJunction instead)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *
 */
fsal_status_t CEPHFSAL_lookup(fsal_handle_t * extparent,
                              fsal_name_t * filename,
                              fsal_op_context_t * extcontext,
                              fsal_handle_t * exthandle,
                              fsal_attrib_list_t * object_attributes)
{
  int rc;
  struct stat st;
  fsal_status_t status;
  cephfsal_handle_t* parent = (cephfsal_handle_t*) extparent;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  char str[FSAL_MAX_NAME_LEN+1];
  struct ceph_mount_info *cmount = context->export_context->cmount;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!handle || !context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

  memset(handle, 0, sizeof(cephfsal_handle_t));

  /* retrieves root handle */

  if(!parent)
    {
      /* check that filename is NULL,
       * else, parent should not
       * be NULL.
       */
      if(filename != NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      /* Ceph seems to have a constant identifying the root inode.
	 Possible source of bugs, so check here if trouble */

      VINODE(handle).ino.val = CEPH_INO_ROOT;
      VINODE(handle).snapid.val = CEPH_NOSNAP;

      if(object_attributes)
        {
          status = CEPHFSAL_getattrs(exthandle, extcontext, object_attributes);

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
            }
        }
    }
  else                          /* this is a real lookup(parent, name)  */
    {
      /* the filename should not be null */
      if(filename == NULL)
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookup);

      FSAL_name2str(filename, str, FSAL_MAX_NAME_LEN);

      /* Ceph returns POSIX errors, so let's use them */

      rc = ceph_ll_lookup(cmount,
                          VINODE(parent), str, &st,
                          FSAL_OP_CONTEXT_TO_UID(context),
                          FSAL_OP_CONTEXT_TO_GID(context));

      if(rc)
        {
          Return(posix2fsal_error(rc), 0, INDEX_FSAL_lookup);
        }

      rc = stat2fsal_fh(cmount, &st, handle);

      if (rc < 0)
        Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

      if(object_attributes)
        {
          /* convert attributes */
          status = posix2fsal_attributes(&st, object_attributes);
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
              Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
            }
        }
    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);

}

/**
 * FSAL_lookupJunction :
 * Get the fileset root for a junction.
 *
 * \param p_junction_handle (input)
 *        Handle of the junction to be looked up.
 * \param cred (input)
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
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (p_junction_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 *
 */
fsal_status_t CEPHFSAL_lookupJunction(fsal_handle_t * extjunction,
                                      fsal_op_context_t * extcontext,
                                      fsal_handle_t * extfsroot,
                                      fsal_attrib_list_t * fsroot_attributes)
{
  Return(ERR_FSAL_SERVERFAULT, 0, INDEX_FSAL_lookup);
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
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - ERR_FSAL_INVAL        (the path argument is not absolute)
 *        - ERR_FSAL_NOENT        (an element in the path does not exist)
 *        - ERR_FSAL_NOTDIR       (an element in the path is not a directory)
 *        - ERR_FSAL_XDEV         (tried to cross a filesystem junction,
 *                                 whereas is has not been authorized in the server
 *                                 configuration - FSAL::auth_xdev_export parameter)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t CEPHFSAL_lookupPath(fsal_path_t * path,
                                  fsal_op_context_t * extcontext,
                                  fsal_handle_t * exthandle,
                                  fsal_attrib_list_t * object_attributes)
{
  int rc;
  struct stat st;
  fsal_status_t status;
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  char str[FSAL_MAX_PATH_LEN];
  struct ceph_mount_info *cmount = context->export_context->cmount;

  /* sanity checks
   * note : object_attributes is optionnal
   *        parent_directory_handle may be null for getting FS root.
   */
  if(!path || !context || !handle)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lookupPath);

  memset(handle, 0, sizeof(cephfsal_handle_t));

  FSAL_path2str(path, str, FSAL_MAX_PATH_LEN);

  /* retrieves root handle */

  if((strcmp(str, "/") == 0))
    {
      VINODE(handle).ino.val = CEPH_INO_ROOT;
      VINODE(handle).snapid.val = CEPH_NOSNAP;

      if(object_attributes)
        {
          status = CEPHFSAL_getattrs(exthandle, extcontext,
                                     object_attributes);

          /* On error, we set a flag in the returned attributes */

          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
            }
        }
    }
  else                          /* this is a real lookup(parent, name)  */
    {
      rc = ceph_ll_walk(cmount, str, &st);

      if(rc)
        {
          Return(posix2fsal_error(rc), 0, INDEX_FSAL_lookupPath);
        }

      stat2fsal_fh(cmount, &st, handle);
      if (rc < 0)
        Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

      if(object_attributes)
        {
          status = posix2fsal_attributes(&st, object_attributes);
          if(FSAL_IS_ERROR(status))
            {
              FSAL_CLEAR_MASK(object_attributes->asked_attributes);
              FSAL_SET_MASK(object_attributes->asked_attributes,
                            FSAL_ATTR_RDATTR_ERR);
              Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getattrs);
            }
        }
    }

  /* lookup complete ! */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lookup);
}
