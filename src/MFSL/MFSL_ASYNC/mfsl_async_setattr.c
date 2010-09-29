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
#include "stuff_alloc.h"

#ifndef _USE_SWIG

extern mfsl_parameter_t mfsl_param;

/**
 *
 * MFSL_setattr_async_op: Callback for asynchronous setattr. 
 *
 * Callback for asynchronous setattr. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_setattrs.
 */
fsal_status_t MFSL_setattr_async_op(mfsl_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  LogDebug(COMPONENT_MFSL, "Making asynchronous FSAL_setattrs for async op %p",
                  popasyncdesc);

  P(popasyncdesc->op_args.setattr.pmobject->lock);
  fsal_status = FSAL_setattrs(&(popasyncdesc->op_args.setattr.pmobject->handle),
                              &popasyncdesc->fsal_op_context,
                              &popasyncdesc->op_args.setattr.attr,
                              &popasyncdesc->op_res.setattr.attr);
  V(popasyncdesc->op_args.setattr.pmobject->lock);

  return fsal_status;
}                               /* MFSL_setattr_async_op */

/**
 *
 * MFSL_setattrs_check_perms : Checks authorization to perform an asynchronous setattr.
 *
 * Checks authorization to perform an asynchronous setattr.
 *
 * @param filehandle        [IN]    mfsl object to be operated on.
 * @param pspecdata         [INOUT] mfsl object associated specific data
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param attrib_set        [IN]    attributes to be set 
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSL_setattrs_check_perms(mfsl_object_t * filehandle,     /* IN */
                                        mfsl_object_specific_data_t * pspecdata,        /* IN */
                                        fsal_op_context_t * p_context,  /* IN */
                                        mfsl_context_t * p_mfsl_context,        /* IN */
                                        fsal_attrib_list_t * attrib_set /* IN */ )
{
  fsal_status_t fsal_status;

  /* Root is the only one that can chown or chgrp */
  if(attrib_set->asked_attributes & (FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(p_context->user_credential.user != 0)
        MFSL_return(ERR_FSAL_ACCESS, 0);
    }

  fsal_status = FSAL_setattr_access(p_context, attrib_set, &pspecdata->async_attr);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_setattr_check_perms */

/**
 *
 * MFSL_setattrs : posts an asynchronous setattr and sets the cached attributes in return.
 *
 * Posts an asynchronous setattr and sets the cached attributes in return.
 * If the object is not asynchronous, then the content of object attributes will be used to populate it.
 *
 * @param filehandle        [IN]    mfsl object to be operated on.
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param attrib_set        [IN]    attributes to be set 
 * @param object_attributes [INOUT] resulting attributes
 *
 * @return the same as FSAL_setattrs
 */
fsal_status_t MFSL_setattrs(mfsl_object_t * filehandle, /* IN */
                            fsal_op_context_t * p_context,      /* IN */
                            mfsl_context_t * p_mfsl_context,    /* IN */
                            fsal_attrib_list_t * attrib_set,    /* IN */
                            fsal_attrib_list_t * object_attributes      /* [ IN/OUT ] */
    )
{
  fsal_status_t fsal_status;
  mfsl_async_op_desc_t *pasyncopdesc = NULL;
  mfsl_object_specific_data_t *pasyncdata = NULL;

  P(p_mfsl_context->lock);

  GetFromPool(pasyncopdesc, &p_mfsl_context->pool_async_op, mfsl_async_op_desc_t);

  V(p_mfsl_context->lock);

  if(pasyncopdesc == NULL)
    MFSL_return(ERR_FSAL_INVAL, 0);

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogMajor(COMPONENT_MFSL, "MFSL_setattrs: cannot get time of day... exiting");
      exit(1);
    }

  /* Is the object asynchronous ? */
  if(!mfsl_async_get_specdata(filehandle, &pasyncdata))
    {
      /* Not yet asynchronous object */
      P(p_mfsl_context->lock);

      GetFromPool(pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

      V(p_mfsl_context->lock);

      /* In this case use object_attributes parameter to initiate asynchronous object */
      pasyncdata->async_attr = *object_attributes;
    }

  fsal_status =
      MFSL_setattrs_check_perms(filehandle, pasyncdata, p_context, p_mfsl_context,
                                attrib_set);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  LogDebug(COMPONENT_MFSL,  "Creating asyncop %p",
                    pasyncopdesc);

  pasyncopdesc->op_type = MFSL_ASYNC_OP_SETATTR;
  pasyncopdesc->op_mobject = filehandle;
  pasyncopdesc->op_args.setattr.pmobject = filehandle;
  pasyncopdesc->op_args.setattr.attr = *attrib_set;
  pasyncopdesc->op_res.setattr.attr = *attrib_set;

  pasyncopdesc->op_func = MFSL_setattr_async_op;
  pasyncopdesc->fsal_op_context = *p_context;

  pasyncopdesc->ptr_mfsl_context = (caddr_t) p_mfsl_context;

  fsal_status = MFSL_async_post(pasyncopdesc);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Update the associated times for this object */
  pasyncdata->async_attr.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec;  /** @todo: there may be a coefficient to be applied here */
  filehandle->health = MFSL_ASYNC_ASYNCHRONOUS;

  /* merge the attributes to the asynchronous attributes */
  if((attrib_set->asked_attributes & FSAL_ATTR_SIZE) ||
     (attrib_set->asked_attributes & FSAL_ATTR_SPACEUSED))
    {
      /* Operation on a non data cached file */
      pasyncdata->async_attr.filesize = attrib_set->filesize;
      pasyncdata->async_attr.spaceused = attrib_set->spaceused;
    }

  if(attrib_set->asked_attributes & (FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(attrib_set->asked_attributes & FSAL_ATTR_MODE)
        pasyncdata->async_attr.mode = attrib_set->mode;

      if(attrib_set->asked_attributes & FSAL_ATTR_OWNER)
        pasyncdata->async_attr.owner = attrib_set->owner;

      if(attrib_set->asked_attributes & FSAL_ATTR_GROUP)
        pasyncdata->async_attr.group = attrib_set->group;
    }

  if(attrib_set->asked_attributes & (FSAL_ATTR_ATIME | FSAL_ATTR_MTIME))
    {
      if(attrib_set->asked_attributes & FSAL_ATTR_ATIME)
        pasyncdata->async_attr.atime = attrib_set->atime;

      if(attrib_set->asked_attributes & FSAL_ATTR_MTIME)
        pasyncdata->async_attr.mtime = attrib_set->mtime;
    }

  /* Set output attributes */
  *object_attributes = pasyncdata->async_attr;

  if(!mfsl_async_set_specdata(filehandle, pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_setattr */

#endif                          /* ! _USE_SWIG */
