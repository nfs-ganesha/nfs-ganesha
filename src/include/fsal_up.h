/*
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
 *---------------------------------------
 */

/**
 * \file    fsal_up.h
 * \date    $Date: 2011/09/29 $
 */

#ifndef _FSAL_UP_H
#define _FSAL_UP_H

#ifdef _USE_FSAL_UP

#include "fsal_types.h"
#include "cache_inode.h"
#include "nfs_exports.h"

/* In the "static" case, original types are used, this is safer */
#define MAX_FILTER_NAMELEN 255

#define FSAL_UP_EVENT_CREATE     1
#define FSAL_UP_EVENT_UNLINK     2
#define FSAL_UP_EVENT_RENAME     3
#define FSAL_UP_EVENT_COMMIT     4
#define FSAL_UP_EVENT_WRITE      5
#define FSAL_UP_EVENT_LINK       6
#define FSAL_UP_EVENT_LOCK_GRANT 7
#define FSAL_UP_EVENT_LOCK_AVAIL 8
#define FSAL_UP_EVENT_OPEN       9
#define FSAL_UP_EVENT_CLOSE      10
#define FSAL_UP_EVENT_SETATTR    11
#define FSAL_UP_EVENT_UPDATE     12
#define FSAL_UP_EVENT_INVALIDATE 13

/* Defines for the flags in callback_arg, keep up to date with CXIUP_xxx */
#define FSAL_UP_NLINK        0x00000001   /* update nlink */
#define FSAL_UP_MODE         0x00000002   /* update mode and ctime */
#define FSAL_UP_OWN          0x00000004   /* update mode,uid,gid and ctime */
#define FSAL_UP_SIZE         0x00000008   /* update fsize */
#define FSAL_UP_SIZE_BIG     0x00000010   /* update fsize if bigger */
#define FSAL_UP_TIMES        0x00000020   /* update all times */
#define FSAL_UP_ATIME        0x00000040   /* update atime only */
#define FSAL_UP_PERM         0x00000080   /* update fields needed for permission checking*/
#define FSAL_UP_RENAME       0x00000100   /* this is a rename op */


typedef struct fsal_up_filter_list_t_
{
  char name[MAX_FILTER_NAMELEN];
  struct fsal_up_filter_list_t_ *next;
} fsal_up_filter_list_t;

typedef struct fsal_up_event_bus_parameter_t_
{
} fsal_up_event_bus_parameter_t;

typedef struct fsal_up_event_bus_context_t_
{
  fsal_export_context_t FS_export_context;
  pool_t *event_pool;
  pthread_mutex_t *event_pool_lock;
} fsal_up_event_bus_context_t;

typedef struct fsal_up_event_data_context_t_
{
  cache_inode_fsal_data_t fsal_data;
} fsal_up_event_data_context_t;

typedef struct fsal_up_arg_t_
{
  struct exportlist__ *export_entry;
} fsal_up_arg_t;

typedef struct fsal_up_event_bus_filter_t_
{
} fsal_up_event_bus_filter_t;

typedef struct fsal_up_event_data_create_t_
{
} fsal_up_event_data_create_t;

typedef struct fsal_up_event_data_unlink_t_
{
} fsal_up_event_data_unlink_t;

typedef struct fsal_up_event_data_rename_t_
{
} fsal_up_event_data_rename_t;

typedef struct fsal_up_event_data_commit_t_
{
} fsal_up_event_data_commit_t;

typedef struct fsal_up_event_data_write_t_
{
} fsal_up_event_data_write_t;

typedef struct fsal_up_event_data_link_t_
{
} fsal_up_event_data_link_t;

typedef struct fsal_up_event_data_lock_grant_t_
{
  void              * lock_owner;
  fsal_lock_param_t   lock_param;
} fsal_up_event_data_lock_grant_t;

typedef struct fsal_up_event_data_open_t_
{
} fsal_up_event_data_open_t;

typedef struct fsal_up_event_data_close_t_
{
} fsal_up_event_data_close_t;

typedef struct fsal_up_event_data_setattr_
{
} fsal_up_event_data_setattr_t;

typedef struct fsal_up_event_data_invalidate_
{
} fsal_up_event_data_invalidate_t;

typedef struct fsal_up_event_data_update_
{
  struct stat upu_stat_buf;
  int upu_flags;
} fsal_up_event_data_update_t;

typedef struct fsal_up_event_data__
{
  union {
    fsal_up_event_data_create_t create;
    fsal_up_event_data_unlink_t unlink;
    fsal_up_event_data_rename_t rename;
    fsal_up_event_data_commit_t commit;
    fsal_up_event_data_write_t write;
    fsal_up_event_data_link_t link;
    fsal_up_event_data_lock_grant_t lock_grant;
    fsal_up_event_data_open_t open;
    fsal_up_event_data_close_t close;
    fsal_up_event_data_setattr_t setattr;
    fsal_up_event_data_update_t update;
    fsal_up_event_data_invalidate_t invalidate;
  } type;
  /* Common data most functions will need. */
  fsal_up_event_data_context_t event_context;
} fsal_up_event_data_t;

typedef fsal_status_t (fsal_up_event_process_func_t) (fsal_up_event_data_t * arg);

typedef struct fsal_up_event_t_
{
  struct glist_head event_list;
  fsal_up_event_process_func_t   * event_process_func;
  unsigned int event_type;
  fsal_up_event_data_t event_data;
} fsal_up_event_t;

typedef struct fsal_up_event_functions__
{
  fsal_status_t (*fsal_up_create) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_unlink) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_rename) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_commit) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_write) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_link) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_lock_grant) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_lock_avail) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_open) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_close) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_setattr) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_update) (fsal_up_event_data_t * pevdata );
  fsal_status_t (*fsal_up_invalidate) (fsal_up_event_data_t * pevdata );
} fsal_up_event_functions_t;

#define FSAL_UP_DUMB_TYPE "DUMB"
fsal_up_event_functions_t *get_fsal_up_dumb_functions();

#endif /* _USE_FSAL_UP */
#endif /* _FSAL_UP_H */
