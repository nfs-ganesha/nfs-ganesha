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

#define CONF_LABEL_FS_SPECIFIC   "GPFS"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define FSAL_MAX_NAME_LEN   NAME_MAX
#define FSAL_MAX_PATH_LEN   PATH_MAX

#define FSAL_NGROUPS_MAX  32

#define FSAL_NAME_INITIALIZER {"",0}
#define FSAL_PATH_INITIALIZER {"",0}

#define FSAL_GPFS_HANDLE_LEN 64
#define FSAL_GPFS_FSHANDLE_LEN 64

/** the following come from using the character driver */

#define AT_FDCWD   -100

#define OPENHANDLE_HANDLE_LEN 20
#define OPENHANDLE_DRIVER_MAGIC     'O'
#define OPENHANDLE_NAME_TO_HANDLE _IOWR(OPENHANDLE_DRIVER_MAGIC, 0, struct name_handle_arg)
#define OPENHANDLE_OPEN_BY_HANDLE _IOWR(OPENHANDLE_DRIVER_MAGIC, 1, struct open_arg)
#define OPENHANDLE_LINK_BY_FD     _IOWR(OPENHANDLE_DRIVER_MAGIC, 2, struct link_arg)
#define OPENHANDLE_READLINK_BY_FD _IOWR(OPENHANDLE_DRIVER_MAGIC, 3, struct readlink_arg)

/**
 *  The following structures are also defined in the kernel module,
 *  and if any change happens it needs to happen both places.  They
 *  are the same except for the change of file_handle.f_handle to a
 *  static 20 character array to work better with the way that ganesha
 *  does memory management.
 */

struct file_handle
{
  int handle_size;
  int handle_type;
  /* file identifier */
  unsigned char f_handle[OPENHANDLE_HANDLE_LEN];
};

/**
 * name_handle_arg: 
 * 
 * this structure is used in 3 ways.  If the dfd is AT_FWCWD and name
 * is a full path, it returns the handle to the file.
 * 
 * It can also get the same handle by having dfd be the parent
 * directory file handle and the name be the local file name.
 *
 * Lastly, if dfd is actually the file handle for the file, and name
 * == NULL, it can be used to get the handle directly from the file
 * descriptor.
 *
 */

struct name_handle_arg
{
  int dfd;
  int flag;
  char *name;
  struct file_handle *handle;
};

struct open_arg
{
  int mountdirfd;
  int flags;
  int openfd;
  struct file_handle *handle;
};

struct link_arg
{
  int file_fd;
  int dir_fd;
  char *name;
};

struct readlink_arg
{
  int fd;
  char *buffer;
  int size;
};
/** end of open by handle structures */

#ifndef _USE_SHARED_FSAL

#define fsal_handle_t gpfsfsal_handle_t
#define fsal_op_context_t gpfsfsal_op_context_t
#define fsal_file_t gpfsfsal_file_t
#define fsal_dir_t gpfsfsal_dir_t
#define fsal_export_context_t gpfsfsal_export_context_t
#define fsal_lockdesc_t gpfsfsal_lockdesc_t
#define fsal_cookie_t gpfsfsal_cookie_t
#define fs_specific_initinfo_t gpfsfs_specific_initinfo_t
#define fsal_cred_t gpfsfsal_cred_t

#endif

typedef struct
{
//  unsigned int fsid[2];
  struct file_handle handle;
} gpfsfsal_handle_t;  /**< FS object handle */

/** Authentification context.    */

typedef struct 
{
  uid_t user;
  gid_t group;
  fsal_count_t nbgroups;
  gid_t alt_groups[FSAL_NGROUPS_MAX];
} gpfsfsal_cred_t;

typedef struct
{
  /* Warning: This string is not currently filled in or used. */
  char mount_point[FSAL_MAX_PATH_LEN];

  int open_by_handle_fd;
  int mount_root_fd;
  fsal_handle_t mount_root_handle;
  unsigned int fsid[2];
} gpfsfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( _pexport_context ) (uint64_t)((_pexport_context)->dev_id)

typedef struct
{
  fsal_export_context_t *export_context;        /* Must be the first entry in this structure */
  fsal_cred_t credential;
} gpfsfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct 
{
  char gpfs_mount_point[MAXPATHLEN];
  char open_by_handle_dev_file[MAXPATHLEN];
} gpfsfs_specific_initinfo_t;

/**< directory cookie */
typedef union {
 struct
 {
  off_t cookie;
 } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} gpfsfsal_cookie_t;

// static const fsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { 0 };

typedef struct
{
  struct flock flock;
} gpfsfsal_lockdesc_t;

/* Directory stream descriptor. */

typedef struct
{
  int fd;
  gpfsfsal_op_context_t context;    /* credential for accessing the directory */
  fsal_path_t path;
  unsigned int dir_offset;
  gpfsfsal_handle_t handle;
} gpfsfsal_dir_t;

typedef struct fsal_file__
{
  int fd;
  int ro;                       /* read only file ? */
} gpfsfsal_file_t;

//#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->fd )

#endif                          /* _FSAL_TYPES__SPECIFIC_H */
