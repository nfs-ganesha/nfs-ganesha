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
 * \author  $Author: duraffort $
 * \date    $Date: 2010/17/06 10:00:00 $
 * \version $Revision: 1 $
 * \brief   File System Abstraction Layer types and constants.
 *
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

/* >> write here the includes your need for your filesystem << */

  /* Change bellow the label of your filesystem configuration
   * section in the GANESHA's configuration file.
   */
# define CONF_LABEL_FS_SPECIFIC   "ZFS"

#include <sys/types.h>
#include <sys/param.h>
#include <inttypes.h>
#include "config_parsing.h"
#include "err_fsal.h"

#include <libzfswrap.h>

  /* In this section, you must define your own FSAL internal types.
   * Here are some template types :
   */
# define FSAL_MAX_NAME_LEN  256
# define FSAL_MAX_PATH_LEN  1024

/* prefered readdir size */
#define FSAL_READDIR_SIZE 2048

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

# define FSAL_NAME_INITIALIZER {"",0}
# define FSAL_PATH_INITIALIZER {"",0}

static fsal_name_t FSAL_DOT = { ".", 1 };
static fsal_name_t FSAL_DOT_DOT = { "..", 2 };

  /* some void types for this template... */

typedef struct fsal_handle__
{
  inogen_t zfs_handle;
  fsal_nodetype_t type;

} fsal_handle_t;

typedef struct fsal_cred__
{
  int user;
  int group;
  int ticket_handle;
  time_t ticket_renewal_time;

} fsal_cred_t;

typedef struct fsal_export_context__
{
  fsal_handle_t root_handle;
  vfs_t *p_vfs;

} fsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct fsal_op_context__
{
  fsal_cred_t user_credential;
  int thread_connect_array[32];
  fsal_export_context_t *export_context;

} fsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct fsal_dir__
{
  vfs_t* p_vfs;
  vnode_t *p_vnode;
  inogen_t zfs_handle;

} fsal_dir_t;

typedef struct fsal_file__
{
  vfs_t *p_vfs;
  inogen_t zfs_handle;
  off_t current_offset;
  int flags;
  vnode_t *p_vnode;

} fsal_file_t;

# define FSAL_FILENO(_f) (_f)->zfs_handle.inode

typedef off_t fsal_cookie_t;

#define FSAL_READDIR_FROM_BEGINNING 0

typedef struct fs_specific_initinfo__
{
  char psz_zpool[FSAL_MAX_NAME_LEN];

} fs_specific_initinfo_t;

typedef void *fsal_lockdesc_t;

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
