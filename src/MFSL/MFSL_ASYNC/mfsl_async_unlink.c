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
 * MFSL_link_async_op: Callback for asynchronous link. 
 *
 * Callback for asynchronous link. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_setattrs.
 */
fsal_status_t MFSL_unlink_async_op(mfsl_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  LogDebug(COMPONENT_MFSL, "Making asynchronous FSAL_unlink for async op %p",
                  popasyncdesc);

  P(popasyncdesc->op_args.remove.pmobject->lock);
  fsal_status = FSAL_unlink(&(popasyncdesc->op_args.remove.pmobject->handle),
                            &popasyncdesc->op_args.remove.name,
                            &popasyncdesc->fsal_op_context,
                            &popasyncdesc->op_res.remove.attr);
  V(popasyncdesc->op_args.remove.pmobject->lock);

  return fsal_status;
}                               /* MFSL_unlink_async_op */

/**
 *
 * MFSAL_link_check_perms : Checks authorization to perform an asynchronous setattr.
 *
 * Checks authorization to perform an asynchronous link.
 *
 * @param target_handle     [IN]    mfsl object to be operated on.
 * @param dir_handle        [IN]    mfsl object to be operated on.
 * @param tgt_pspecdata     [INOUT] mfsl object associated specific data
 * @param dir_pspecdata     [INOUT] mfsl object associated specific data
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_unlink_check_perms(mfsl_object_t * dir_handle,      /* IN */
                                       mfsl_object_specific_data_t * dir_pspecdata,     /* IN */
                                       fsal_name_t * p_object_name,     /* IN */
                                       fsal_attrib_list_t * dir_attributes,     /* [ IN/OUT ] */
                                       fsal_op_context_t * p_context,   /* IN */
                                       mfsl_context_t * p_mfsl_context /* IN */ )
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_unlink_access(p_context, dir_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /** @todo : put some stuff in this function */
  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_unlink_check_perms */

/**
 *
 * MFSL_unlink : posts an asynchronous unlink and sets the cached attributes in return.
 *
 * Posts an asynchronous unlink and sets the cached attributes in return.
 * If an object is not asynchronous, then the content of object attributes structure for result will be used to populate it.
 *
 * @param parentdir_handle  [IN]    mfsl object to be operated on (source directory for the unlink)
 * @param p_object_name     [IN]    name of the object to be destroyed
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param parentdir_attributes    [INOUT] resulting attributes for directory 
 *
 * @return the same as FSAL_unlink
 */
fsal_status_t MFSL_unlink(mfsl_object_t * dir_handle,   /* IN */
                          fsal_name_t * p_object_name,  /* IN */
                          mfsl_object_t * object_handle,        /* INOUT */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * dir_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  mfsl_async_op_desc_t *pasyncopdesc = NULL;
  mfsl_object_specific_data_t *dir_pasyncdata = NULL;
  mfsl_object_specific_data_t *obj_pasyncdata = NULL;

  GetFromPool(pasyncopdesc, &p_mfsl_context->pool_async_op, mfsl_async_op_desc_t);

  if(pasyncopdesc == NULL)
    MFSL_return(ERR_FSAL_INVAL, 0);

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogMajor(COMPONENT_MFSL, "MFSL_link: cannot get time of day... exiting");
      exit(1);
    }

  if(!mfsl_async_get_specdata(dir_handle, &dir_pasyncdata))
    {
      /* Target is not yet asynchronous */

      GetFromPool(dir_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

      /* In this case use object_attributes parameter to initiate asynchronous object */
      dir_pasyncdata->async_attr = *dir_attributes;
    }

  fsal_status = MFSAL_unlink_check_perms(dir_handle,
                                         dir_pasyncdata,
                                         p_object_name,
                                         dir_attributes, p_context, p_mfsl_context);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  LogDebug(COMPONENT_MFSL,  "Creating asyncop %p",
                    pasyncopdesc);

  pasyncopdesc->op_type = MFSL_ASYNC_OP_REMOVE;

  pasyncopdesc->op_args.remove.pmobject = dir_handle;
  pasyncopdesc->op_args.remove.name = *p_object_name;
  pasyncopdesc->op_res.remove.attr = *dir_attributes;

  pasyncopdesc->op_func = MFSL_unlink_async_op;
  pasyncopdesc->fsal_op_context = *p_context;

  pasyncopdesc->ptr_mfsl_context = (caddr_t) p_mfsl_context;

  fsal_status = MFSL_async_post(pasyncopdesc);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Update the asynchronous metadata */
  dir_pasyncdata->async_attr.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  dir_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec;  /** @todo: there may be a coefficient to be applied here */
  dir_handle->health = MFSL_ASYNC_ASYNCHRONOUS;

  if(!mfsl_async_set_specdata(dir_handle, dir_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  if(!mfsl_async_get_specdata(object_handle, &obj_pasyncdata))
    {
      /* The object to be deleted is not asynchronous, but it has
       * has to become asynchronous to be correctly managed until the FSAL deletes it */
      GetFromPool(obj_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

      /* Possible bug here with getattr because it has not data */
    }

  /* Depending on the value of numlinks, the object should be deleted or not */
  if((obj_pasyncdata->async_attr.numlinks > 1)
     && (obj_pasyncdata->async_attr.type == FSAL_TYPE_FILE))
    obj_pasyncdata->async_attr.numlinks -= 1;
  else
    obj_pasyncdata->deleted = TRUE;

  if(!mfsl_async_set_specdata(object_handle, obj_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  /* Return the correct attributes */
  *dir_attributes = dir_pasyncdata->async_attr;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_unlink */

#endif                          /* ! _USE_SWIG */
