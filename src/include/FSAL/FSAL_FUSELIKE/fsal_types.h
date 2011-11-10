/*
 *
 *
 * Copyright CEA/DAM/DIF  (2005)
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

#include "ganesha_fuse_wrap.h"

# define CONF_LABEL_FS_SPECIFIC   "FUSE"

#include <sys/types.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"

#include "fsal_glue_const.h"

  /* In this section, you must define your own FSAL internal types.
   * Here are some template types :
   */

typedef union {
  struct 
  {
    ino_t inode;
    dev_t device;
    unsigned int validator;       /* because fuse filesystem
                                   can reuse their old inode numbers,
                                   which is not NFS compliant. */
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} fusefsal_handle_t;

typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  fusefsal_handle_t root_handle;
  fsal_path_t root_full_path;   /* not expected to change when filesystem is mounted ! */
  struct ganefuse *ganefuse;
} fusefsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct
{
  fusefsal_export_context_t *export_context;    /* Must be the first entry in this structure */
  struct user_credentials credential;
  struct ganefuse_context ganefuse_context;
} fusefsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct
{
  fusefsal_handle_t dir_handle;
  fusefsal_op_context_t context;
  struct ganefuse_file_info dir_info;
} fusefsal_dir_t;

typedef struct
{
  fusefsal_handle_t file_handle;
  fusefsal_op_context_t context;
  struct ganefuse_file_info file_info;
  fsal_off_t current_offset;
} fusefsal_file_t;

//# define FSAL_FILENO(_p_f) ( (_p_f)->file_info.fh )

typedef union {
  off_t data;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} fusefsal_cookie_t;

//#define FSAL_READDIR_FROM_BEGINNING ((fusefsal_cookie_t)0)

typedef struct
{
  struct ganefuse_operations *fs_ops;
  void *user_data;

} fusefs_specific_initinfo_t;

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
