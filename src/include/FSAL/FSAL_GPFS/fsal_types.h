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
#include <gpfs_nfs.h>
#include <stddef.h> /* For offsetof */


#include "fsal_glue_const.h"

#define fsal_handle_t gpfsfsal_handle_t
#define fsal_op_context_t gpfsfsal_op_context_t
#define fsal_file_t gpfsfsal_file_t
#define fsal_dir_t gpfsfsal_dir_t
#define fsal_export_context_t gpfsfsal_export_context_t
#define fsal_lockdesc_t gpfsfsal_lockdesc_t
#define fsal_cookie_t gpfsfsal_cookie_t
#define fs_specific_initinfo_t gpfsfs_specific_initinfo_t
#define fsal_cred_t gpfsfsal_cred_t

/*
 * labels in the config file
 */

#define CONF_LABEL_FS_SPECIFIC   "GPFS"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */

#define FSAL_GPFS_HANDLE_LEN 64
#define FSAL_GPFS_FSHANDLE_LEN 64

/** the following come from using the character driver */

#define AT_FDCWD   -100

#define OPENHANDLE_HANDLE_LEN 40
#define OPENHANDLE_KEY_LEN 28
#define OPENHANDLE_VERSION 1
#define OPENHANDLE_DRIVER_MAGIC     'O'
#define OPENHANDLE_OFFSET_OF_FILEID (2 * sizeof(int))

/**
 *  The following structures are also defined in the kernel module,
 *  and if any change happens it needs to happen both places.  They
 *  are the same except for the change of file_handle.f_handle to a
 *  static 20 character array to work better with the way that ganesha
 *  does memory management.
 */

/* some versions of GPFS don't have this in their headers */
#ifndef H_GPFS_NFS
struct gpfs_file_handle
{
   u_int32_t handle_size;
   u_int32_t handle_type;
   u_int16_t handle_version;
   u_int16_t handle_key_size;
  /* file identifier */
  unsigned char f_handle[OPENHANDLE_HANDLE_LEN];
};
#endif

static inline size_t gpfs_sizeof_handle(struct gpfs_file_handle *hdl)
{
  return offsetof(struct gpfs_file_handle, f_handle) + hdl->handle_key_size;
}
/** end of open by handle structures */

/* Allow aliasing of fsal_handle_t since FSALs will be
 * casting between pointer types
 */
typedef struct
{
  struct
  {
    //  unsigned int fsid[2];
    struct gpfs_file_handle handle;
  } data ;
} __attribute__((__may_alias__)) gpfsfsal_handle_t;  /**< FS object handle */

/** Authentification context.    */

typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  /* Warning: This string is not currently filled in or used. */
  char mount_point[FSAL_MAX_PATH_LEN];

  int mount_root_fd;
  gpfsfsal_handle_t mount_root_handle;
  unsigned int fsid[2];
} gpfsfsal_export_context_t;

#define FSAL_EXPORT_CONTEXT_SPECIFIC( _pexport_context ) (uint64_t)((_pexport_context)->dev_id)

typedef struct
{
  gpfsfsal_export_context_t *export_context;        /* Must be the first entry in this structure */
  struct user_credentials credential;
} gpfsfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct
{
  int  use_kernel_module_interface;
  char open_by_handle_dev_file[MAXPATHLEN];
} gpfsfs_specific_initinfo_t;

/**< directory cookie */
typedef union {
 struct
 {
  off_t cookie;
 } data ;
  char pad[FSAL_COOKIE_T_SIZE];
} gpfsfsal_cookie_t;

// static const fsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { 0 };

/* Directory stream descriptor. */

typedef struct
{
  int fd;
  gpfsfsal_op_context_t context;    /* credential for accessing the directory */
  fsal_path_t path;
  unsigned int dir_offset;
  gpfsfsal_handle_t handle;
} gpfsfsal_dir_t;

typedef struct
{
  int fd;
  int ro;                       /* read only file ? */
} gpfsfsal_file_t;

//#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->fd )

/* Define the buffer size for GPFS NFS4 ACL. */
#define GPFS_ACL_BUF_SIZE 0x1000

/* A set of buffers to retrieve multiple attributes at the same time. */
typedef struct fsal_xstat__
{
  int attr_valid;
  struct stat buffstat;
  char buffacl[GPFS_ACL_BUF_SIZE];
} gpfsfsal_xstat_t;

#endif                          /* _FSAL_TYPES__SPECIFIC_H */
