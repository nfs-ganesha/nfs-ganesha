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

#ifdef _SOLARIS
#include "solaris_port.h"
#ifdef _USE_POSIX
#define NAME_MAX  255
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#endif                          /* _USE_POSIX */
#endif                          /* _SOLARIS */

/*
 * FS relative includes
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

/*
 * labels in the config file
 */

#define CONF_LABEL_FS_SPECIFIC   "POSIX"

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define FSAL_MAX_NAME_LEN   NAME_MAX
#define FSAL_MAX_PATH_LEN   PATH_MAX

#define FSAL_NGROUPS_MAX  32

#include "fsal_glue_const.h"

#define fsal_handle_t posixfsal_handle_t
#define fsal_op_context_t posixfsal_op_context_t
#define fsal_file_t posixfsal_file_t
#define fsal_dir_t posixfsal_dir_t
#define fsal_export_context_t posixfsal_export_context_t
#define fsal_lockdesc_t posixfsal_lockdesc_t
#define fsal_cookie_t posixfsal_cookie_t
#define fs_specific_initinfo_t posixfs_specific_initinfo_t
#define fsal_cred_t posixfsal_cred_t


/** object POSIX infos */
typedef struct
{
  dev_t devid;
  ino_t inode;
  int nlink;
  time_t ctime;
  fsal_nodetype_t ftype;
} fsal_posixdb_fileinfo_t;

typedef union {
 struct
  {
    fsal_u64_t id;
    int ts;                       /* timestamp */
    fsal_posixdb_fileinfo_t info; /* info from the database, related to the object on the FS */
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} posixfsal_handle_t;  /**< FS object handle.            */

/** Authentification context.    */

typedef struct fsal_cred__
{
  uid_t user;
  gid_t group;
  fsal_count_t nbgroups;
  gid_t alt_groups[FSAL_NGROUPS_MAX];
} posixfsal_cred_t;

/** fs specific init info */
#include "posixdb.h"

//typedef void *fsal_export_context_t;
typedef struct
{
  void *data;
} posixfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( pexport_context ) (uint64_t)(*pexport_context)

typedef struct
{
  posixfsal_export_context_t *export_context;   /* Must be the first entry in this structure */
  posixfsal_cred_t credential;
  fsal_posixdb_conn *p_conn;
} posixfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct fs_specific_initinfo__
{
  fsal_posixdb_conn_params_t dbparams;
} posixfs_specific_initinfo_t;

/**< directory cookie */
typedef union {
 struct 
  {
    off_t cookie;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} posixfsal_cookie_t;

typedef struct fsal_lockdesc__
{
  struct flock flock;
} posixfsal_lockdesc_t;

/* Directory stream descriptor. */

typedef struct fsal_dir__
{
  DIR *p_dir;
  posixfsal_op_context_t context;       /* credential for accessing the directory */
  fsal_path_t path;
  posixfsal_handle_t handle;
#ifdef _USE_POSIXDB_READDIR_BLOCK
  fsal_posixdb_child *p_dbentries;
  int dbentries_count;        /**< if -1 then do not try to fill p_dbentries */
#endif
} posixfsal_dir_t;

#ifdef _FSAL_POSIX_USE_STREAM
typedef struct fsal_file__
{
  FILE *p_file;
  int ro;                       /* read only file ? */
} posixfsal_file_t;

//#define FSAL_FILENO( p_fsal_file )  ( fileno( (p_fsal_file)->p_file ) )

#else
typedef struct fsal_file__
{
  int filefd;
  int ro;                       /* read only file ? */
} posixfsal_file_t;

//#define FSAL_FILENO(p_fsal_file)  ((p_fsal_file)->filefd )

#endif                          /* _FSAL_POSIX_USE_STREAM */


#endif                          /* _FSAL_TYPES__SPECIFIC_H */
