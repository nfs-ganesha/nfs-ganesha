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
 * \file    fsal_xattrs.c
 * \brief   Extended attributes functions.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

#include <string.h>
#include <alloca.h>
#include <fcntl.h>

/**
 * Get the attributes of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_cookie xattr's cookie (as returned by listxattrs).
 * \param p_attrs xattr's attributes.
 */
fsal_status_t CEPHFSAL_GetXAttrAttrs(fsal_handle_t * exthandle,
                                     fsal_op_context_t * extcontext,
                                     unsigned int xattr_id,
                                     fsal_attrib_list_t * attrs)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int rc = 0;
  fsal_status_t status;
  fsal_attrib_list_t file_attrs;
  fsal_attrib_mask_t na_supported=
    (FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE  | FSAL_ATTR_SIZE  |
     FSAL_ATTR_FSID     | FSAL_ATTR_MODE  | FSAL_ATTR_OWNER |
     FSAL_ATTR_GROUP    | FSAL_ATTR_CHGTIME);
  fsal_attrib_mask_t fa_wanted=
    (FSAL_ATTR_FSID  | FSAL_ATTR_MODE  | FSAL_ATTR_OWNER |
     FSAL_ATTR_GROUP | FSAL_ATTR_CHGTIME);
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  int len;

  /* sanity checks */
  if(!handle || !context || !attrs)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrAttrs);

  /* Check that asked attributes are supported */

  if(attrs->asked_attributes & ~(na_supported))
    Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_GetXAttrAttrs);

  /* object attributes we want to retrieve from parent */
  file_attrs.asked_attributes = fa_wanted;

  /* don't retrieve attributes not asked */

  file_attrs.asked_attributes &= attrs->asked_attributes;

  status = CEPHFSAL_getattrs(exthandle, extcontext, attrs);

  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_GetXAttrAttrs);

  /* We support a subset of the attributes of files */

  if (attrs->asked_attributes & FSAL_ATTR_SUPPATTR)
    file_attrs.supported_attributes=na_supported;

  /* Attributes are attributes */

  if (attrs->asked_attributes & FSAL_ATTR_TYPE)
    file_attrs.type = FSAL_TYPE_XATTR;

  /* Attributes are not executable */

  if (attrs->asked_attributes & FSAL_ATTR_MODE)
    file_attrs.mode = file_attrs.mode & ~(S_IXUSR | S_IXGRP | S_IXOTH);

  /* Length */

  if (attrs->asked_attributes & FSAL_ATTR_SIZE)
    {
        len = ceph_ll_lenxattr_by_idx(context->export_context->cmount,
                                      VINODE(handle), xattr_id, uid, gid);
        if (len < 0)
            Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_GetXAttrAttrs);
        file_attrs.filesize = len;
    }

  *attrs = file_attrs;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrAttrs);
}                               /* FSAL_GetXAttrAttrs */

/**
 * Retrieves the list of extended attributes for an object in the filesystem.
 *
 * \param p_objecthandle Handle of the object we want to get extended attributes.
 * \param cookie index of the next entry to be returned.
 * \param p_context pointer to the current security context.
 * \param xattrs_tab a table for storing extended attributes list to.
 * \param xattrs_tabsize the maximum number of xattr entries that xattrs_tab
 *            can contain.
 * \param p_nb_returned the number of xattr entries actually stored in xattrs_tab.
 * \param end_of_list this boolean indicates that the end of xattrs list has been reached.
 */
fsal_status_t CEPHFSAL_ListXAttrs(fsal_handle_t * exthandle,
                                  unsigned int cookie,
                                  fsal_op_context_t * extcontext,
                                  fsal_xattrent_t * xattrs_tab,
                                  unsigned int xattrs_tabsize,
                                  unsigned int *p_nb_returned,
                                  int *end_of_list)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  fsal_status_t status;
  fsal_attrib_list_t attr_attrs;
  fsal_attrib_mask_t fa_wanted=
    (FSAL_ATTR_FSID  | FSAL_ATTR_MODE  | FSAL_ATTR_OWNER |
     FSAL_ATTR_GROUP | FSAL_ATTR_CHGTIME);
  fsal_attrib_mask_t na_supported=
    (FSAL_ATTR_SUPPATTR | FSAL_ATTR_TYPE  | FSAL_ATTR_SIZE  |
     FSAL_ATTR_FSID     | FSAL_ATTR_MODE  | FSAL_ATTR_OWNER |
     FSAL_ATTR_GROUP    | FSAL_ATTR_CHGTIME);
  char* names;
  int rc;
  int lcookie=cookie;
  int uid=FSAL_OP_CONTEXT_TO_UID(context);
  int gid=FSAL_OP_CONTEXT_TO_GID(context);
  int idx;
  char *ptr;

  names = alloca(sizeof(fsal_xattrent_t) * xattrs_tabsize);

  ptr = names;

  /* sanity checks */
  if(!handle|| !context || !xattrs_tab || !p_nb_returned || !end_of_list)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_ListXAttrs);

  /* Retrieve attributes that should be inherited from the file */

  attr_attrs.asked_attributes = fa_wanted;
  status = CEPHFSAL_getattrs(exthandle, extcontext, &attr_attrs);
  if(FSAL_IS_ERROR(status))
    ReturnStatus(status, INDEX_FSAL_GetXAttrAttrs);

  /* We support a subset of the attributes of files */

  attr_attrs.supported_attributes = na_supported;

  /* Attributes are attributes */

  attr_attrs.type = FSAL_TYPE_XATTR;

  /* Attributes are not executable */

  attr_attrs.mode = attr_attrs.mode & ~(S_IXUSR | S_IXGRP | S_IXOTH);

  rc = ceph_ll_listxattr_chunks(context->export_context->cmount,
                                VINODE(handle), names,
                                sizeof(fsal_xattrent_t) * xattrs_tabsize,
                                &lcookie, end_of_list, uid, gid);

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_open);

  for(idx=0; idx <= rc && idx <= xattrs_tabsize; idx++)
    {
      xattrs_tab[idx].xattr_id=idx;
      FSAL_str2name(ptr, FSAL_MAX_NAME_LEN,
                    &(xattrs_tab[idx].xattr_name));
      xattrs_tab[idx].xattr_cookie = idx+1;
      ptr += strlen(ptr);
      attr_attrs.filesize = ((uint64_t) *ptr);
      ptr += sizeof(uint64_t);
      xattrs_tab[idx].attributes = attr_attrs;
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_ListXAttrs);
}

/**
 * Get the value of an extended attribute from its index.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t CEPHFSAL_GetXAttrValueById(fsal_handle_t * exthandle,
                                         unsigned int xattr_id,
                                         fsal_op_context_t * extcontext,
                                         caddr_t buffer_addr,
                                         size_t buffer_size,
                                         size_t * p_output_size)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) context;
  int len = 0;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks */
  if(!handle || !context || !p_output_size || !buffer_addr)
    ReturnCode(ERR_FSAL_FAULT, 0);

  ceph_ll_getxattr_by_idx(context->export_context->cmount,
                          VINODE(handle), xattr_id, buffer_addr,
                          buffer_size, uid, gid);

  if (len < 0)
    ReturnCode(posix2fsal_error(len), 0);

  *p_output_size=len;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
}

/**
 * Get the index of an xattr based on its name
 *
 *   \param p_objecthandle Handle of the object you want to get attribute for.
 *   \param xattr_name the name of the attribute to be read.
 *   \param pxattr_id found xattr_id
 *
 *   \return ERR_FSAL_NO_ERROR if xattr_name exists, ERR_FSAL_NOENT otherwise
 */
fsal_status_t CEPHFSAL_GetXAttrIdByName(fsal_handle_t * exthandle,
                                        const fsal_name_t * xattr_name,
                                        fsal_op_context_t * extcontext,
                                        unsigned int *pxattr_id)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int index;
  char name[FSAL_MAX_NAME_LEN+1];
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  FSAL_name2str((fsal_name_t *) xattr_name, name, FSAL_MAX_NAME_LEN);

  /* sanity checks */
  if(!handle || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  index = ceph_ll_getxattridx(context->export_context->cmount,
                              VINODE(handle), name,
                              uid, gid);
  if (index < 0)
    ReturnCode(posix2fsal_error(index), 0);

  else
    *pxattr_id = index;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
}

/**
 * Get the value of an extended attribute from its name.
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param xattr_name the name of the attribute to be read.
 * \param p_context pointer to the current security context.
 * \param buffer_addr address of the buffer where the xattr value is to be stored.
 * \param buffer_size size of the buffer where the xattr value is to be stored.
 * \param p_output_size size of the data actually stored into the buffer.
 */
fsal_status_t CEPHFSAL_GetXAttrValueByName(fsal_handle_t * exthandle,
                                           const fsal_name_t * xattr_name,
                                           fsal_op_context_t * extcontext,
                                           caddr_t buffer_addr,
                                           size_t buffer_size,
                                           size_t * p_output_size)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int len;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  char name[FSAL_MAX_NAME_LEN+1];

  FSAL_name2str((fsal_name_t*) xattr_name, name, FSAL_MAX_NAME_LEN);

  /* sanity checks */
  if(!handle || !context || !p_output_size || !buffer_addr || !xattr_name)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_GetXAttrValue);

  len = ceph_ll_getxattr(context->export_context->cmount,
                         VINODE(handle), name,
                         buffer_addr, buffer_size,
                         uid, gid);
  if (len < 0)
    ReturnCode(posix2fsal_error(len), 0);


  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_GetXAttrValue);
}

fsal_status_t CEPHFSAL_SetXAttrValue(fsal_handle_t * exthandle,
                                     const fsal_name_t * xattr_name,
                                     fsal_op_context_t * extcontext,
                                     caddr_t buffer_addr,
                                     size_t buffer_size,
                                     int create)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int rc;
  char name[FSAL_MAX_NAME_LEN+1];
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  FSAL_name2str((fsal_name_t *) xattr_name, name, FSAL_MAX_NAME_LEN);

  rc = ceph_ll_setxattr(context->export_context->cmount,
                        VINODE(handle), name,
                        buffer_addr, buffer_size,
                        create ? O_CREAT : 0,
                        uid, gid);


  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_SetXAttrValue);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_SetXAttrValue);
}

fsal_status_t CEPHFSAL_SetXAttrValueById(fsal_handle_t * exthandle,
                                         unsigned int xattr_id,
                                         fsal_op_context_t * extcontext,
                                         caddr_t buffer_addr,
                                         size_t buffer_size)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  ceph_ll_setxattr_by_idx(context->export_context->cmount,
                          VINODE(handle), xattr_id,
                          buffer_addr, buffer_size, 0, uid, gid);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  Removes a xattr by Id
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_id xattr's id
 */
fsal_status_t CEPHFSAL_RemoveXAttrById(fsal_handle_t * exthandle,
                                       fsal_op_context_t * extcontext,
                                       unsigned int xattr_id)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  int rc;

  rc = ceph_ll_removexattr_by_idx(context->export_context->cmount,
                                  VINODE(handle), xattr_id, uid, gid);
  if(rc < 0)
    ReturnCode(posix2fsal_error(rc), 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

/**
 *  Removes a xattr by Name
 *
 * \param p_objecthandle Handle of the object you want to get attribute for.
 * \param p_context pointer to the current security context.
 * \param xattr_name xattr's name
 */
fsal_status_t CEPHFSAL_RemoveXAttrByName(fsal_handle_t * exthandle,
                                         fsal_op_context_t * extcontext,
                                         const fsal_name_t * xattr_name)
{
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int rc;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);
  char name[FSAL_MAX_NAME_LEN+1];

  FSAL_name2str((fsal_name_t *)xattr_name, name, FSAL_MAX_NAME_LEN);

  rc = ceph_ll_removexattr(context->export_context->cmount,
                           VINODE(handle), name, uid, gid);

  if(rc < 0)
    ReturnCode(posix2fsal_error(rc), 0);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}
