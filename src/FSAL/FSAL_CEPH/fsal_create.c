/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2010, The Linux Box Corporation
 * Contributor : Adam C. Emerson <aemerson@linuxbox.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
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
 * \file    fsal_create.c
 * \brief   Filesystem objects creation functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_create:
 * Create a regular file.
 *
 * \param extparent (input):
 *        Handle of the parent directory where the file is to be created.
 * \param filename (input):
 *        Pointer to the name of the file to be created.
 * \param context (input):
 *        Authentication context for the operation (user, export...).
 * \param accessmode (input):
 *        Mode for the file to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created file.
 * \param object_attributes (optionnal input/output):
 *        The postop attributes of the created file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        Can be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
fsal_status_t CEPHFSAL_create(fsal_handle_t * extparent,
                              fsal_name_t * filename,
                              fsal_op_context_t * extcontext,
                              fsal_accessmode_t accessmode,
                              fsal_handle_t * object_handle,
                              fsal_attrib_list_t * object_attributes)
{
  int rc;
  mode_t mode;
  struct Fh *fd;
  struct stat st;
  char strname[FSAL_MAX_NAME_LEN+1];
  cephfsal_handle_t* parent = (cephfsal_handle_t*) extparent;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  struct ceph_mount_info *cmount = context->export_context->cmount;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent || !context || !object_handle || !filename)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_create);

  memset(object_handle, 0, sizeof(cephfsal_handle_t));

  mode = fsal2unix_mode(accessmode);
  mode = mode & ~global_fs_info.umask;

  FSAL_name2str(filename, strname, FSAL_MAX_NAME_LEN);

  TakeTokenFSCall();

  rc = ceph_ll_create(cmount, VINODE(parent), strname,
                      mode, 0, &fd, &st, uid, gid);
  ceph_ll_close(cmount, fd);

  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  rc = stat2fsal_fh(cmount,
                    &st,
                    (cephfsal_handle_t*) object_handle);

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  if(object_attributes)
    {
      fsal_status_t status;
      status = posix2fsal_attributes(&st, object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_create);
}

/**
 * FSAL_mkdir:
 * Create a directory.
 *
 * \param extparent (input):
 *        Handle of the parent directory where
 *        the subdirectory is to be created.
 * \param dirname (input):
 *        Pointer to the name of the directory to be created.
 * \param context (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param object_handle (output):
 *        Pointer to the handle of the created directory.
 * \param object_attributes (optionnal input/output):
 *        The attributes of the created directory.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the object_attributes->asked_attributes field.
 */
fsal_status_t CEPHFSAL_mkdir(fsal_handle_t * extparent,
                             fsal_name_t * dirname,
                             fsal_op_context_t * extcontext,
                             fsal_accessmode_t accessmode,
                             fsal_handle_t * object_handle,
                             fsal_attrib_list_t * object_attributes)
{
  int rc;
  mode_t mode;
  struct stat st;
  char strname[FSAL_MAX_NAME_LEN+1];
  cephfsal_handle_t* parent = (cephfsal_handle_t*) extparent;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  struct ceph_mount_info *cmount = context->export_context->cmount;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!parent || !context || !object_handle || !dirname)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mkdir);

  memset(object_handle, 0, sizeof(cephfsal_handle_t));

  mode = fsal2unix_mode(accessmode);
  mode = mode & ~global_fs_info.umask;

  FSAL_name2str(dirname, strname, FSAL_MAX_NAME_LEN);
  TakeTokenFSCall();

  rc = ceph_ll_mkdir(cmount, VINODE(parent), dirname->name,
                     mode, &st, uid, gid);

  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_mkdir);

  rc = stat2fsal_fh(cmount,
                    &st,
                    (cephfsal_handle_t*) object_handle);

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  if(object_attributes)
    {
      fsal_status_t status;
      status = posix2fsal_attributes(&st, object_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes,
                        FSAL_ATTR_RDATTR_ERR);
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mkdir);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_mkdir);
}

/**
 * FSAL_link:
 * Create a hardlink.
 *
 * \param exttarget (input):
 *        Handle of the target object.
 * \param extdir (input):
 *        Pointer to the directory handle where
 *        the hardlink is to be created.
 * \param link_name (input):
 *        Pointer to the name of the hardlink to be created.
 * \param extcontext (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (input):
 *        Mode for the directory to be created.
 *        (the umask defined into the FSAL configuration file
 *        will be applied on it).
 * \param attributes (optionnal input/output):
 *        The post_operation attributes of the linked object.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (target or dir does not address an existing object)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_EXIST, ERR_FSAL_IO, ...
 *
 *        NB: if getting postop attributes failed,
 *        the function does not return an error
 *        but the FSAL_ATTR_RDATTR_ERR bit is set in
 *        the attributes->asked_attributes field.
 */
fsal_status_t CEPHFSAL_link(fsal_handle_t * exttarget,
                            fsal_handle_t * extdir,
                            fsal_name_t * link_name,
                            fsal_op_context_t * extcontext,
                            fsal_attrib_list_t * attributes)
{
  int rc;
  struct stat st;
  char strname[FSAL_MAX_NAME_LEN+1];
  cephfsal_handle_t* target = (cephfsal_handle_t*) exttarget;
  cephfsal_handle_t* dir = (cephfsal_handle_t*) extdir;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) context;
  struct ceph_mount_info *cmount = context->export_context->cmount;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks.
   * note : attributes is optional.
   */
  if(!target || !dir || !context || !link_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_link);

  memset(target, 0, sizeof(cephfsal_handle_t));

  /* Tests if hardlinking is allowed by configuration. */

  if(!global_fs_info.link_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_link);

  FSAL_name2str(link_name, strname, FSAL_MAX_NAME_LEN);

  TakeTokenFSCall();
  rc = ceph_ll_link(cmount, VINODE(target), VINODE(dir), strname, &st,
                    uid, gid);
  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_link);

  if(attributes)
    {
      fsal_status_t status;
      status = posix2fsal_attributes(&st, attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(attributes->asked_attributes);
          FSAL_SET_MASK(attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_link);
}

/**
 * FSAL_mknode:
 * Create a special object in the filesystem.
 * Not supported in upper layers in this GANESHA's version.
 *
 * \return ERR_FSAL_NOTSUPP.
 */
fsal_status_t CEPHFSAL_mknode(fsal_handle_t * parent,
                              fsal_name_t * node_name,
                              fsal_op_context_t * context,
                              fsal_accessmode_t accessmode,
                              fsal_nodetype_t nodetype,
                              fsal_dev_t * dev,
                              fsal_handle_t * object_handle,
                              fsal_attrib_list_t * node_attributes)
{
  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parent|| !context || !nodetype || !dev || !node_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_mknode);

  /* Not implemented */
  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_mknode);

}
