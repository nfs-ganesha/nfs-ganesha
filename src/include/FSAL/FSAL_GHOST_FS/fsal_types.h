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

/*
 * FS relative includes
 */

#include "ghost_fs.h"

/*
 * labels in the config file
 */

# define CONF_LABEL_FS_SPECIFIC   "GHOST_FS"

/* -------------------------------------------
 *      GHOST FS dependant definitions
 * ------------------------------------------- */

#define FSAL_MAX_NAME_LEN   GHOSTFS_MAX_FILENAME
#define FSAL_MAX_PATH_LEN   GHOSTFS_MAX_PATH

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

#define FSAL_NAME_INITIALIZER {"",0}
#define FSAL_PATH_INITIALIZER {"",0}

static fsal_name_t FSAL_DOT = { ".", 1 };
static fsal_name_t FSAL_DOT_DOT = { "..", 2 };

typedef GHOSTFS_handle_t fsal_handle_t;    /**< FS object handle.            */

/** Authentification context.    */

typedef struct fsal_cred__
{
  GHOSTFS_user_t user;
  GHOSTFS_group_t group;
} fsal_cred_t;

/** fs specific init info */

typedef struct ghostfs_dir_def__
{
  char path[FSAL_MAX_PATH_LEN];
  fsal_accessmode_t dir_mode;
  fsal_uid_t dir_owner;
  fsal_gid_t dir_group;

  struct ghostfs_dir_def__ *next;

} ghostfs_dir_def_t;

typedef struct fs_specific_initinfo__
{
  fsal_accessmode_t root_mode;  /* the mode of fs root */
  fsal_uid_t root_owner;        /* the owner of fs root */
  fsal_gid_t root_group;        /* the group of fs root */
  int dot_dot_root_eq_root;     /* indicates if fs root contains a '..' entry pointing on itself */
  int root_access;              /* indicates if root can access everything */

  ghostfs_dir_def_t *dir_list;

} fs_specific_initinfo_t;

/**< directory cookie */

typedef struct fsal_cookie__
{
  GHOSTFS_cookie_t cookie;
} fsal_cookie_t;

static fsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { (GHOSTFS_cookie_t) NULL };

typedef void *fsal_lockdesc_t;   /**< not implemented in ghostfs */
typedef void *fsal_export_context_t;

typedef struct
{
  fsal_export_context_t *export_context;        /* Must be the first entry in this structure */
  fsal_cred_t credential;
} fsal_op_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(*pexport_context)

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )
/* Directory stream descriptor. */

typedef struct fsal_dir__
{
  dir_descriptor_t dir_descriptor;      /* GHOSTFS dirdescriptor */
  fsal_op_context_t context;    /* credential for readdir operations */
} fsal_dir_t;

typedef void *fsal_file_t;      /**< not implemented in ghostfs */

/* no fd in ghostfs for the moment */
//#define FSAL_FILENO( p_fsal_file )  ( 1 )

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
