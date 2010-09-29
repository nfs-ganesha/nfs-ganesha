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
#include "RW_Lock.h"

#ifndef _USE_SWIG

extern mfsl_parameter_t mfsl_param;
extern fsal_handle_t dir_handle_precreate;
extern mfsl_synclet_data_t *synclet_data;

/**
 *
 * MFSL_create_async_op: Callback for asynchronous link. 
 *
 * Callback for asynchronous create. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_create.
 */
fsal_status_t MFSL_create_async_op(mfsl_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;
  fsal_attrib_list_t attrsrc, attrdest, chown_attr;
  fsal_handle_t handle;

  attrsrc = attrdest = popasyncdesc->op_res.mkdir.attr;

  LogDebug(COMPONENT_MFSL,
                  "Renaming file to complete asynchronous FSAL_create for async op %p",
                  popasyncdesc);

  P(popasyncdesc->op_args.create.pmfsl_obj_dirdest->lock);
  fsal_status = FSAL_rename(&dir_handle_precreate,
                            &popasyncdesc->op_args.create.precreate_name,
                            &(popasyncdesc->op_args.create.pmfsl_obj_dirdest->handle),
                            &popasyncdesc->op_args.create.filename,
                            &popasyncdesc->fsal_op_context, &attrsrc, &attrdest);
  if(FSAL_IS_ERROR(fsal_status))
    {
      V(popasyncdesc->op_args.create.pmfsl_obj_dirdest->lock);

      return fsal_status;
    }

  /* Lookup to get the right attributes for the object */
  fsal_status = FSAL_lookup(&(popasyncdesc->op_args.create.pmfsl_obj_dirdest->handle),
                            &popasyncdesc->op_args.create.filename,
                            &popasyncdesc->fsal_op_context,
                            &handle, &popasyncdesc->op_res.create.attr);

  if(FSAL_IS_ERROR(fsal_status))
    {
      V(popasyncdesc->op_args.create.pmfsl_obj_dirdest->lock);

      return fsal_status;
    }

  /* If user is not root, setattr to chown the entry */
  if(popasyncdesc->op_args.create.owner != 0)
    {
      chown_attr.asked_attributes = FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP;
      chown_attr.mode = popasyncdesc->op_args.create.mode;
      chown_attr.owner = popasyncdesc->op_args.create.owner;
      chown_attr.group = popasyncdesc->op_args.create.group;

      fsal_status =
          FSAL_setattrs(&handle, &popasyncdesc->fsal_op_context, &chown_attr,
                        &popasyncdesc->op_res.create.attr);
    }

  V(popasyncdesc->op_args.create.pmfsl_obj_dirdest->lock);

  return fsal_status;
}                               /* MFSL_create_async_op */

/**
 *
 * MFSAL_create_check_perms : Checks authorization to perform an asynchronous setattr.
 *
 * Checks authorization to perform an asynchronous setattr.
 *
 * @param target_handle     [IN]    mfsl object to be operated on.
 * @param p_dirname         [IN]    name of the object to be created
 * @param p_context         [IN]    associated fsal context
 * @param p_mfsl_context    [INOUT] associated mfsl context
 * @param object_attributes [INOUT] parent's attributes
 *
 */
fsal_status_t MFSAL_create_check_perms(mfsl_object_t * target_handle,
                                       fsal_name_t * p_dirname,
                                       fsal_op_context_t * p_context,
                                       mfsl_context_t * p_mfsl_context,
                                       fsal_attrib_list_t * object_attributes)
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_create_access(p_context, object_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /** @todo : put some stuff in this function */
  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_create_check_perms */

/**
 *
 * MFSL_create : posts an asynchronous create and sets the cached attributes in return.
 *
 * Posts an asynchronous create and sets the cached attributes in return.
 * If an object is not asynchronous, then the content of object attributes structure for result will be used to populate it.
 *
 * @param parent_directory_handle  [IN]    mfsl object to be operated on (parent dir for the new file).
 * @param p_dirname                [IN]    new file's name
 * @param p_context                [IN]    associated fsal context
 * @param p_mfsl_context           [INOUT] associated mfsl context
 * @param accessmode               [IN]    access mode for file create
 * @param object_handle            [INOUT] new mfsl object
 * @param object_attributes        [INOUT] resulting attributes for new object
 * @param parent_attributes        [IN]    attributes of the parent entry
 *
 * @return the same as FSAL_link
 */
fsal_status_t MFSL_create(mfsl_object_t * parent_directory_handle,      /* IN */
                          fsal_name_t * p_dirname,      /* IN */
                          fsal_op_context_t * p_context,        /* IN */
                          mfsl_context_t * p_mfsl_context,      /* IN */
                          fsal_accessmode_t accessmode, /* IN */
                          mfsl_object_t * object_handle,        /* OUT */
                          fsal_attrib_list_t * object_attributes,       /* [ IN/OUT ] */
                          fsal_attrib_list_t * parent_attributes /* IN */ )
{
  fsal_status_t fsal_status;
  mfsl_async_op_desc_t *pasyncopdesc = NULL;
  mfsl_object_specific_data_t *newfile_pasyncdata = NULL;
  mfsl_object_t *pnewfile_handle = NULL;
  mfsl_precreated_object_t *pprecreated = NULL;

  fsal_status = MFSAL_create_check_perms(parent_directory_handle,
                                         p_dirname,
                                         p_context, p_mfsl_context, parent_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  P(p_mfsl_context->lock);

  GetFromPool(pasyncopdesc, &p_mfsl_context->pool_async_op, mfsl_async_op_desc_t);

  GetFromPool(newfile_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

  V(p_mfsl_context->lock);

  if(pasyncopdesc == NULL)
    MFSL_return(ERR_FSAL_INVAL, 0);

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogMajor(COMPONENT_MFSL, "MFSL_create: cannot get time of day... exiting");
      exit(1);
    }

  /* Now get a pre-allocated directory from the synclet data */
  P(p_mfsl_context->lock);
  GetFromPool(pprecreated, &p_mfsl_context->pool_files, mfsl_precreated_object_t);
  V(p_mfsl_context->lock);

  pnewfile_handle = &(pprecreated->mobject);

  LogDebug(COMPONENT_MFSL,  "Creating asyncop %p",
                    pasyncopdesc);

  pasyncopdesc->op_type = MFSL_ASYNC_OP_CREATE;

  pasyncopdesc->op_args.create.pmfsl_obj_dirdest = parent_directory_handle;
  pasyncopdesc->op_args.create.precreate_name = pprecreated->name;
  pasyncopdesc->op_args.create.filename = *p_dirname;
  pasyncopdesc->op_args.create.owner = FSAL_OP_CONTEXT_TO_UID(p_context);
  pasyncopdesc->op_args.create.group = FSAL_OP_CONTEXT_TO_GID(p_context);
  pasyncopdesc->op_args.create.mode = accessmode;
  pasyncopdesc->op_res.create.attr.asked_attributes = object_attributes->asked_attributes;
  pasyncopdesc->op_res.create.attr.supported_attributes =
      object_attributes->supported_attributes;

  pasyncopdesc->ptr_mfsl_context = (caddr_t) p_mfsl_context;

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  pasyncopdesc->op_func = MFSL_create_async_op;
  //pasyncopdesc->fsal_op_context = p_context ;
  pasyncopdesc->fsal_op_context =
      synclet_data[pasyncopdesc->related_synclet_index].root_fsal_context;

  fsal_status = MFSL_async_post(pasyncopdesc);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Update the asynchronous metadata */
  newfile_pasyncdata->async_attr = pprecreated->attr;

  newfile_pasyncdata->async_attr.type = FSAL_TYPE_FILE;
  newfile_pasyncdata->async_attr.filesize = 0;  /* New file */
  newfile_pasyncdata->async_attr.spaceused = 0;
  newfile_pasyncdata->async_attr.numlinks = 1;  /* New file */

  newfile_pasyncdata->async_attr.owner = pasyncopdesc->op_args.create.owner;
  newfile_pasyncdata->async_attr.group = pasyncopdesc->op_args.create.group;

  newfile_pasyncdata->async_attr.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  newfile_pasyncdata->async_attr.ctime.nseconds = pasyncopdesc->op_time.tv_usec;  /** @todo: there may be a coefficient to be applied here */

  newfile_pasyncdata->deleted = FALSE;

  if(!mfsl_async_set_specdata(pnewfile_handle, newfile_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  /* Return the correct attributes */
  *object_attributes = newfile_pasyncdata->async_attr;
  *object_handle = pprecreated->mobject;
  object_handle->health = MFSL_ASYNC_NEVER_SYNCED;

  /* Do not forget that the parent directory becomes asynchronous too */
  parent_directory_handle->health = MFSL_ASYNC_ASYNCHRONOUS;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_create */

#endif                          /* ! _USE_SWIG */
