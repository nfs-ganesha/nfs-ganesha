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
 * \file    mfsl_types.h
 */

#ifndef _MFSL_ASYNC_TYPES_H
#define _MFSL_ASYNC_TYPES_H

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * labels in the config file
 */

#define CONF_LABEL_MFSL_ASYNC          "MFSL_Async"

#define MFSL_ASYNC_DEFAULT_NB_SYNCLETS 10
#define MFSL_ASYNC_DEFAULT_SLEEP_TIME  60
#define MFSL_ASYNC_DEFAULT_BEFORE_GC   10
#define MFSL_ASYNC_DEFAULT_NB_PREALLOCATED_DIRS  10
#define MFSL_ASYNC_DEFAULT_NB_PREALLOCATED_FILES 100

/* other includes */
#include <sys/types.h>
#include <sys/param.h>
#include <pthread.h>
#include <dirent.h>             /* for MAXNAMLEN */
#include "config_parsing.h"
#include "LRU_List.h"
#include "HashTable.h"
#include "err_fsal.h"
#include "err_mfsl.h"

typedef enum mfsl_async_health__
{ MFSL_ASYNC_SYNCHRONOUS = 0,
  MFSL_ASYNC_ASYNCHRONOUS = 1,
  MFSL_ASYNC_NEVER_SYNCED = 2
} mfsl_async_health_t;

typedef struct mfsl_object_specific_data__
{
  fsal_attrib_list_t async_attr;
  unsigned int deleted;
} mfsl_object_specific_data_t;

typedef struct mfsl_object__
{
  fsal_handle_t handle;
  pthread_mutex_t lock;
  mfsl_async_health_t health;
} mfsl_object_t;

typedef struct mfsl_precreated_object__
{
  mfsl_object_t mobject;
  fsal_name_t name;
  fsal_attrib_list_t attr;
  unsigned int inited;
} mfsl_precreated_object_t;

typedef struct mfsl_synclet_context__
{
  pthread_mutex_t lock;
} mfsl_synclet_context_t;

typedef enum mfsl_async_addr_type__
{ MFSL_ASYNC_ADDR_DIRECT = 1,
  MFSL_ASYNC_ADDR_INDIRECT = 2
} mfsl_async_addr_type_t;

typedef struct mfsl_synclet_data__
{
  unsigned int my_index;
  pthread_cond_t op_condvar;
  pthread_mutex_t mutex_op_condvar;
  fsal_op_context_t root_fsal_context;
  mfsl_synclet_context_t synclet_context;
  pthread_mutex_t mutex_op_lru;
  unsigned int passcounter;
  LRU_list_t *op_lru;
} mfsl_synclet_data_t;

typedef enum mfsl_async_op_type__
{
  MFSL_ASYNC_OP_CREATE = 0,
  MFSL_ASYNC_OP_MKDIR = 1,
  MFSL_ASYNC_OP_LINK = 2,
  MFSL_ASYNC_OP_REMOVE = 3,
  MFSL_ASYNC_OP_RENAME = 4,
  MFSL_ASYNC_OP_SETATTR = 5,
  MFSL_ASYNC_OP_TRUNCATE = 6,
  MFSL_ASYNC_OP_SYMLINK = 7
} mfsl_async_op_type_t;

static const char *mfsl_async_op_name[] = { "MFSL_ASYNC_OP_CREATE",
  "MFSL_ASYNC_OP_MKDIR",
  "MFSL_ASYNC_OP_LINK",
  "MFSL_ASYNC_OP_REMOVE",
  "MFSL_ASYNC_OP_RENAME",
  "MFSL_ASYNC_OP_SETATTR",
  "MFSL_ASYNC_OP_TRUNCATE",
  "MFSL_ASYNC_OP_SYMLINK"
};

typedef struct mfsl_async_op_create_args__
{
  fsal_name_t precreate_name;
  mfsl_object_t *pmfsl_obj_dirdest;
  fsal_name_t filename;
  fsal_accessmode_t mode;
  fsal_uid_t owner;
  fsal_gid_t group;
} mfsl_async_op_create_args_t;

typedef struct mfsl_async_op_create_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_create_res_t;

typedef struct mfsl_async_op_mkdir_args__
{
  fsal_name_t precreate_name;
  mfsl_object_t *pmfsl_obj_dirdest;
  fsal_name_t dirname;
  fsal_accessmode_t mode;
  fsal_uid_t owner;
  fsal_gid_t group;
} mfsl_async_op_mkdir_args_t;

typedef struct mfsl_async_op_mkdir_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_mkdir_res_t;

typedef struct mfsl_async_op_link_args__
{
  mfsl_object_t *pmobject_src;
  mfsl_object_t *pmobject_dirdest;
  fsal_name_t name_link;
} mfsl_async_op_link_args_t;

typedef struct mfsl_async_op_link_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_link_res_t;

typedef struct mfsl_async_op_remove_args__
{
  mfsl_object_t *pmobject;
  fsal_name_t name;
} mfsl_async_op_remove_args_t;

typedef struct mfsl_async_op_remove_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_remove_res_t;

typedef struct mfsl_async_op_rename_args__
{
  mfsl_object_t *pmobject_src;
  fsal_name_t name_src;
  mfsl_object_t *pmobject_dirdest;
  fsal_name_t name_dest;
} mfsl_async_op_rename_args_t;

typedef struct mfsl_async_op_rename_res__
{
  fsal_attrib_list_t attrsrc;
  fsal_attrib_list_t attrdest;
} mfsl_async_op_rename_res_t;

typedef struct mfsl_async_op_setattr_args__
{
  mfsl_object_t *pmobject;
  fsal_attrib_list_t attr;
} mfsl_async_op_setattr_args_t;

typedef struct mfsl_async_op_setattr_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_setattr_res_t;

typedef struct mfsl_async_op_truncate_args__
{
  mfsl_object_t *pmobject;
  fsal_size_t size;
} mfsl_async_op_truncate_args_t;

typedef struct mfsl_async_op_truncate_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_truncate_res_t;

typedef struct mfsl_async_op_symlink_args__
{
  fsal_name_t precreate_name;
  fsal_name_t linkname;
  mfsl_object_t *pmobject_dirdest;
} mfsl_async_op_symlink_args_t;

typedef struct mfsl_async_op_symlink_res__
{
  fsal_attrib_list_t attr;
} mfsl_async_op_symlink_res_t;

typedef union mfsl_async_op_args__
{
  mfsl_async_op_create_args_t create;
  mfsl_async_op_mkdir_args_t mkdir;
  mfsl_async_op_link_args_t link;
  mfsl_async_op_remove_args_t remove;
  mfsl_async_op_rename_args_t rename;
  mfsl_async_op_setattr_args_t setattr;
  mfsl_async_op_truncate_args_t truncate;
  mfsl_async_op_symlink_args_t symlink;
} mfsl_async_op_args_t;

typedef union mfsl_async_op_res__
{
  mfsl_async_op_create_res_t create;
  mfsl_async_op_mkdir_res_t mkdir;
  mfsl_async_op_link_res_t link;
  mfsl_async_op_remove_res_t remove;
  mfsl_async_op_rename_res_t rename;
  mfsl_async_op_setattr_res_t setattr;
  mfsl_async_op_truncate_res_t truncate;
  mfsl_async_op_symlink_res_t symlink;
} mfsl_async_op_res_t;

typedef struct mfsl_async_op_desc__
{
  struct timeval op_time;
  mfsl_async_op_type_t op_type;
  mfsl_async_op_args_t op_args;
  mfsl_async_op_res_t op_res;
  mfsl_object_t *op_mobject;
   fsal_status_t(*op_func) (struct mfsl_async_op_desc__ *);
  fsal_op_context_t fsal_op_context;
  caddr_t ptr_mfsl_context;
  unsigned int related_synclet_index;
} mfsl_async_op_desc_t;

void *mfsl_synclet_thread(void *Arg);
void *mfsl_asynchronous_dispatcher_thread(void *Arg);

/* Async Operations on FSAL */
fsal_status_t mfsl_async_create(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_mkdir(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_link(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_remove(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_rename(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_setattr(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_truncate(mfsl_async_op_desc_t * popasyncdesc);
fsal_status_t mfsl_async_symlink(mfsl_async_op_desc_t * popasyncdesc);

typedef struct mfsl_parameter__
{
  unsigned int nb_pre_async_op_desc;             /**< Number of preallocated Aync Op descriptors       */
  unsigned int nb_synclet;                       /**< Number of synclet to be used                     */
  unsigned int async_window_sec;                 /**< Asynchronos Task Dispatcher Window (seconds)     */
  unsigned int async_window_usec;                /**< Asynchronos Task Dispatcher Window (useconds)    */
  unsigned int nb_before_gc;                     /**< Numbers of calls before LRU invalide GC          */
  LRU_parameter_t lru_async_param;               /**< Asynchorous Synclet Tasks LRU parameters         */
  unsigned int nb_pre_create_dirs;               /**< The size of pre-created directories per synclet  */
  unsigned int nb_pre_create_files;              /**< The size of pre-created files per synclet        */
  char pre_create_obj_dir[MAXPATHLEN];                 /**< Directory for pre-createed objects         */
  char tmp_symlink_dir[MAXPATHLEN];                    /**< Directory for symlinks's birth             */
  LRU_parameter_t lru_param;                           /**< Parameter to LRU for async op              */
} mfsl_parameter_t;

typedef struct mfsl_context__
{
  struct prealloc_pool pool_spec_data;
  struct prealloc_pool pool_async_op;
  pthread_mutex_t lock;
  unsigned int synclet_index;
  struct prealloc_pool pool_dirs;
  struct prealloc_pool pool_files;
} mfsl_context_t;

int mfsl_async_hash_init(void);
int mfsl_async_set_specdata(mfsl_object_t * key, mfsl_object_specific_data_t * value);
int mfsl_async_get_specdata(mfsl_object_t * key, mfsl_object_specific_data_t ** value);
int mfsl_async_remove_specdata(mfsl_object_t * key);

void *mfsl_async_synclet_thread(void *Arg);
void *mfsl_async_asynchronous_dispatcher_thread(void *Arg);
fsal_status_t mfsl_async_post_async_op(mfsl_async_op_desc_t * popdes,
                                       mfsl_object_t * pmobject);
fsal_status_t MFSL_async_post(mfsl_async_op_desc_t * popdesc);

fsal_status_t mfsl_async_init_precreated_directories(fsal_op_context_t    *pcontext,
                                                     struct prealloc_pool *pool_dirs);

fsal_status_t mfsl_async_init_precreated_files(fsal_op_context_t    *pcontext,
                                               struct prealloc_pool *pool_dirs);

fsal_status_t mfsl_async_init_clean_precreated_objects(fsal_op_context_t * pcontext);

int mfsl_async_is_object_asynchronous(mfsl_object_t * object);

fsal_status_t mfsl_async_init_symlinkdir(fsal_op_context_t * pcontext);

void constructor_preacreated_entries(void *ptr);

fsal_status_t MFSL_PrepareContext(fsal_op_context_t * pcontext);

fsal_status_t MFSL_RefreshContext(mfsl_context_t * pcontext,
                                  fsal_op_context_t * pfsal_context);

fsal_status_t MFSL_ASYNC_GetSyncletContext(mfsl_synclet_context_t * pcontext,
                                           fsal_op_context_t * pfsal_context);

fsal_status_t MFSL_ASYNC_RefreshSyncletContext(mfsl_synclet_context_t * pcontext,
                                               fsal_op_context_t * pfsal_context);

int MFSL_ASYNC_is_synced(mfsl_object_t * mobject);

#endif                          /* _MFSL_ASYNC_TYPES_H */
