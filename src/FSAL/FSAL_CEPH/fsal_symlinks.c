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
 * \file    fsal_symlinks.c
 * \brief   symlinks operations.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <string.h>

/**
 * FSAL_readlink:
 * Read the content of a symbolic link.
 *
 * \param linkhandle (input):
 *        Handle of the link to be read.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param p_link_content (output):
 *        Pointer to an fsal path structure where
 *        the link content is to be stored..
 * \param link_attributes (optionnal input/output): 
 *        The post operation attributes of the symlink link.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (linkhandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (linkhandle does not address a symbolic link)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 * */

fsal_status_t CEPHFSAL_readlink(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_path_t * link_content,
                                fsal_attrib_list_t * link_attributes)
{
  int rc;
  fsal_status_t st;
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  char strcontent[FSAL_MAX_PATH_LEN];
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  struct ceph_mount_info *cmount = context->export_context->cmount;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!handle || !context || !link_content)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_readlink);

  rc = ceph_ll_readlink(cmount,
                        VINODE(handle), (char**) &strcontent, uid, gid);

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_open);

  st = FSAL_str2path(strcontent, FSAL_MAX_PATH_LEN, link_content);

  if(FSAL_IS_ERROR(st))
    Return(st.major, st.minor, INDEX_FSAL_readlink);

  /* retrieves object attributes, if asked */

  if(link_attributes)
    {
      fsal_status_t status =
        CEPHFSAL_getattrs(exthandle, extcontext, link_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(link_attributes->asked_attributes);
          FSAL_SET_MASK(link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }

    }
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_readlink);
}

/**
 * FSAL_symlink:
 * Create a symbolic link.
 *
 * \param parent_directory_handle (input):
 *        Handle of the parent directory where the link is to be created.
 * \param p_linkname (input):
 *        Name of the link to be created.
 * \param p_linkcontent (input):
 *        Content of the link to be created.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param accessmode (ignored input):
 *        Mode of the link to be created.
 *        It has no sense in HPSS nor UNIX filesystems.
 * \param link_handle (output):
 *        Pointer to the handle of the created symlink.
 * \param link_attributes (optionnal input/output): 
 *        Attributes of the newly created symlink.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (parent_directory_handle does not address an existing object)
 *        - ERR_FSAL_NOTDIR       (parent_directory_handle does not address a directory)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t CEPHFSAL_symlink(fsal_handle_t * extparent,
                               fsal_name_t * linkname,
                               fsal_path_t * linkcontent,
                               fsal_op_context_t * extcontext,
                               fsal_accessmode_t accessmode,
                               fsal_handle_t * extlink,
                               fsal_attrib_list_t * link_attributes)
{
  int rc;
  struct stat st;
  cephfsal_handle_t* parent = (cephfsal_handle_t*) extparent;
  cephfsal_handle_t* link = (cephfsal_handle_t*) extlink;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  char strpath[FSAL_MAX_PATH_LEN];
  char strname[FSAL_MAX_NAME_LEN+1];
  struct ceph_mount_info *cmount = context->export_context->cmount;

  /* sanity checks.
   * note : link_attributes is optional.
   */
  if(!parent || !context || !link || !linkname || !linkcontent)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_symlink);

  FSAL_path2str(linkcontent, strpath, FSAL_MAX_PATH_LEN);
  FSAL_name2str(linkname, strname, FSAL_MAX_NAME_LEN);

  /* Tests if symlinking is allowed by configuration. */

  if(!global_fs_info.symlink_support)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_symlink);

  rc = ceph_ll_symlink(cmount,
                       VINODE(parent), strname, strpath, &st, uid, gid);
  if (rc)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_symlink);

  rc = stat2fsal_fh(cmount, &st, link);
  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  if(link_attributes)
    {
      /* convert attributes */
      fsal_status_t status = posix2fsal_attributes(&st, link_attributes);
      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(link_attributes->asked_attributes);
          FSAL_SET_MASK(link_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
          Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
        }
    }

  /* OK */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_symlink);
}

