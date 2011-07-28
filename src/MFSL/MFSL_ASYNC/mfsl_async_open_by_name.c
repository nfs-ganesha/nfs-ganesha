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

fsal_status_t MFSL_open_by_name(mfsl_object_t * dirhandle,      /* IN */
                                fsal_name_t * filename, /* IN */
                                mfsl_object_t * filehandle,     /* IN */
                                fsal_op_context_t * p_context,  /* IN */
                                mfsl_context_t * p_mfsl_context,        /* IN */
                                fsal_openflags_t openflags,     /* IN */
                                fsal_file_t * file_descriptor,  /* OUT */
                                fsal_attrib_list_t * file_attributes /* [ IN/OUT ] */ )
{
    /** @todo: quick and dirty work-around */
  if(dirhandle->health != MFSL_ASYNC_SYNCHRONOUS)
    MFSL_return(ERR_FSAL_NO_ERROR, 0);

  return FSAL_open_by_name(&dirhandle->handle,
                           filename,
                           p_context, openflags, file_descriptor, file_attributes);
}                               /* MFSL_open_by_name */

#endif                          /* ! _USE_SWIG */
