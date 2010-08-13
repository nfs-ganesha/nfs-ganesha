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
 * \file    cache_inode_async.h
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/12 $
 * \version 1.0
 * \brief   Asynchronous (write back) management of the cached inode layer. 
 *
 * cache_inode_async.h : Asynchronous (write back) management of the cached inode layer
 *
 *
 */

#ifndef _CACHE_INODE__ASYNC_H
#define _CACHE_INODE__ASYNC_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"

typedef enum cache_inode_async_health__
{ CACHE_INODE_ASYNC_STAYING_ALIVE = 1,
  CACHE_INODE_ASYNC_ZOMBIE = 2,
  CACHE_INODE_ASYNC_DEAD = 3
} cache_inode_async_health_t;
typedef enum cache_inode_async_addr_type__
{ CACHE_INODE_ASYNC_ADDR_DIRECT = 1,
  CACHE_INODE_ASYNC_ADDR_INDIRECT = 2
} cache_inode_async_addr_type_t;

typedef struct cache_inode_synclet_data__
{
  unsigned int my_index;
  pthread_cond_t op_condvar;
  pthread_mutex_t mutex_op_condvar;
  fsal_op_context_t root_fsal_context;
  LRU_list_t *op_lru;
} cache_inode_synclet_data_t;

typedef enum cache_inode_async_op_type__
{
  CACHE_INODE_ASYNC_OP_CREATE = 0,
  CACHE_INODE_ASYNC_OP_LINK = 1,
  CACHE_INODE_ASYNC_OP_REMOVE = 2,
  CACHE_INODE_ASYNC_OP_RENAME_SRC = 3,
  CACHE_INODE_ASYNC_OP_RENAME_DST = 4,
  CACHE_INODE_ASYNC_OP_SETATTR = 5,
  CACHE_INODE_ASYNC_OP_TRUNCATE = 6
} cache_inode_async_op_type_t;

typedef struct cache_inode_async_op_create_args__
{
  fsal_handle_t *pfsal_handle_dir_pre;
  fsal_handle_t *pfsal_handle_obj_pre;
  fsal_u64_t fileid;
  fsal_handle_t *pfsal_handle_dir;
  fsal_nodetype_t object_type;
  fsal_name_t name;
  fsal_accessmode_t mode;
} cache_inode_async_op_create_args_t;

typedef struct cache_inode_async_op_create_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_create_res_t;

typedef struct cache_inode_async_op_link_args__
{
  fsal_handle_t *pfsal_handle_src;
  fsal_handle_t *pfsal_handle_dirdest;
  fsal_name_t name_link;
} cache_inode_async_op_link_args_t;

typedef struct cache_inode_async_op_link_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_link_res_t;

typedef struct cache_inode_async_op_remove_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_name_t name;
} cache_inode_async_op_remove_args_t;

typedef struct cache_inode_async_op_remove_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_remove_res_t;

typedef struct cache_inode_async_op_rename_args__
{
  fsal_handle_t *pfsal_handle_dirsrc;
  fsal_name_t name_src;
  fsal_handle_t *pfsal_handle_dirdest;
  fsal_name_t name_dest;
} cache_inode_async_op_rename_args_t;

typedef struct cache_inode_async_op_rename_res__
{
  fsal_attrib_list_t attrsrc;
  fsal_attrib_list_t attrdest;
} cache_inode_async_op_rename_res_t;

typedef struct cache_inode_async_op_setattr_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_attrib_list_t attr;
} cache_inode_async_op_setattr_args_t;

typedef struct cache_inode_async_op_setattr_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_setattr_res_t;

typedef struct cache_inode_async_op_truncate_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_size_t size;
} cache_inode_async_op_truncate_args_t;

typedef struct cache_inode_async_op_truncate_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_truncate_res_t;

typedef union cache_inode_async_op_args__
{
  cache_inode_async_op_create_args_t create;
  cache_inode_async_op_link_args_t link;
  cache_inode_async_op_remove_args_t remove;
  cache_inode_async_op_rename_args_t rename;
  cache_inode_async_op_setattr_args_t setattr;
  cache_inode_async_op_truncate_args_t truncate;
} cache_inode_async_op_args_t;

typedef union cache_inode_async_op_res__
{
  cache_inode_async_op_create_res_t create;
  cache_inode_async_op_link_res_t link;
  cache_inode_async_op_remove_res_t remove;
  cache_inode_async_op_rename_res_t rename;
  cache_inode_async_op_setattr_res_t setattr;
  cache_inode_async_op_truncate_res_t truncate;
} cache_inode_async_op_res_t;

typedef struct cache_inode_async_op_desc__
{
  struct timeval op_time;
  cache_inode_async_op_type_t op_type;
  cache_inode_async_op_args_t op_args;
  cache_inode_async_op_res_t op_res;
  unsigned int synclet_index;
   fsal_status_t(*op_func) (struct cache_inode_async_op_desc__ *);
  fsal_op_context_t fsal_op_context;
  fsal_export_context_t fsal_export_context;
  hash_table_t *ht;
  struct cache_inode_async_op_desc__ *origine_pool;
  pthread_mutex_t *ppool_lock;
  struct cache_inode_async_op_desc__ *next;
  struct cache_inode_async_op_desc__ *next_alloc;
} cache_inode_async_op_desc_t;

typedef struct cache_inode_async_op_queue_param__
{
  LRU_parameter_t lru_param;
} cache_inode_async_op_queue_parameter_t;

void *cache_inode_synclet_thread(void *Arg);
void *cache_inode_asynchronous_dispatcher_thread(void *Arg);

/* Async Operations on FSAL */
fsal_status_t cache_inode_async_create(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_link(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_remove(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_rename_src(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_rename_dst(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_setattr(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_truncate(cache_inode_async_op_desc_t * popasyncdesc);

#endif                          /* _CACHE_INODE__ASYNC_H */
