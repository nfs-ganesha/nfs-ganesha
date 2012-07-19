// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_truncate.c
// Description: FSAL truncate operations implementation
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
 * \file    fsal_truncate.c
 * \author  $Author: leibovic $
 * \date    $Date: 2005/07/29 09:39:05 $
 * \version $Revision: 1.4 $
 * \brief   Truncate function.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

#include <unistd.h>
#include <sys/types.h>

#include "pt_ganesha.h"

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
 *        - Another error code if an error occurred.
 */

fsal_status_t 
PTFSAL_truncate(fsal_handle_t * p_filehandle,       /* IN */
                fsal_op_context_t * p_context,      /* IN */
                fsal_size_t length,                 /* IN */
                fsal_file_t * file_descriptor,      /* IN */
                fsal_attrib_list_t * p_object_attributes    /* [ IN/OUT ] */)
{

  int rc=0, errsv;
  int fd = -1;
  fsal_status_t st;
  fsal_file_t file_desc;
  ptfsal_op_context_t     * fsi_op_context     =
    (ptfsal_op_context_t *)p_context;
  ptfsal_export_context_t * fsi_export_context =
    fsi_op_context->export_context;

  FSI_TRACE(FSI_DEBUG,"Truncate called, length=%d",length);
  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_truncate);

  ptfsal_print_handle(p_filehandle);
  /* Check to see if we already have fd */
  if (file_descriptor && file_descriptor->fd != 0)
  {
    fd = file_descriptor->fd;
    FSI_TRACE(FSI_DEBUG, "Truncating with fd=%d, truncate length=%d",
              fd,length );
    rc = ptfsal_ftruncate(p_context, fd, length);
    errsv = errno;
  }

  /* either the fd passed in was 0, or invalid */
  if (rc || fd == -1)
  {
    /* Get an fd since we dont have one */
    st = fsal_internal_handle2fd(p_context, p_filehandle, &fd, O_RDWR);

    if (FSAL_IS_ERROR(st))
      ReturnStatus(st, INDEX_FSAL_truncate);

    /* Executes the PT truncate operation */
    FSI_TRACE(FSI_DEBUG, 
              "Truncating with POSIX truncate fd=%d, truncate length=%d",
              fd,length );
    rc = ptfsal_ftruncate(p_context, fd, length);
    errsv = errno;
    /* convert return code */
    if(rc) {
      if(errsv == ENOENT)
        Return(ERR_FSAL_STALE, errsv, INDEX_FSAL_truncate);
      else
        Return(posix2fsal_error(errsv), errsv, INDEX_FSAL_truncate);
    }

    /* Close the fd we opened */
    file_desc.fd = fd;
    file_desc.export_id = fsi_export_context->pt_export_id; 
    file_desc.uid = fsi_op_context->credential.user;
    file_desc.gid = fsi_op_context->credential.group;
  }

  /* Optionally retrieve attributes */
  if(p_object_attributes)
  {

    fsal_status_t st;

    st = PTFSAL_getattrs(p_filehandle, p_context, p_object_attributes);

    if(FSAL_IS_ERROR(st))
    {
      FSAL_CLEAR_MASK(p_object_attributes->asked_attributes);
      FSAL_SET_MASK(p_object_attributes->asked_attributes, 
                    FSAL_ATTR_RDATTR_ERR);
    }

  }
 
  /* No error occurred */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_truncate);

}
