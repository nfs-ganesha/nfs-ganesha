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
 * \file    fsal_types.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 * \brief   File System Abstraction Layer types and constants.
 *
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

/* >> write here the includes your need for your filesystem << */

  /* Change bellow the label of your filesystem configuration
   * section in the GANESHA's configuration file.
   */
# define CONF_LABEL_FS_SPECIFIC   "TEMPLATE"

#include <sys/types.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"

  /* In this section, you must define your own FSAL internal types.
   */

/** object name.  */

typedef struct fsal_name__
{
  char name[FSAL_MAX_NAME_LEN];
  unsigned int len;
} fsal_name_t;

/** object path.  */

typedef struct fsal_path__
{
  char path[FSAL_MAX_PATH_LEN];
  unsigned int len;
} fsal_path_t;

static fsal_name_t FSAL_DOT = { ".", 1 };
static fsal_name_t FSAL_DOT_DOT = { "..", 2 };

  /* some void types for this template... */

typedef struct fsal_handle__
{
  int object_identifier;
  fsal_nodetype_t object_type_reminder;

} fsal_handle_t;

typedef struct fsal_export_context__
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  int filesystem_id;
  fsal_handle_t root_handle;
  char server_name[256];
  int default_cos;

} fsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct fsal_op_context__
{
  fsal_export_context_t *export_context;        /* Must be the first entry in this structure */

  struct user_credentials credential;
  int thread_connect_array[32];

} fsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef int fsal_dir_t;
typedef int fsal_file_t;

//# define FSAL_FILENO(_f) fileno(_f)

typedef int fsal_cookie_t;

#define FSAL_READDIR_FROM_BEGINNING 0

typedef struct fs_specific_initinfo__
{
  int parameter1;
  char parameter2[256];
  int parameter3;

} fs_specific_initinfo_t;

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
