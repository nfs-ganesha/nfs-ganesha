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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <inttypes.h>
#include "config_parsing.h"
#include "err_fsal.h"

#include <libzfswrap.h>

#define ZFS_SNAP_DIR ".zfs"

#define ZFS_SNAP_DIR_INODE 2

typedef struct zfs_file_handle
{
    inogen_t zfs_handle;
    object_file_type_t type;
    char i_snap;
} zfs_file_handle_t;

typedef struct
{
  struct user_cred credential;
  int thread_connect_array[32];

} zfsfsal_op_context_t;

typedef struct
{
  creden_t cred;
  libzfswrap_vnode_t *p_vnode;
  struct zfs_file_handle handle;

} zfsfsal_dir_t;

typedef struct
{
  creden_t cred;
  struct zfs_file_handle handle;
  off_t current_offset;
  int flags;
  libzfswrap_vnode_t *p_vnode;
  int is_closed;

} zfsfsal_file_t;

static inline size_t zfs_sizeof_handle(struct zfs_file_handle *hdl)
{
  return (size_t)sizeof( struct zfs_file_handle ) ;
}

typedef struct
{
    off_t cookie;
} zfsfsal_cookie_t;

typedef struct
{
  char psz_zpool[MAXNAMLEN];

  int auto_snapshots;

  char psz_snap_hourly_prefix[MAXNAMLEN];
  int snap_hourly_time;
  int snap_hourly_number;

  char psz_snap_daily_prefix[MAXNAMLEN];
  int snap_daily_time;
  int snap_daily_number;

} zfsfs_specific_initinfo_t;

#endif                          /* _FSAL_TYPES_SPECIFIC_H */
