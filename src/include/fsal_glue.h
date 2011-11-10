/*
 *
 *
 * Copyright CEA/DAM/DIF  (2010)
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
 * \file    fsal_glue.h
 * \date    $Date: 2010/07/01 12:45:27 $
 *
 *
 */

#ifndef _FSAL_GLUE_H
#define _FSAL_GLUE_H

#include "fsal_types.h"
#include "fsal_glue_const.h"

/* In the "static" case, original types are used, this is safer */
#if defined(_USE_SHARED_FSAL) || \
	defined(_USE_POSIX) || \
	defined(_USE_VFS) || \
        defined(_USE_XFS) || \
	defined(_USE_GPFS) || \
	defined(_USE_ZFS) || \
	defined(_USE_SNMP) || \
	defined(_USE_PROXY) || \
	defined(_USE_LUSTRE) || \
	defined(_USE_FUSE)

/* Allow aliasing of fsal_handle_t since FSALs will be
 * casting between pointer types
 */
typedef struct
{
  char data[FSAL_HANDLE_T_SIZE];
} __attribute__((__may_alias__)) fsal_handle_t;

typedef fsal_handle_t fsal_handle_storage_t ;

/* NOTE: this structure is very dangerous as noted by the comments
 * in the individual fsal_types.h files.  It harkens back to the
 * days of fortran commons...  We let it go for now as we
 * refactor.  The first element must be identical throughout!
 */

typedef struct
{
  fsal_staticfsinfo_t * fe_static_fs_info;

  char                  fe_data[FSAL_EXPORT_CONTEXT_T_SIZE];
} fsal_export_context_t;

/* NOTE: this structure is very dangerous as noted by the comments
 * in the individual fsal_types.h files.  It harkens back to the
 * days of fortran commons...  We let it go for now as we
 * refactor.  The first 2 elements must be identical throughout!
 */

typedef struct
{
  fsal_export_context_t *export_context;
  struct user_credentials credential;
  char data[FSAL_OP_CONTEXT_T_SIZE]; /* slightly bigger (for now) */
} fsal_op_context_t;

typedef struct
{
  char data[FSAL_DIR_T_SIZE];
} fsal_dir_t;

typedef struct
{
  char data[FSAL_FILE_T_SIZE];
} fsal_file_t;

typedef struct
{
  char data[FSAL_COOKIE_T_SIZE];
} fsal_cookie_t;

typedef struct
{
  char data[FSAL_CRED_T_SIZE];
} fsal_cred_t;

typedef struct
{
  char data[FSAL_FS_SPECIFIC_INITINFO_T];
} fs_specific_initinfo_t;

#endif                          /* USE_SHARED_FSAL */

#endif                          /* _FSAL_GLUE_H */
