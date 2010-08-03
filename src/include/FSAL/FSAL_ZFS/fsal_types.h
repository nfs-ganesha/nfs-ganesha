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

#include "fsal_glue_const.h"

#define fsal_handle_t zfsfsal_handle_t
#define fsal_op_context_t zfsfsal_op_context_t
#define fsal_file_t zfsfsal_file_t
#define fsal_dir_t zfsfsal_dir_t
#define fsal_export_context_t zfsfsal_export_context_t
#define fsal_lockdesc_t zfsfsal_lockdesc_t
#define fsal_cookie_t zfsfsal_cookie_t
#define fs_specific_initinfo_t zfsfs_specific_initinfo_t
#define fsal_cred_t zfsfsal_cred_t


typedef union
{
  struct
  {
    inogen_t zfs_handle;
    fsal_nodetype_t type;
  }data;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} zfsfsal_handle_t;

typedef struct fsal_cred__
{
  creden_t cred;
  int ticket_handle;
  time_t ticket_renewal_time;

} zfsfsal_cred_t;

typedef struct fsal_export_context__
{
  fsal_handle_t root_handle;
  libzfswrap_vfs_t *p_vfs;

} zfsfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(FSAL_Handle_to_RBTIndex( &(pexport_context->root_handle), 0 ) )

typedef struct fsal_op_context__
{
  fsal_cred_t user_credential;
  int thread_connect_array[32];
  fsal_export_context_t *export_context;

} zfsfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.cred.uid )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.cred.gid )

typedef struct fsal_dir__
{
  libzfswrap_vfs_t* p_vfs;
  creden_t cred;
  libzfswrap_vnode_t *p_vnode;
  inogen_t zfs_handle;

} zfsfsal_dir_t;

typedef struct fsal_file__
{
  libzfswrap_vfs_t *p_vfs;
  creden_t cred;
  inogen_t zfs_handle;
  off_t current_offset;
  int flags;
  libzfswrap_vnode_t *p_vnode;
  int is_closed;

} zfsfsal_file_t;

typedef union
{
  struct
  {
    off_t cookie;
  } data;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} zfsfsal_cookie_t;

#define FSAL_READDIR_FROM_BEGINNING 0

typedef struct fs_specific_initinfo__
{
  char psz_zpool[FSAL_MAX_NAME_LEN];

} zfsfs_specific_initinfo_t;

typedef void *fsal_lockdesc_t;

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
