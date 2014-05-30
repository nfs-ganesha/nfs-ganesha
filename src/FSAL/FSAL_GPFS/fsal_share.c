/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
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
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * GPFSFSAL_share_op:
 */
fsal_status_t GPFSFSAL_share_op( fsal_file_t        * p_file_descriptor, /* IN */
                                 fsal_handle_t      * p_filehandle,      /* IN */
                                 fsal_op_context_t  * p_context,         /* IN */
                                 void               * p_owner,           /* IN */
                                 fsal_share_param_t   request_share)     /* IN */
{
  int rc = 0;
  int dirfd = 0;
  struct share_reserve_arg share_arg;
  gpfsfsal_file_t * pfd = NULL;

  if(p_file_descriptor == NULL)
    {
      LogDebug(COMPONENT_FSAL, "p_file_descriptor arg is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_share_op);
    }

  if(p_context == NULL)
    {
      LogDebug(COMPONENT_FSAL, "p_context arg is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_share_op);
    }

  LogFullDebug(COMPONENT_FSAL,
               "Share reservation: access:%u deny:%u owner:%p",
               request_share.share_access, request_share.share_deny, p_owner);

  dirfd = ((gpfsfsal_op_context_t *)p_context)->export_context->mount_root_fd;
  pfd = (gpfsfsal_file_t *) p_file_descriptor;

  share_arg.mountdirfd = dirfd;
  share_arg.openfd = pfd->fd;
  share_arg.share_access = request_share.share_access;
  share_arg.share_deny = request_share.share_deny;

  rc = gpfs_ganesha(OPENHANDLE_SHARE_RESERVE, &share_arg);

  if(rc < 0)
    {
      if (errno == EUNATCH)
        LogFatal(COMPONENT_FSAL, "GPFS Returned EUNATCH");

      LogDebug(COMPONENT_FSAL,
               "gpfs_ganesha: OPENHANDLE_SHARE_RESERVE returned error, rc=%d, errno=%d",
               rc, errno);
      ReturnCode(posix2fsal_error(errno), errno);
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_share_op);
}
