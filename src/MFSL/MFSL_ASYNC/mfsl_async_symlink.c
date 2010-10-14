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
#include "log_functions.h"
#include "stuff_alloc.h"
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"

#include <pthread.h>
#include <errno.h>

#ifndef _USE_SWIG

extern fsal_handle_t tmp_symlink_dirhandle;
extern mfsl_parameter_t mfsl_param;
extern mfsl_synclet_data_t *synclet_data;

/**
 *
 * MFSL_symlink_async_op: Callback for asynchronous link. 
 *
 * Callback for asynchronous symlink. 
 *
 * @param popasyncdes [INOUT] asynchronous operation descriptor
 *
 * @return the status of the performed FSAL_symlink.
 */
fsal_status_t MFSL_symlink_async_op(mfsl_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;
  fsal_attrib_list_t attrsrc, attrdest, chown_attr;
  fsal_handle_t handle;

  attrsrc = attrdest = popasyncdesc->op_res.mkdir.attr;

  LogDebug(COMPONENT_MFSL,
                  "Renaming file to complete asynchronous FSAL_symlink for async op %p",
                  popasyncdesc);

  P(popasyncdesc->op_args.symlink.pmobject_dirdest->lock);
  fsal_status = FSAL_rename(&tmp_symlink_dirhandle,
                            &popasyncdesc->op_args.symlink.precreate_name,
                            &(popasyncdesc->op_args.symlink.pmobject_dirdest->handle),
                            &popasyncdesc->op_args.symlink.linkname,
                            &popasyncdesc->fsal_op_context, &attrsrc, &attrdest);
  if(FSAL_IS_ERROR(fsal_status))
    {
      V(popasyncdesc->op_args.symlink.pmobject_dirdest->lock);

      return fsal_status;
    }

  V(popasyncdesc->op_args.symlink.pmobject_dirdest->lock);

  return fsal_status;
}                               /* MFSL_symlink_async_op */

/**
 *
 * MFSAL_symlink_check_perms : Checks authorization to perform an asynchronous symlink.
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
fsal_status_t MFSAL_symlink_check_perms(mfsl_object_t * target_handle,
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
}                               /* MFSL_symlink_check_perms */

/**
 *
 * MFSL_symlink : posts an asynchronous symlink and sets the cached attributes in return.
 *
 * Posts an asynchronous symlink and sets the cached attributes in return.
 *
 * @param parent_directory_handle  [IN]    mfsl object to be operated on (parent dir for the new file).
 * @param p_linkname               [IN]    new symlink's name
 * @param p_linkcontent            [IN]    new symlink's content 
 * @param p_context                [IN]    associated fsal context
 * @param p_mfsl_context           [INOUT] associated mfsl context
 * @param accessmode               [IN]    access mode for file create
 * @param link_handle              [INOUT] new mfsl object
 * @param link_attributes          [INOUT] resulting attributes for new object
 *
 * @return the same as FSAL_link
 */
extern fsal_handle_t tmp_symlink_dirhandle;  /**< Global variable that will contain the handle to the symlinks's nursery */

fsal_status_t MFSL_symlink(mfsl_object_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored); */
                           mfsl_object_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes /* [ IN/OUT ] */ )
{
  fsal_status_t fsal_status;
  mfsl_async_op_desc_t *pasyncopdesc = NULL;
  mfsl_object_specific_data_t *symlink_pasyncdata = NULL;
  mfsl_object_t *psymlink_handle = NULL;
  fsal_name_t tmp_fsal_name;
  char tmp_name[MAXNAMLEN];
  static unsigned int counter = 0;

  snprintf(tmp_name, MAXNAMLEN, "%s.%u", p_linkname->name, counter);
  counter += 1;

  if(FSAL_IS_ERROR(FSAL_str2name(tmp_name, MAXNAMLEN, &tmp_fsal_name)))
    return fsal_status;

  fsal_status = MFSAL_symlink_check_perms(parent_directory_handle,
                                          p_linkname,
                                          p_context, p_mfsl_context, link_attributes);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  P(parent_directory_handle->lock);
  fsal_status = FSAL_symlink(&tmp_symlink_dirhandle,
                             &tmp_fsal_name,
                             p_linkcontent,
                             p_context,
                             accessmode, &link_handle->handle, link_attributes);
  V(parent_directory_handle->lock);

  P(p_mfsl_context->lock);

  GetFromPool(pasyncopdesc, &p_mfsl_context->pool_async_op, mfsl_async_op_desc_t);

  GetFromPool(symlink_pasyncdata, &p_mfsl_context->pool_spec_data, mfsl_object_specific_data_t);

  V(p_mfsl_context->lock);

  if(pasyncopdesc == NULL)
    MFSL_return(ERR_FSAL_INVAL, 0);

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogMajor(COMPONENT_MFSL, "MFSL_synlink: cannot get time of day... exiting");
      exit(1);
    }

  LogDebug(COMPONENT_MFSL,  "Creating asyncop %p",
                    pasyncopdesc);

  pasyncopdesc->op_type = MFSL_ASYNC_OP_SYMLINK;

  pasyncopdesc->op_args.symlink.pmobject_dirdest = parent_directory_handle;
  pasyncopdesc->op_args.symlink.precreate_name = tmp_fsal_name;
  pasyncopdesc->op_args.symlink.linkname = *p_linkname;

  pasyncopdesc->op_res.symlink.attr.asked_attributes = link_attributes->asked_attributes;
  pasyncopdesc->op_res.symlink.attr.supported_attributes =
      link_attributes->supported_attributes;

  pasyncopdesc->ptr_mfsl_context = (caddr_t) p_mfsl_context;

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  pasyncopdesc->op_func = MFSL_symlink_async_op;
  //pasyncopdesc->fsal_op_context = p_context ;
  pasyncopdesc->fsal_op_context =
      synclet_data[pasyncopdesc->related_synclet_index].root_fsal_context;

  fsal_status = MFSL_async_post(pasyncopdesc);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Update the asynchronous metadata */
  symlink_pasyncdata->async_attr = *link_attributes;
  symlink_pasyncdata->deleted = FALSE;

  if(!mfsl_async_set_specdata(link_handle, symlink_pasyncdata))
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  /* Return the correct attributes */
  link_handle->health = MFSL_ASYNC_NEVER_SYNCED;

  /* Do not forget that the parent directory becomes asynchronous too */
  parent_directory_handle->health = MFSL_ASYNC_ASYNCHRONOUS;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_symlink */

#ifdef _HAVE_SYNCHRONOUS_SYMLINK
fsal_status_t MFSL_symlink(mfsl_object_t * parent_directory_handle,     /* IN */
                           fsal_name_t * p_linkname,    /* IN */
                           fsal_path_t * p_linkcontent, /* IN */
                           fsal_op_context_t * p_context,       /* IN */
                           mfsl_context_t * p_mfsl_context,     /* IN */
                           fsal_accessmode_t accessmode,        /* IN (ignored); */
                           mfsl_object_t * link_handle, /* OUT */
                           fsal_attrib_list_t * link_attributes /* [ IN/OUT ] */
    )
{
  fsal_status_t fsal_status;

  P(parent_directory_handle->lock);
  fsal_status = FSAL_symlink(&parent_directory_handle->handle,
                             p_linkname,
                             p_linkcontent,
                             p_context,
                             accessmode, &link_handle->handle, link_attributes);
  V(parent_directory_handle->lock);

  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* If successful, the symlink's mobject should be clearly indentified as a symbolic link: a symbolic link can't be an asynchronous 
   * object and it has to remain synchronous everywhere */
  link_handle->health = MFSL_ASYNC_IS_SYMLINK;

  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}                               /* MFSL_symlink */
#endif

#endif                          /* ! _USE_SWIG */
