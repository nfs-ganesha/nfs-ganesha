/*
 * Copyright (C) 2010 The Linx Box Corporation
 * Contributor : Adam C. Emerson
 *
 * Some Portions Copyright CEA/DAM/DIF  (2008)
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

# define CONF_LABEL_FS_SPECIFIC   "CEPH"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/param.h>
#include "config_parsing.h"
#include "err_fsal.h"
#include <cephfs/libcephfs.h>
//#include "ceph/libceph.h"
#include <pthread.h>
#include "fsal_glue_const.h"

#define fsal_handle_t cephfsal_handle_t
#define fsal_op_context_t cephfsal_op_context_t
#define fsal_file_t cephfsal_file_t
#define fsal_dir_t cephfsal_dir_t
#define fsal_export_context_t cephfsal_export_context_t
#define fsal_lockdesc_t cephfsal_lockdesc_t
#define fsal_cookie_t cephfsal_cookie_t
#define fs_specific_initinfo_t cephfs_specific_initinfo_t

typedef union {
  struct
  {
    vinodeno_t vi;
    uint64_t parent_ino;
    uint32_t parent_hash;
#ifdef _PNFS
    struct ceph_file_layout layout;
    uint64_t snapseq;
#endif /* _PNFS */
   } data;
  char pad[FSAL_HANDLE_T_SIZE];
} cephfsal_handle_t;

#define VINODE(fh) ((fh)->data.vi)

typedef struct fsal_export_context__
{
  fsal_staticfsinfo_t * fe_static_fs_info;     /* Must be the first entry in this structure */
  exportlist_t        * fe_export;

  char mount[FSAL_MAX_PATH_LEN];
  struct ceph_mount_info *cmount;
} cephfsal_export_context_t;

typedef struct fsal_op_context__
{
  cephfsal_export_context_t *export_context;
  struct user_credentials credential;
  msectimer_t latency;
  unsigned int count;
} cephfsal_op_context_t;

#define FSAL_OP_CONTEXT_TO_UID( pcontext ) ( (pcontext)->credential.user )
#define FSAL_OP_CONTEXT_TO_GID( pcontext ) ( (pcontext)->credential.group )

typedef struct fs_specific_initinfo__
{
  char cephserver[FSAL_MAX_NAME_LEN+1];
} cephfs_specific_initinfo_t;


typedef union {
  struct {
    loff_t cookie;
  } data;
  char pad[FSAL_COOKIE_T_SIZE];
} cephfsal_cookie_t;

#define COOKIE(c) ((c).data.cookie)

typedef void *cephfsal_lockdesc_t;

typedef struct {
  vinodeno_t vi;
  struct ceph_dir_result *dh;
  cephfsal_op_context_t ctx;
} cephfsal_dir_t;

#define DH(dir) (dir->dh)

typedef struct {
  Fh* fh;
  vinodeno_t vi;
  cephfsal_op_context_t ctx;
} cephfsal_file_t;

#define FH(file) ((file)->fh)

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
