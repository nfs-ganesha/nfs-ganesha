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
#include "gpfs_methods.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/fsuid.h>

/**
 * FSAL_truncate:
 * Modify the data length of a regular file.
 *
 * \param export (input):
 *        For use of mount fd
 * \param p_filehandle (input):
 *        Handle of the file is to be truncated.
 * \param p_contexd (input):
 *        Authentication context for the operation (user,...).
 * \param length (input):
 *        The new data length for the file.
 * \param p_object_attributes (optionnal input/output):
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

fsal_status_t GPFSFSAL_truncate(struct fsal_export *export,            /* IN */
                           struct gpfs_file_handle * p_filehandle,     /* IN */
                           const struct req_op_context * p_context,    /* IN */
                           size_t length,                             /* IN */
                           struct attrlist * p_object_attributes)  /* IN/OUT */
{
  int fsuid, fsgid;
  fsal_status_t st;
  int mount_fd;

  /* sanity checks.
   * note : object_attributes is optional.
   */
  if(!p_filehandle || !p_context || !export)
    return fsalstat(ERR_FSAL_FAULT, 0);

  mount_fd = gpfs_get_root_fd(export);

  fsuid = setfsuid(p_context->creds->caller_uid);
  fsgid = setfsgid(p_context->creds->caller_gid);

  st = fsal_trucate_by_handle(mount_fd, p_context, p_filehandle, length);
  if (FSAL_IS_ERROR(st)) {
    setfsuid(fsuid);
    setfsgid(fsgid);
    return(st);
  }
  /* Optionally retrieve attributes */
  if(p_object_attributes)
    {
      fsal_status_t st;

      st = GPFSFSAL_getattrs(export, p_context, p_filehandle, p_object_attributes);

      if(FSAL_IS_ERROR(st))
        {
          FSAL_CLEAR_MASK(p_object_attributes->mask);
          FSAL_SET_MASK(p_object_attributes->mask, ATTR_RDATTR_ERR);
        }
    }
  fsuid = setfsuid(fsuid);
  fsgid = setfsgid(fsgid);

  return fsalstat(ERR_FSAL_NO_ERROR, 0);

}
