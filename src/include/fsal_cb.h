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
 * ---------------------------------------
 */

/**
 * \file    fsal_cb.h
 * \date    $Date: 2011/09/29 $
 */

#ifndef _FSAL_CB_H
#define _FSAL_CB_H

#ifdef _USE_FSAL_CB

#include "fsal_types.h"
#include "cache_inode.h"
#include "nfs_exports.h"

/* In the "static" case, original types are used, this is safer */
#define MAX_FILTER_NAMELEN 255

#define FSAL_CB_EVENT_CREATE     1
#define FSAL_CB_EVENT_UNLINK     2
#define FSAL_CB_EVENT_RENAME     3
#define FSAL_CB_EVENT_COMMIT     4
#define FSAL_CB_EVENT_WRITE      5
#define FSAL_CB_EVENT_LINK       6
#define FSAL_CB_EVENT_LOCK       7
#define FSAL_CB_EVENT_LOCKU      8
#define FSAL_CB_EVENT_OPEN       9
#define FSAL_CB_EVENT_CLOSE      10
#define FSAL_CB_EVENT_SETATTR    11
#define FSAL_CB_EVENT_INVALIDATE 12

typedef struct fsal_cb_filter_list_t_
{
  char name[MAX_FILTER_NAMELEN];
  struct fsal_cb_filter_list_t_ *next;
} fsal_cb_filter_list_t;

typedef struct fsal_cb_event_bus_parameter_t_
{
} fsal_cb_event_bus_parameter_t;

typedef struct fsal_cb_event_bus_context_t_
{
  fsal_export_context_t FS_export_context;
  struct prealloc_pool *event_pool;
} fsal_cb_event_bus_context_t;

typedef struct fsal_cb_event_data_context_t_
{
  cache_inode_fsal_data_t fsal_data;
  hash_table_t *ht;
} fsal_cb_event_data_context_t;

typedef struct fsal_cb_arg_t_
{
  struct exportlist__ *export_entry;
} fsal_cb_arg_t;

typedef struct fsal_cb_event_bus_filter_t_
{
} fsal_cb_event_bus_filter_t;

typedef struct fsal_cb_event_data_create_t_
{
} fsal_cb_event_data_create_t;

typedef struct fsal_cb_event_data_unlink_t_
{
} fsal_cb_event_data_unlink_t;

typedef struct fsal_cb_event_data_rename_t_
{
} fsal_cb_event_data_rename_t;

typedef struct fsal_cb_event_data_commit_t_
{
} fsal_cb_event_data_commit_t;

typedef struct fsal_cb_event_data_write_t_
{
} fsal_cb_event_data_write_t;

typedef struct fsal_cb_event_data_link_t_
{
} fsal_cb_event_data_link_t;

typedef struct fsal_cb_event_data_lock_t_
{
  fsal_lock_param_t lock_param;
} fsal_cb_event_data_lock_t;

typedef struct fsal_cb_event_data_locku_t_
{
} fsal_cb_event_data_locku_t;

typedef struct fsal_cb_event_data_open_t_
{
} fsal_cb_event_data_open_t;

typedef struct fsal_cb_event_data_close_t_
{
} fsal_cb_event_data_close_t;

typedef struct fsal_cb_event_data_setattr_
{
} fsal_cb_event_data_setattr_t;

typedef struct fsal_cb_event_data_invalidate_
{
} fsal_cb_event_data_invalidate_t;

typedef struct fsal_cb_event_data__
{
  union {
    fsal_cb_event_data_create_t create;
    fsal_cb_event_data_unlink_t unlink;
    fsal_cb_event_data_rename_t rename;
    fsal_cb_event_data_commit_t commit;
    fsal_cb_event_data_write_t write;
    fsal_cb_event_data_link_t link;
    fsal_cb_event_data_lock_t lock;
    fsal_cb_event_data_locku_t locku;
    fsal_cb_event_data_open_t open;
    fsal_cb_event_data_close_t close;
    fsal_cb_event_data_setattr_t setattr;
    fsal_cb_event_data_invalidate_t invalidate;
  } type;
  /* Common data most functions will need. */
  fsal_cb_event_data_context_t event_context;
} fsal_cb_event_data_t;

typedef struct fsal_cb_event_t_
{
  unsigned int event_type;
  fsal_cb_event_data_t event_data;
  struct fsal_cb_event_t_ *next_event;
} fsal_cb_event_t;

typedef struct fsal_cb_event_functions__
{
  fsal_status_t (*fsal_cb_create) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_unlink) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_rename) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_commit) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_write) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_link) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_lock) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_locku) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_open) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_close) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_setattr) (fsal_cb_event_data_t * pevdata );
  fsal_status_t (*fsal_cb_invalidate) (fsal_cb_event_data_t * pevdata );
} fsal_cb_event_functions_t;

#define FSAL_CB_DUMB_TYPE "DUMB"
fsal_cb_event_functions_t *get_fsal_cb_dumb_functions();

#endif /* _USE_FSAL_CB */
#endif /* _FSAL_CB_H */
