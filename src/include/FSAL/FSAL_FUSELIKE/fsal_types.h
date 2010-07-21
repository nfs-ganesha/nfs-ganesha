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

#define fsal_handle_t fusefsal_handle_t
#define fsal_op_context_t fusefsal_op_context_t
#define fsal_file_t fusefsal_file_t
#define fsal_dir_t fusefsal_dir_t
#define fsal_export_context_t fusefsal_export_context_t
#define fsal_lockdesc_t fusefsal_lockdesc_t
#define fsal_cookie_t fusefsal_cookie_t
#define fs_specific_initinfo_t fusefs_specific_initinfo_t
#define fsal_cred_t fusefsal_cred_t

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

typedef struct fsal_cred__
{
  uid_t user;
  gid_t group;
} fusefsal_cred_t;

typedef struct fsal_export_context__
{
  fusefsal_handle_t root_handle;
  fsal_path_t root_full_path;   /* not expected to change when filesystem is mounted ! */
  struct ganefuse *ganefuse;
} fusefsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct fsal_op_context__
{
  fusefsal_export_context_t *export_context;    /* Must be the first entry in this structure */
  fusefsal_cred_t credential;
  struct ganefuse_context ganefuse_context;
} fusefsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct fsal_dir__
{
  fusefsal_handle_t dir_handle;
  fusefsal_op_context_t context;
  struct ganefuse_file_info dir_info;
} fusefsal_dir_t;

typedef struct fsal_file__
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

typedef struct fs_specific_initinfo__
{
  struct ganefuse_operations *fs_ops;
  void *user_data;

} fusefs_specific_initinfo_t;

typedef struct fsal_lockdesc__
{
  struct ganefuse_file_info file_info;
  struct flock file_lock;
} fusefsal_lockdesc_t;


#endif                          /* _FSAL_TYPES_SPECIFIC_H */
