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
extern fsal_handle_t dir_handle_precreate;

/**
 *
 * MFSL_rename_async_op: Callback for asynchronous rename. 
 *
 * Callback for asynchronous rename. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_rename.
 */
fsal_status_t MFSL_rename_async_op(mfsl_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  LogDebug(COMPONENT_MFSL, "Making asynchronous FSAL_rename for async op %p",
                  popasyncdesc);

  if(popasyncdesc->op_args.rename.pmobject_src !=
     popasyncdesc->op_args.rename.pmobject_dirdest)
    {
      P(popasyncdesc->op_args.rename.pmobject_src->lock);
      P(popasyncdesc->op_args.rename.pmobject_dirdest->lock);
    }
  else
    P(popasyncdesc->op_args.rename.pmobject_src->lock);

  fsal_status = FSAL_rename(&(popasyncdesc->op_args.rename.pmobject_src->handle),
                            &popasyncdesc->op_args.rename.name_src,
                            &(popasyncdesc->op_args.rename.pmobject_dirdest->handle),
                            &popasyncdesc->op_args.rename.name_dest,
                            &popasyncdesc->fsal_op_context,
                            &popasyncdesc->op_res.rename.attrsrc,
                            &popasyncdesc->op_res.rename.attrdest);

  if(popasyncdesc->op_args.rename.pmobject_src !=
     popasyncdesc->op_args.rename.pmobject_dirdest)
    {
      V(popasyncdesc->op_args.rename.pmobject_src->lock);
      V(popasyncdesc->op_args.rename.pmobject_dirdest->lock);
    }
  else
    V(popasyncdesc->op_args.rename.pmobject_src->lock);

  return fsal_status;
}                               /* MFSL_rename_async_op */

/**
 *
 * MFSAL_rename_check_perms : Checks authorization to perform an asynchronous rename.
 *
 * Checks authorization to perform an asynchronous rename.
  *
 * @param old_parentdir_handle  [IN]    mfsl object to be operated on (source directory for the rename)
 * @param p_old_name            [IN]    name of the object to be renamed
 * @param new_parentdir_handle  [IN]    mfsl object to be operated on (destination directory for the rename)
 * @param p_new_name            [IN]    new name of the object 
 * @param p_context             [IN]    associated fsal context
 * @param p_mfsl_context        [INOUT] associated mfsl context
 * @param src_dir_attributes    [INOUT] resulting attributes for directory 
 * @param tgt_dir_attributes    [INOUT] resulting attributes for directory 
 *
 * @return always FSAL_NO_ERROR (not yet implemented 
 */
fsal_status_t MFSAL_rename_check_perms(mfsl_object_t * old_parentdir_handle,    /* IN */
                                       fsal_name_t * p_old_name,        /* IN */
                                       fsal_attrib_list_t * src_dir_attributes, /* [ IN/OUT ] */
                                       mfsl_object_t * new_parentdir_handle,    /* IN */
                                       fsal_name_t * p_new_name,        /* IN */
                                       fsal_attrib_list_t * tgt_dir_attributes, /* [ IN/OUT ] */
                                       fsal_op_context_t * p_context,   /* IN */
                                       mfsl_context_t * p_mfsl_context /* IN */ )
{
  fsal_status_t fsal_status;

  /* Check for the attributes first */
  fsal_status = FSAL_rename_access(p_context, src_dir_attributes, tgt_dir_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /** @todo : put some stuff in this function */
  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_rename_check_perms */

/**
 *
 * MFSL_rename : posts an asynchronous rename and sets the cached attributes in return.
 *
 * Posts an asynchronous rename and sets the cached attributes in return.
 * If an object is not asynchronous, then the content of object attributes structure for result will be used to populate it.
 *
 * @param old_parentdir_handle  [IN]    mfsl object to be operated on (source directory for the rename)
 * @param p_old_name            [IN]    name of the object to be renamed
ee @param new_parentdir_handle  [IN]    mfsl object to be operated on (destination directory for the rename)
 * @param p_new_name            [IN]    new name of the object 
 * @param p_context             [IN]    associated fsal context
 * @param p_mfsl_context        [INOUT] associated mfsl context
 * @param src_dir_attributes    [INOUT] resulting attributes for directory 
 * @param tgt_dir_attributes    [INOUT] resulting attributes for directory 
 *
 * @return the same as FSAL_rename
 */
fsal_status_t MFSL_rename(mfsl_object_t * old_parentdir_handle, /* IN */
                          fsal_name_t * p_old_name,     /* IN */
                          mfsl_object_t * new_parentdir_handle, /* IN */
                          fsal_name_t * p_new_name,     /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_attrib_list_t * src_dir_attributes,      /* [ IN/OUT ] */
                          fsal_attrib_list_t * tgt_dir_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  mfsl_async_op_desc_t *pasyncopdesc = NULL;
  mfsl_object_specific_data_t *old_parentdir_pasyncdata = NULL;
  mfsl_object_specific_data_t *new_parentdir_pasyncdata = NULL;

  P(p_mfsl_context->lock);

  GetFromPool(pasyncopdesc, &p_mfsl_context->pool_async_op, mfsl_async_op_desc_t);

  V(p_mfsl_context->lock);

  if(pasyncopdesc == NULL)
    MFSL_return(ERR_FSAL_INVAL, 0);

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogMajor(COMPONENT_MFSL,"MFSL_link: cannot get time of day... exiting");
      exit(1);
    }

  if(!mfsl_async_get_specdata(old_parentdir_handle, &old_parentdir_pasyncdata))
    {
      /* Target is not yet asynchronous */
      P(p_mfsl_context->lock);

      GetFromPool(old_parentdir_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

      V(p_mfsl_context->lock);

      /* In this case use object_attributes parameter to initiate asynchronous object */
      old_parentdir_pasyncdata->async_attr = *src_dir_attributes;
    }

  if(!mfsl_async_get_specdata(new_parentdir_handle, &new_parentdir_pasyncdata))
    {
      /* Target is not yet asynchronous */
      P(p_mfsl_context->lock);

      GetFromPool(new_parentdir_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

      V(p_mfsl_context->lock);

      /* In this case use object_attributes parameter to initiate asynchronous object */
      new_parentdir_pasyncdata->async_attr = *tgt_dir_attributes;
    }

  fsal_status = MFSAL_rename_check_perms(old_parentdir_handle,
                                         p_old_name,
                                         src_dir_attributes,
                                         new_parentdir_handle,
                                         p_new_name,
                                         tgt_dir_attributes, p_context, p_mfsl_context);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  LogDebug(COMPONENT_MFSL, "Creating asyncop %p",
                    pasyncopdesc);

  pasyncopdesc->op_type = MFSL_ASYNC_OP_RENAME;

  pasyncopdesc->op_args.rename.pmobject_src = old_parentdir_handle;
  pasyncopdesc->op_args.rename.name_src = *p_old_name;
  pasyncopdesc->op_res.rename.attrsrc = *src_dir_attributes;

  pasyncopdesc->op_args.rename.pmobject_dirdest = new_parentdir_handle;
  pasyncopdesc->op_args.rename.name_dest = *p_new_name;
  pasyncopdesc->op_res.rename.attrdest = *tgt_dir_attributes;

  pasyncopdesc->op_func = MFSL_rename_async_op;
  pasyncopdesc->fsal_op_context = *p_context;

  pasyncopdesc->ptr_mfsl_context = (caddr_t) p_mfsl_context;

  fsal_status = MFSL_async_post(pasyncopdesc);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Update the asynchronous metadata */
  old_parentdir_pasyncdata->async_attr.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  old_parentdir_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec;  /** @todo: there may be a coefficient to be applied here */
  old_parentdir_handle->health = MFSL_ASYNC_ASYNCHRONOUS;

  new_parentdir_pasyncdata->async_attr.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  new_parentdir_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec;  /** @todo: there may be a coefficient to be applied here */
  new_parentdir_handle->health = MFSL_ASYNC_ASYNCHRONOUS;

  if(!mfsl_async_set_specdata(old_parentdir_handle, old_parentdir_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  if(!mfsl_async_set_specdata(new_parentdir_handle, new_parentdir_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  /* Return the correct attributes */
  *src_dir_attributes = old_parentdir_pasyncdata->async_attr;
  *tgt_dir_attributes = new_parentdir_pasyncdata->async_attr;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_rename */

#endif                          /* ! _USE_SWIG */
