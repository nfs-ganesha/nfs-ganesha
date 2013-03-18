/*
 *
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
 * ---------------------------------------
 */

/**
 * \file    fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:41:01 $
 * \version $Revision: 1.72 $
 * \brief   File System Abstraction Layer interface.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* fsal_types contains constants and type definitions for FSAL */
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"

/**
 *
 * MFSL_getattrs : performs getattr but takes care of the asynchronous logic.
 *
 * Performs getattr but takes care of the asynchronous logic.
 *
 * @param filehandle         [IN]    mfsl object related to the object
 * @param p_context          [IN]    associated fsal context
 * @param p_mfsl_context     [INOUT] associated mfsl context
 * @param object_attributes  [INOUT] attributes for the object
 *
 * @return the same as FSAL_getattrs
 */
fsal_status_t MFSL_getattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  fsal_status_t fsal_status;
  mfsl_object_specific_data_t *pasyncdata;

  if(mfsl_async_get_specdata(filehandle, &pasyncdata))
    {
      /* Is the object deleted ? */
      if(pasyncdata->deleted == TRUE)
        MFSL_return(ERR_FSAL_NOENT, ENOENT);

      /* merge the attributes to the asynchronous attributes */
      if((object_attributes->asked_attributes & FSAL_ATTR_SIZE) ||
         (object_attributes->asked_attributes & FSAL_ATTR_SPACEUSED))
        {
          /* Operation on a non data cached file */
          object_attributes->filesize = pasyncdata->async_attr.filesize;
          object_attributes->spaceused = pasyncdata->async_attr.spaceused;
        }

      if(object_attributes->asked_attributes &
         (FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
        {
          if(object_attributes->asked_attributes & FSAL_ATTR_MODE)
            object_attributes->mode = pasyncdata->async_attr.mode;

          if(object_attributes->asked_attributes & FSAL_ATTR_OWNER)
            object_attributes->owner = pasyncdata->async_attr.owner;

          if(object_attributes->asked_attributes & FSAL_ATTR_GROUP)
            object_attributes->group = pasyncdata->async_attr.group;
        }

      if(object_attributes->asked_attributes & (FSAL_ATTR_ATIME | FSAL_ATTR_MTIME))
        {
          if(object_attributes->asked_attributes & FSAL_ATTR_ATIME)
            object_attributes->atime = pasyncdata->async_attr.atime;

          if(object_attributes->asked_attributes & FSAL_ATTR_MTIME)
            object_attributes->mtime = pasyncdata->async_attr.mtime;
        }

      /* Regular exit */
      MFSL_return(ERR_FSAL_NO_ERROR, 0);
    }
  else
    {
      return FSAL_getattrs(&filehandle->handle, p_context, object_attributes);
    }
}                               /* MFSL_getattrs */
