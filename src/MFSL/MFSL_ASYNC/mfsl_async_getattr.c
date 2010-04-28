/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
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
#include "stuff_alloc.h"

#ifndef _USE_SWIG

/**
 *
 * MFSAL_getattrs_check_perms : Checks authorization to perform an asynchronous getattr.
 *
 * Checks authorization to perform an asynchronous getattr.
 *
 * @param target_handle      [IN]    mfsl object to be operated on.
 * @param pspecdata          [IN]    object's specific data
 * @param p_context          [IN]    associated fsal context
 * @param object_attributes  [INOUT] attributes for the objet
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_getattrs_check_perms(mfsl_object_t * filehandle,    /* IN */
                                         mfsl_object_specific_data_t * pspecdata,       /* IN */
                                         fsal_op_context_t * p_context, /* IN */
                                         mfsl_context_t * p_mfsl_context,       /* IN */
                                         fsal_attrib_list_t * object_attributes /* IN */ )
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_test_access(p_context, FSAL_R_OK, object_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_setattr_check_perms */

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
      P(filehandle->lock);
      fsal_status = MFSAL_getattrs_check_perms(filehandle,
                                               pasyncdata,
                                               p_context,
                                               p_mfsl_context, object_attributes);
      V(filehandle->lock);

      if(FSAL_IS_ERROR(fsal_status))
        return fsal_status;

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

#endif                          /* ! _USE_SWIG */
