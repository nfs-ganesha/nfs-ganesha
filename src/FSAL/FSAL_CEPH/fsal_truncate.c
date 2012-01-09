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
 * \file    fsal_truncate.c
 * \brief   Truncate function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * FSAL_truncate:
 * Modify the data length of a regular file.
 *
 * \param filehandle (input):
 *        Handle of the file is to be truncated.
 * \param cred (input):
 *        Authentication context for the operation (user,...).
 * \param length (input):
 *        The new data length for the file.
 * \param object_attributes (optionnal input/output):
 *        The post operation attributes of the file.
 *        As input, it defines the attributes that the caller
 *        wants to retrieve (by positioning flags into this structure)
 *        and the output is built considering this input
 *        (it fills the structure according to the flags it contains).
 *        May be NULL.
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_STALE        (filehandle does not address an existing object)
 *        - ERR_FSAL_INVAL        (filehandle does not address a regular file)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */

fsal_status_t CEPHFSAL_truncate(fsal_handle_t * exthandle,
                                fsal_op_context_t * extcontext,
                                fsal_size_t length,
                                fsal_file_t * file_descriptor,
                                fsal_attrib_list_t * object_attributes)
{
  int rc;
  fsal_status_t status;
  cephfsal_handle_t* handle = (cephfsal_handle_t*) exthandle;
  cephfsal_op_context_t* context = (cephfsal_op_context_t*) extcontext;
  int uid = FSAL_OP_CONTEXT_TO_UID(context);
  int gid = FSAL_OP_CONTEXT_TO_GID(context);

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!handle || !context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  TakeTokenFSCall();
  rc = ceph_ll_truncate(context->export_context->cmount, VINODE(handle),
                        length, uid, gid);
  ReleaseTokenFSCall();

  if (rc < 0)
    Return(posix2fsal_error(rc), 0, INDEX_FSAL_create);

  if(object_attributes)
    {
      status = CEPHFSAL_getattrs(exthandle, extcontext, object_attributes);

      if(FSAL_IS_ERROR(status))
        {
          FSAL_CLEAR_MASK(object_attributes->asked_attributes);
          FSAL_SET_MASK(object_attributes->asked_attributes, FSAL_ATTR_RDATTR_ERR);
        }
    }

  /* No error occured */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);
}
