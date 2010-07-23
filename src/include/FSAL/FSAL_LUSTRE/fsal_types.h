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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#define LPX64 "%#llx"
#include <asm/types.h>
#include <lustre/liblustreapi.h>

/*
 * labels in the config file
 */

#define CONF_LABEL_FS_SPECIFIC   "LUSTRE"

/* -------------------------------------------
 *      POSIX FS dependant definitions
 * ------------------------------------------- */
#include "fsal_glue_const.h"

#define FSAL_NGROUPS_MAX  32

#define fsal_handle_t lustrefsal_handle_t
#define fsal_op_context_t lustrefsal_op_context_t
#define fsal_file_t lustrefsal_file_t
#define fsal_dir_t lustrefsal_dir_t
#define fsal_export_context_t lustrefsal_export_context_t
#define fsal_lockdesc_t lustrefsal_lockdesc_t
#define fsal_cookie_t lustrefsal_cookie_t
#define fs_specific_initinfo_t lustrefs_specific_initinfo_t
#define fsal_cred_t lustrefsal_cred_t


typedef union {
 struct
  {
    lustre_fid fid;
    /* used for FSAL_DIGEST_FILEID */
    unsigned long long inode;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_HANDLE_T_SIZE];
#endif
} lustrefsal_handle_t;  /**< FS object handle */

/** Authentification context.    */

typedef struct lustrefsal_cred__
{
  uid_t user;
  gid_t group;
  fsal_count_t nbgroups;
  gid_t alt_groups[FSAL_NGROUPS_MAX];
} lustrefsal_cred_t;

typedef struct lustrefsal_export_context_t
{
  char mount_point[FSAL_MAX_PATH_LEN];
  unsigned int mnt_len;         /* for optimizing concatenation */
  dev_t dev_id;
} lustrefsal_export_context_t;

//#define FSAL_EXPORT_CONTEXT_SPECIFIC( _pexport_context ) (uint64_t)((_pexport_context)->dev_id)

typedef struct
{
  lustrefsal_export_context_t *export_context;  /* Must be the first entry in this structure */
  lustrefsal_cred_t credential;
} lustrefsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct lustrefs_specific_initinfo__
{
  int dummy;
} lustrefs_specific_initinfo_t;

/**< directory cookie */
typedef union
{
 struct 
  {
    off_t cookie;
  } data ;
#ifdef _BUILD_SHARED_FSAL
  char pad[FSAL_COOKIE_T_SIZE];
#endif
} lustrefsal_cookie_t;

//static const lustrefsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { 0 };

typedef void *lustrefsal_lockdesc_t;   /**< not implemented for now */

/* Directory stream descriptor. */

typedef struct lustrefsal_dir__
{
  DIR *p_dir;
  lustrefsal_op_context_t context;      /* credential for accessing the directory */
  fsal_path_t path;
  lustrefsal_handle_t handle;
} lustrefsal_dir_t;

typedef struct lustrefsal_file__
{
  int fd;
  int ro;                       /* read only file ? */
} lustrefsal_file_t;

//#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->fd )


#endif                          /* _FSAL_TYPES__SPECIFIC_H */
