/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    cache_inode_create.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.29 $
 * \brief   Creation of a file through the cache layer.
 *
 * cache_inode_mkdir.c : Creation of an entry through the cache layer
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

extern fsal_handle_t pre_created_dir_handle;        /**< The handle for the precreated object directory */

/**
 *
 * cache_inode_async_create: creates an object, a file or a directory for the moment (asynchronously)
 *
 * Creates an object. In fact, this will move an entry already created to the right place and then chown it
 *
 * @param popasyncdesc [IN] Cache Inode Asynchonous Operation descriptor
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
fsal_status_t cache_inode_async_create(cache_inode_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;
  fsal_name_t fileidname;
  char fileidstr[MAXNAMLEN];
  fsal_attrib_list_t attr_src;
  fsal_attrib_list_t attr_dest;

  attr_src.asked_attributes = FSAL_ATTRS_POSIX;
  attr_dest.asked_attributes = FSAL_ATTRS_POSIX;

  /* Step 1: Put the pre-created object (already inserted in the datacache) at the right place */

  /* Build the originated name */
  switch (popasyncdesc->op_args.create.object_type)
    {
    case FSAL_TYPE_DIR:
      snprintf(fileidstr, MAXNAMLEN, "dir.export=%llu.fileid=%llu",
               FSAL_EXPORT_CONTEXT_SPECIFIC(popasyncdesc->fsal_op_context.export_context),
               popasyncdesc->op_args.create.fileid);
      break;

    case FSAL_TYPE_FILE:
      snprintf(fileidstr, MAXNAMLEN, "file.export=%llu.fileid=%llu",
               FSAL_EXPORT_CONTEXT_SPECIFIC(popasyncdesc->fsal_op_context.export_context),
               popasyncdesc->op_args.create.fileid);
      break;

    default:
      fsal_status.major = ERR_FSAL_INVAL;
      fsal_status.minor = EINVAL;
      return fsal_status;
      break;                    /* useless but wanted by some compilers */
    }                           /* switch( popasyncdesc->op_args.create.object_type ) */

  fsal_status = FSAL_str2name(fileidstr, MAXNAMLEN, &fileidname);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Rename the object to place it at the correct place */
  fsal_status = FSAL_rename(popasyncdesc->op_args.create.pfsal_handle_dir_pre,
                            &fileidname,
                            popasyncdesc->op_args.create.pfsal_handle_dir,
                            &popasyncdesc->op_args.create.name,
                            &popasyncdesc->fsal_op_context, &attr_src, &attr_dest);
  if(FSAL_IS_ERROR(fsal_status))
    return fsal_status;

  /* Step 2: Put the right attributes (owner, group, mode) */

  return fsal_status;
}                               /* cache_inode_aync_create */

/**
 *
 * cache_inode_create: creates an entry through the cache.
 *
 * Creates an entry through the cache.
 *
 * @param pentry_parent [IN] pointer to the pentry parent
 * @param pname         [IN] pointer to the name of the object in the destination directory.
 * @param type          [IN] type of the object to be created.
 * #param pcreate_arg   [IN] additional argument for object creation
 * @param pattr         [OUT] attributes for the new object.
 * @param ht            [INOUT] hash table used for the cache.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext      [IN] FSAL credentials
 * @param pstatus       [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry\n
 * @return CACHE_INODE_BAD_TYPE either source or destination have incorrect type\n
 * @return CACHE_INODE_ENTRY_EXISTS entry of that name already exists in destination.
 *
 */

cache_entry_t *cache_inode_create(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  cache_inode_file_type_t type,
                                  fsal_accessmode_t mode,
                                  cache_inode_create_arg_t * pcreate_arg,
                                  fsal_attrib_list_t * pattr,
                                  hash_table_t * ht,
                                  cache_inode_client_t * pclient,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
  fsal_handle_t object_handle;
  fsal_attrib_list_t parent_attributes;
  fsal_attrib_list_t object_attributes;
  fsal_handle_t dir_handle;
  fsal_handle_t *pdir_handle = NULL;
  cache_inode_fsal_data_t fsal_data;
  cache_inode_status_t status;
  cache_inode_async_op_desc_t *pasyncopdesc = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_CREATE] += 1;

  /* Check if the required type is correct, with this function, we manage file, dir and symlink */
  if(type != REGULAR_FILE && type != DIR_BEGINNING && type != SYMBOLIC_LINK
     && type != SOCKET_FILE && type != FIFO_FILE && type != CHARACTER_FILE
     && type != BLOCK_FILE)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }

  /* Check if an entry of the same name exists */
  if((pentry = cache_inode_lookup(pentry_parent,
                                  pname,
                                  &parent_attributes,
                                  ht, pclient, pcontext, pstatus)) != NULL)
    {
      *pstatus = CACHE_INODE_ENTRY_EXISTS;

      if(pentry->internal_md.type != type)
        {
          /* Incompatible types, returns NULL */

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
      else
        {
          /* stats */
          pclient->stat.func_stats.nb_success[CACHE_INODE_CREATE] += 1;

          /* redondant creation, returned the previously created entry */
          return pentry;
        }
    }

  /* At this point, the entry was not found, this means that is doesn't exist is FSAL, we can create it */

  switch (type)
    {
    case REGULAR_FILE:
    case DIR_BEGINNING:
    case DIR_CONTINUE:
      break;

    case SYMBOLIC_LINK:
    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
      *pstatus = CACHE_INODE_NOT_SUPPORTED;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
      break;

    default:
      /* we should never go there */
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
      break;
    }

  /* Get the lock for the parent */
  P(pentry_parent->lock);

  if(pentry_parent->internal_md.type == DIR_BEGINNING)
    {
      dir_handle = pentry_parent->object.dir_begin.handle;
      pdir_handle = &pentry_parent->object.dir_begin.handle;
    }

  if(pentry_parent->internal_md.type == DIR_CONTINUE)
    {
      P(pentry_parent->object.dir_cont.pdir_begin->lock);
      dir_handle = pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.handle;
      pdir_handle = &pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V(pentry_parent->object.dir_cont.pdir_begin->lock);
    }

  /* Post an asynchronous operation */
  P(pclient->pool_lock);
  GET_PREALLOC(pasyncopdesc,
               pclient->pool_async_op,
               pclient->nb_pre_async_op_desc, cache_inode_async_op_desc_t, next_alloc);
  V(pclient->pool_lock);

  if(pasyncopdesc == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      *pstatus = CACHE_INODE_MALLOC_ERROR;

      V(pentry_parent->lock);

      return NULL;
    }

  pasyncopdesc->op_type = CACHE_INODE_ASYNC_OP_CREATE;
  pasyncopdesc->op_args.create.pfsal_handle_dir_pre = &pre_created_dir_handle;
  pasyncopdesc->op_args.create.pfsal_handle_obj_pre =
      cache_inode_async_get_preallocated(pclient, type,
                                         &pasyncopdesc->op_args.create.fileid,
                                         pcontext->export_context, pstatus);
  if(pasyncopdesc->op_args.create.pfsal_handle_obj_pre == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      *pstatus = CACHE_INODE_MALLOC_ERROR;

      V(pentry_parent->lock);

      return NULL;
    }
  else
    object_handle = *(pasyncopdesc->op_args.create.pfsal_handle_obj_pre);

  pasyncopdesc->op_args.create.pfsal_handle_dir = pdir_handle;

  if(type == REGULAR_FILE)
    pasyncopdesc->op_args.create.object_type = FSAL_TYPE_FILE;
  else
    pasyncopdesc->op_args.create.object_type = FSAL_TYPE_DIR;

  pasyncopdesc->op_args.create.name = *pname;
  pasyncopdesc->op_args.create.mode = mode;
  pasyncopdesc->op_res.create.attr.asked_attributes = FSAL_ATTRS_POSIX;
  pasyncopdesc->op_func = cache_inode_async_create;

  pasyncopdesc->fsal_op_context = *pcontext;
  pasyncopdesc->fsal_export_context = *(pcontext->export_context);
  pasyncopdesc->fsal_op_context.export_context = &pasyncopdesc->fsal_export_context;

  pasyncopdesc->ht = ht;
  pasyncopdesc->origine_pool = pclient->pool_async_op;
  pasyncopdesc->ppool_lock = &pclient->pool_lock;

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      DisplayLog("cache_inode_create: cannot get time of day... exiting");
      exit(1);
    }

  /* Affect the operation to a synclet */
  if(cache_inode_post_async_op(pasyncopdesc,
                               pentry_parent, pstatus) != CACHE_INODE_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      DisplayLog("WARNING !!! cache_inode_create could not post async op....");

      *pstatus = CACHE_INODE_ASYNC_POST_ERROR;

      V(pentry_parent->lock);

      return NULL;
    }

  object_attributes.asked_attributes = FSAL_ATTRS_POSIX;

  object_attributes.supported_attributes = FSAL_ATTRS_POSIX;
  if(type == REGULAR_FILE)
    {
      object_attributes.type = FSAL_TYPE_FILE;
      object_attributes.numlinks = 1;
    }
  else
    {
      /* This is a directory */
      object_attributes.type = FSAL_TYPE_DIR;
      object_attributes.numlinks = 2;   /* . and .. */
    }
  object_attributes.filesize = 0;       /* A new object, either a file or a directory, is empty */
  object_attributes.spaceused = 0;      /* A new object, either a file or a directory, is empty */
  object_attributes.fsid = parent_attributes.fsid;
  object_attributes.fileid = pasyncopdesc->op_args.create.fileid;
  object_attributes.mode = mode;
  object_attributes.owner = 0;  /* For wanting of a better solution */
  object_attributes.group = 0;  /* For wanting of a better solution */
  object_attributes.atime.seconds = pasyncopdesc->op_time.tv_sec;
  object_attributes.atime.seconds = pasyncopdesc->op_time.tv_sec;
  object_attributes.ctime.seconds = pasyncopdesc->op_time.tv_sec;
  object_attributes.ctime.nseconds = pasyncopdesc->op_time.tv_usec;
  object_attributes.mtime.nseconds = pasyncopdesc->op_time.tv_usec;
  object_attributes.mtime.nseconds = pasyncopdesc->op_time.tv_usec;

  fsal_data.handle = object_handle;
  fsal_data.cookie = DIR_START;

  /* This call will return NULL if failed */
  if((pentry = cache_inode_new_entry(&fsal_data, &object_attributes, type, pcreate_arg, NULL, ht, pclient, pcontext, TRUE,      /* This is a creation and not a population */
                                     pstatus)) == NULL)
    {
      *pstatus = CACHE_INODE_INSERT_ERROR;
      V(pentry_parent->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }

  /* Add this entry to the directory */
  if(cache_inode_add_cached_dirent(pentry_parent,
                                   pname,
                                   pentry,
                                   NULL,
                                   ht, pclient, pcontext, pstatus) != CACHE_INODE_SUCCESS)
    {
      V(pentry_parent->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }

  /* Update the parent cached attributes */
  if(pentry_parent->internal_md.type == DIR_BEGINNING)
    {
      pentry_parent->object.dir_begin.attributes.mtime.seconds =
          pasyncopdesc->op_time.tv_sec;
      pentry_parent->object.dir_begin.attributes.mtime.nseconds =
          pasyncopdesc->op_time.tv_usec;
      pentry_parent->object.dir_begin.attributes.ctime =
          pentry_parent->object.dir_begin.attributes.mtime;

      /* if the created object is a directory, it contains a link
       * to its parent : '..'. Thus the numlink attr must be increased.
       */
      if(type == DIR_BEGINNING)
        {
          pentry_parent->object.dir_begin.attributes.numlinks++;
        }

    }
  else
    {
      /* DIR_CONTINUE */
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.
          seconds = time(NULL);
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime.
          seconds = 0;
      pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.ctime =
          pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.mtime;

      /* if the created object is a directory, it contains a link
       * to its parent : '..'. Thus the numlink attr must be increased.
       */
      if(type == DIR_BEGINNING)
        {
          pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.attributes.
              numlinks++;
        }

    }

  /* Get the attributes in return */
  *pattr = object_attributes;

  /* valid the parent */
  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_SET, pclient);

  /* release the lock for the parent */
  V(pentry_parent->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_CREATE] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_CREATE] += 1;

  return pentry;
}                               /* cache_inode_create */
