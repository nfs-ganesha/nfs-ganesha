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

#ifndef LPX64
#define LPX64 "%#llx"
#endif

#ifndef LPX64i
#define LPX64i "%llx"
#endif

#ifndef DFID_NOBRACE
#define DFID_NOBRACE    LPX64":0x%x:0x%x"
#endif

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

#define MAX_LUSTRE_FSNAME 128
typedef struct lustrefsal_export_context_t
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */

  char mount_point[FSAL_MAX_PATH_LEN];
  unsigned int mnt_len;         /* for optimizing concatenation */
  char fsname[MAX_LUSTRE_FSNAME];
  dev_t dev_id;
} lustrefsal_export_context_t;

//#define FSAL_EXPORT_CONTEXT_SPECIFIC( _pexport_context ) (uint64_t)((_pexport_context)->dev_id)

typedef struct
{
  lustrefsal_export_context_t *export_context;  /* Must be the first entry in this structure */
  struct user_credentials credential;
} lustrefsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( pcontext->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( pcontext->credential.group )

typedef struct
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

#define FSAL_SET_PCOOKIE_BY_OFFSET( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   ((lustrefsal_cookie_t *)__pfsal_cookie)->data.cookie = (off_t)__cookie ; \
} while( 0 )

#define FSAL_SET_OFFSET_BY_PCOOKIE( __pfsal_cookie, __cookie )           \
do                                                                       \
{                                                                        \
   __cookie =  ((lustrefsal_cookie_t *)__pfsal_cookie)->data.cookie ;       \
} while( 0 )


//static const lustrefsal_cookie_t FSAL_READDIR_FROM_BEGINNING = { 0 };

/* Directory stream descriptor. */

typedef struct
{
  DIR *p_dir;
  lustrefsal_op_context_t context;      /* credential for accessing the directory */
  fsal_path_t path;
  lustrefsal_handle_t handle;
} lustrefsal_dir_t;

typedef struct
{
  int fd;
  int ro;                       /* read only file ? */
} lustrefsal_file_t;

//#define FSAL_FILENO( p_fsal_file )  ( (p_fsal_file)->fd )


#endif                          /* _FSAL_TYPES__SPECIFIC_H */
