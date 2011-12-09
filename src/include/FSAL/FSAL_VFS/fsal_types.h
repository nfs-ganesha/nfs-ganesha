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
 *
 *
 */

#ifndef _FSAL_TYPES_SPECIFIC_H
#define _FSAL_TYPES_SPECIFIC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * FS relative includes
 */

#include "config_parsing.h"
#include "err_fsal.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif                          /* _GNU_SOURCE */

#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE
#endif                          /* _ATFILE_SOURCE */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/*
 * labels in the config file
 */

#define CONF_LABEL_FS_SPECIFIC   "VFS"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define FSAL_VFS_HANDLE_LEN 29
#define FSAL_VFS_FSHANDLE_LEN 64

#include "fsal_handle_syscalls.h"
#include "fsal_glue_const.h"

typedef union {
 struct
  {
     vfs_file_handle_t vfs_handle ;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} vfsfsal_handle_t;  /**< FS object handle */

/** Authentification context.    */


typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  char              fstype[MAXNAMLEN] ;
  int               mount_root_fd ;
  vfs_file_handle_t root_handle ;
} vfsfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( _pexport_context ) (uint64_t)((_pexport_context)->dev_id)

//#define FSAL_GET_EXP_CTX( popctx ) (fsal_export_context_t *)(( (vfsfsal_op_context_t *)popctx)->export_context)

typedef struct
{
  vfsfsal_export_context_t *export_context;     /* Must be the first entry in this structure */
  struct user_credentials credential;
} vfsfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct
{
  char vfs_mount_point[MAXPATHLEN];
} vfsfs_specific_initinfo_t;

/**< directory cookie */
typedef union {
 struct
 {
  off_t cookie;
 } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} vfsfsal_cookie_t;

#define FSAL_SET_PCOOKIE_BY_OFFSET( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   ((vfsfsal_cookie_t *)__pfsal_cookie)->data.cookie = (off_t)__cookie ; \
} while( 0 )

#define FSAL_SET_OFFSET_BY_PCOOKIE( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   __cookie =  ((vfsfsal_cookie_t *)__pfsal_cookie)->data.cookie ;       \
} while( 0 )


//static const vfsfsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { 0 };

/* Directory stream descriptor. */

typedef struct
{
  int fd;
  vfsfsal_op_context_t context; /* credential for accessing the directory */
  fsal_path_t path;
  unsigned int dir_offset;
  vfsfsal_handle_t handle;
} vfsfsal_dir_t;

typedef struct
{
  int fd;
  int ro;                       /* read only file ? */
} vfsfsal_file_t;

//#define FSAL_GET_EXP_CTX( popctx ) (fsal_export_context_t *)(( (vfsfsal_op_context_t *)popctx)->export_context)
//#define FSAL_FILENO( p_fsal_file )  ((vfsfsal_file_t *)p_fsal_file)->fd 

#endif                          /* _FSAL_TYPES__SPECIFIC_H */
