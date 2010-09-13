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
#include "err_LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode_async.h"
#include "cache_inode.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

cache_entry_t *async_pentries_list;
pthread_mutex_t mutex_async_pentries_list;

pthread_t cache_inode_dta_thrid;
pthread_t *cache_inode_synclet_thrid;

fsal_handle_t pre_created_dir_handle;
int once = 0;

cache_inode_synclet_data_t *synclet_data;

cache_inode_client_parameter_t client_parameter;

char *asyncop_name[] = { "CACHE_INODE_ASYNC_OP_CREATE",
  "CACHE_INODE_ASYNC_OP_LINK",
  "CACHE_INODE_ASYNC_OP_REMOVE",
  "CACHE_INODE_ASYNC_OP_RENAME_SRC",
  "CACHE_INODE_ASYNC_OP_RENAME_DST",
  "CACHE_INODE_ASYNC_OP_SETATTR",
  "CACHE_INODE_ASYNC_OP_TRUNCATE"
};

/**
 *
 * cache_inode_async_choose_synclet: Choose the synclet that will receive an entry to manage.
 *
 * @param (none)
 * 
 * @return the index for the synclet to be used.
 *
 */
static unsigned int cache_inode_async_choose_synclet(void)
{
#define NO_VALUE_CHOOSEN  1000000
  unsigned int synclet_chosen = NO_VALUE_CHOOSEN;
  unsigned int min_number_pending = NO_VALUE_CHOOSEN;

  unsigned int i;
  static unsigned int last;
  unsigned int cpt = 0;

  do
    {

      /* chose the smallest queue */

      for(i = (last + 1) % client_parameter.nb_synclet, cpt = 0;
          cpt < client_parameter.nb_synclet;
          cpt++, i = (i + 1) % client_parameter.nb_synclet)
        {
          /* Choose only fully initialized workers and that does not gc */

          if(synclet_data[i].op_lru->nb_entry < min_number_pending)
            {
              synclet_chosen = i;
              min_number_pending = synclet_data[i].op_lru->nb_entry;
            }
        }

    }
  while(synclet_chosen == NO_VALUE_CHOOSEN);

  last = synclet_chosen;

  return synclet_chosen;
}                               /* cache_inode_async_choose_synclet */

/**
 *
 * cache_inode_async_init: starts the asynchronous synclets and dispatcher.
 * 
 * Starts the asynchronous synclets and dispatcher.
 * 
 * @param param [IN] the parameter for this cache. 
 *
 * @return nothing (void function)
 *
 */
void cache_inode_async_init(cache_inode_client_parameter_t param)
{
  pthread_attr_t attr_thr;
  int rc = 0;
  unsigned int i = 0;
  pthread_mutexattr_t mutexattr;
  pthread_condattr_t condattr;
  LRU_status_t lru_status;
  fsal_status_t fsal_status;

  /* Keep the parameters value */
  client_parameter = param;

  /* Starting asynchronous service threads */
  LogEvent(COMPONENT_CACHE_INODE,"Starting writeback threads");

  /* Init the LRU and its associated mutex */
  if((rc = pthread_mutexattr_init(&mutexattr)) != 0)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_MUTEX_INIT, rc);
      exit(1);
    }

  if((rc = pthread_condattr_init(&condattr)) != 0)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_COND_INIT, rc);
      exit(1);
    }

  if((rc = pthread_mutex_init(&mutex_async_pentries_list, &mutexattr)) != 0)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_MUTEX_INIT, rc);
      exit(1);
    }

  /* Prepare the threads to be launched */
  pthread_attr_init(&attr_thr);
#ifndef _IRIX_6
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);
#endif

  /* Starting the threads */
  if((rc = pthread_create(&cache_inode_dta_thrid,
                          &attr_thr,
                          cache_inode_asynchronous_dispatcher_thread, (void *)NULL)) != 0)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_CREATE, rc);
      exit(1);
    }

  if((cache_inode_synclet_thrid =
      (pthread_t *) Mem_Alloc(param.nb_synclet * sizeof(pthread_t))) == NULL)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
      exit(1);
    }

  if((synclet_data =
      (cache_inode_synclet_data_t *) Mem_Alloc(param.nb_synclet *
                                               sizeof(cache_inode_synclet_data_t))) ==
     NULL)
    {
      LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
      exit(1);
    }

  LogEvent(COMPONENT_CACHE_INODE,"MD WRITEBACK STARTUP: writeback dispatcher started successfully");

  /* Prepare and launch the synclets */
  for(i = 0; i < param.nb_synclet; i++)
    {
      /* Init the synclet data */
      if((rc = pthread_mutex_init(&(synclet_data[i].mutex_op_condvar), &mutexattr)) != 0)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_MUTEX_INIT, rc);
          exit(1);
        }

      if((rc = pthread_cond_init(&(synclet_data[i].op_condvar), &condattr)) != 0)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_MUTEX_INIT, rc);
          exit(1);
        }

      if((synclet_data[i].op_lru = LRU_Init(param.lru_async_param, &lru_status)) == NULL)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_LRU, ERR_LRU_LIST_INIT, lru_status);
          exit(1);
        }

      /* Now start the synclet */
      if((rc = pthread_create(&(cache_inode_synclet_thrid[i]),
                              &attr_thr, cache_inode_synclet_thread, (void *)i)) != 0)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_PTHREAD_CREATE, rc);
          exit(1);
        }
      else
        LogEvent(COMPONENT_CACHE_INODE,"MD WRITEBACK STARTUP: writeback synclet #%u started", i);

      /* Init the root fsal_op_context that is used for internal ops in the asyncops */
      fsal_status = FSAL_InitClientContext(&synclet_data[i].root_fsal_context);
      if(FSAL_IS_ERROR(fsal_status))
        {
          LogMajor(COMPONENT_CACHE_INODE,
              "MD WRITEBACK STARTUP: Can't init FSAL context for synclet %u fsal_status=(%u,%u)... exiting",
               i, fsal_status.major, fsal_status.minor);
          exit(1);
        }
      /* Set my own index */
      synclet_data[i].my_index = i;
    }
  LogEvent(COMPONENT_CACHE_INODE,"MD WRITEBACK STARTUP: %u synclet threads were started successfully",
             param.nb_synclet);

  /* End of function */
  return;
}                               /* cache_inode_async_init */

/**
 *
 * cache_inode_async_alloc_precreated: allocates pre-created objects within a cache_init_client_t.
 * 
 * Allocates pre-created objects within a cache_init_client_t.
 * 
 * @param pclient [IN] pointer to the client to be managed.
 *
 * @return nothing (void function)
 *
 */
static void cache_inode_async_alloc_precreated(cache_inode_client_t * pclient,
                                               cache_inode_file_type_t type)
{

  switch (type)
    {
    case DIR_BEGINNING:
    case DIR_CONTINUE:
      if((pclient->dir_pool_handle =
          (fsal_handle_t *) Mem_Alloc(sizeof(fsal_handle_t) *
                                      pclient->nb_pre_create_dirs)) == NULL)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
          exit(1);
        }

      if((pclient->dir_pool_fileid =
          (fsal_u64_t *) Mem_Alloc(sizeof(fsal_u64_t) * pclient->nb_pre_create_dirs)) ==
         NULL)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
          exit(1);
        }

      break;

    case REGULAR_FILE:
      if((pclient->file_pool_handle =
          (fsal_handle_t *) Mem_Alloc(sizeof(fsal_handle_t) *
                                      pclient->nb_pre_create_files)) == NULL)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
          exit(1);
        }

      if((pclient->file_pool_fileid =
          (fsal_u64_t *) Mem_Alloc(sizeof(fsal_u64_t) * pclient->nb_pre_create_files)) ==
         NULL)
        {
          LogError(COMPONENT_CACHE_INODE, ERR_SYS, ERR_MALLOC, errno);
          exit(1);
        }

      break;
    }                           /* switch( type ) */

  /* End of function */
  return;
}                               /* cache_inode_async_alloc_precreated */

/**
 * 
 * cache_inode_async_get_preallocated: gets a preallocated object from a cache_inode_client pool.
 *
 * Gets a preallocated object from a cache_inode_client pool.
 *
 * @param pclient [INOUT] pointer to the client that will be used.
 * @param type    [IN]    type of the object, must be DIR_BEGINNING, DIR_CONTINUE or REGULAR_FILE
 * @param pfileid [OUT]   at exit, will contain the file id for this object 
 * @param pstatus [OUT]   the status for the operation.
 *
 * @return a pointer to the fsal handle fo rthe desired object. NULL value indicates an error (see *pstatus) 
 *
 */
fsal_handle_t *cache_inode_async_get_preallocated(cache_inode_client_t * pclient,
                                                  cache_inode_file_type_t type,
                                                  fsal_u64_t * pfileid,
                                                  fsal_export_context_t * pexport_context,
                                                  cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  unsigned int index = 0;

  if(pclient == NULL || pfileid == NULL || pstatus == NULL)
    return NULL;

  switch (type)
    {
    case DIR_BEGINNING:
    case DIR_CONTINUE:
      if(pclient->avail_precreated_dirs > 0)
        {
          index = pclient->nb_pre_create_dirs - pclient->avail_precreated_dirs;
          pfsal_handle = &pclient->dir_pool_handle[index];
          *pfileid = pclient->dir_pool_fileid[index];

          pclient->avail_precreated_dirs -= 1;
        }
      else
        {
          /* I create a new pool and recall the function is successful */
          if(cache_inode_async_precreate_object(pclient, type, pexport_context) > 0)
            return cache_inode_async_get_preallocated(pclient, type, pfileid,
                                                      pexport_context, pstatus);
        }
      break;

    case REGULAR_FILE:
      if(pclient->avail_precreated_files > 0)
        {
          index = pclient->nb_pre_create_files - pclient->avail_precreated_files;
          pfsal_handle = &pclient->file_pool_handle[index];
          *pfileid = pclient->file_pool_fileid[index];

          pclient->avail_precreated_files -= 1;
        }
      else
        {
          /* I create a new pool and recall the function is successful */
          if(cache_inode_async_precreate_object(pclient, type, pexport_context) > 0)
            return cache_inode_async_get_preallocated(pclient, type, pfileid,
                                                      pexport_context, pstatus);
        }
      break;
    }                           /* switch( type ) */

  return pfsal_handle;
}                               /* cache_inode_async_get_preallocated */

/**
 *
 * cache_inode_async_op_reduce: reduces (by factorization) the number of entries within a list of asyncops
 *
 * Reduces (by factorization) the number of entries within a list of asyncops.
 *
 * @param pasyncoplist     [INOUT]    the beginning of the asyncop list (may be changed by the function)
 * @param ppasynclistres   [OUT]      the resulting list of operation
 * @param pstatus          [OUT]      pointer to the returned status 
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_INVALID_ARGUMENT if a bad argument is provided (NULL pointer...) \n
 *
 */
cache_inode_status_t cache_inode_async_op_reduce(cache_inode_async_op_desc_t *
                                                 pasyncoplist,
                                                 cache_inode_async_op_desc_t **
                                                 ppasyncoplistres,
                                                 cache_inode_status_t * pstatus)
{
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pasyncoplist == NULL || ppasyncoplistres == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Nothing done for the moment */
  *ppasyncoplistres = pasyncoplist;

  /* Successful exit... */
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_async_op_reduce */

/**
 *
 * cache_inode_process_async_op: processes an aynchronous operation to the cache entry's pending operations list.
 *
 * Processes an aynchronous operation to the cache entry's pending operations list.
 * The pentry is supposed to be locked when this function is called. 
 *
 * @param popdesc [IN]    the asynchronous operation descriptor
 * @param pentry  [INOUT] the cache entry to be manipulated by calling this function
 * @param pstatus [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_INVALID_ARGUMENT if a bad argument is provided (NULL pointer...) \n
 *
 */
cache_inode_status_t cache_inode_process_async_op(cache_inode_async_op_desc_t *
                                                  pasyncopdesc, cache_entry_t * pentry,
                                                  cache_inode_status_t * pstatus)
{
  cache_inode_status_t cache_status;
  fsal_status_t fsal_status;

  /* Sanity check */
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(pasyncopdesc == NULL || pentry == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Calling the function from async op */
  LogDebug(COMPONENT_CACHE_INODE, "op_type = %u %s", pasyncopdesc->op_type,
                  asyncop_name[pasyncopdesc->op_type]);

  fsal_status = (pasyncopdesc->op_func) (pasyncopdesc);

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
    }

  /* Regular exit */
  *pstatus = CACHE_INODE_SUCCESS;

  return *pstatus;
}                               /* cache_inode_process_async_op */

/**
 *
 * cache_inode_post_async_op: posts an aynchronous operation to the cache entry's pending operations list.
 *
 * Posts an aynchronous operation to the cache entry's pending operations list.
 * The pentry is supposed to be locked when this function is called. 
 *
 * @param popdesc [IN]    the asynchronous operation descriptor
 * @param pentry  [INOUT] the cache entry to be manipulated by calling this function
 * @param pstatus [OUT]   returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_INVALID_ARGUMENT if a bad argument is provided (NULL pointer...) \n
 *
 */
cache_inode_status_t cache_inode_post_async_op(cache_inode_async_op_desc_t * popdesc,
                                               cache_entry_t * pentry,
                                               cache_inode_status_t * pstatus)
{
  cache_inode_async_op_desc_t *piter_opdesc = NULL;

  /* Sanity check */
  if(pstatus == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  if(popdesc == NULL || pentry == NULL)
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
      return *pstatus;
    }

  /* Add the pentry to async_pentries_list if  pentry->pending_ops == NULL. The fact to
   * have this pointer NULL means that the pentry have no pending op yet and then needs to be put to the list */
  P(mutex_async_pentries_list);

  if(pentry->pending_ops == NULL)
    {
      /* Entry has to be added ro async_pentries_list */
      if(async_pentries_list == NULL)
        async_pentries_list = pentry;
      else
        async_pentries_list->next_asyncop = pentry;

    }
  V(mutex_async_pentries_list);

  popdesc->next = NULL;
  if(pentry->pending_ops == NULL)
    {
      /* Add op to pending_ops */
      pentry->pending_ops = popdesc;
    }
  else
    {
      /* Go to the end of the list of pending ops... */
      for(piter_opdesc = pentry->pending_ops; piter_opdesc != NULL;
          piter_opdesc = piter_opdesc->next)
        if(piter_opdesc->next == NULL)
          {
            piter_opdesc->next = popdesc;
            break;
          }
    }

  pentry->next_asyncop = NULL;

  /* Regular exit */
  *pstatus = CACHE_INODE_SUCCESS;

  return *pstatus;
}                               /* cache_inode_post_async_op */

/**
 *
 * cache_inode_renew_entry: Renews the attributes for an entry.
 * 
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided 
 * with compliance to the underlying FSAL semantics. Attributes that are set are returned in "*pattr".
 *
 * @param pentry_parent [IN] entry for the parent directory to be managed.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
   @return Other errors shows a FSAL error.
 *
 */
cache_inode_status_t cache_inode_resync_entry(cache_entry_t * pentry,
                                              hash_table_t * ht,
                                              fsal_op_context_t * pcontext,
                                              cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  fsal_attrib_list_t object_attributes;
  fsal_path_t link_content;
  time_t current_time = time(NULL);
  time_t entry_time = pentry->internal_md.refresh_time;

  /* If we do nothing (no expiration) then everything is all right */
  *pstatus = CACHE_INODE_SUCCESS;

  LogFullDebug(COMPONENT_CACHE_INODE,
                  "Entry=%p, type=%d, current=%d, read=%d, refresh=%d, alloc=%d",
                  pentry, pentry->internal_md.type, current_time,
                  pentry->internal_md.read_time, pentry->internal_md.refresh_time,
                  pentry->internal_md.alloc_time);

  /* An entry that is a regular file with an associated File Content Entry won't
   * expire until data exists in File Content Cache, to avoid attributes incoherency */

  /* @todo: BUGAZOMEU: I got serious doubts on the following blocks: possible trouble if using data caching */
  if(pentry->internal_md.type == REGULAR_FILE &&
     pentry->object.file.pentry_content != NULL)
    {
      /* Successfull exit without having nothing to do ... */
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }

  /* Do we use getattr/mtime checking */
  if(pentry->internal_md.type == DIR_BEGINNING &&
     pentry->object.dir_begin.has_been_readdir == CACHE_INODE_YES)
    {
      /* This checking is to be done ... */
      pfsal_handle = &pentry->object.dir_begin.handle;

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = FSAL_ATTRS_POSIX;

      fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                   pentry, __LINE__);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }
          return *pstatus;
        }

      /* Compare the FSAL mtime and the cached mtime */
      if(pentry->object.dir_begin.attributes.mtime.seconds <
         object_attributes.mtime.seconds)
        {
          /* Cached directory content is obsolete, it must be renewed */
          pentry->object.dir_begin.attributes = object_attributes;

          /* Set the directory content as "to be renewed" */
          /* Next call to cache_inode_readdir will repopulate the dirent array */
          pentry->object.dir_begin.has_been_readdir = CACHE_INODE_RENEW_NEEDED;

          /* Set the refresh time for the cache entry */
          pentry->internal_md.refresh_time = time(NULL);

        }                       /* if( pentry->object.dir_begin.attributes.mtime < object_attributes.asked_attributes.mtime ) */
    }

  /* if( pentry->internal_md.type == DIR_BEGINNING &&... */
  /* Check for dir content expiration */
  if(pentry->internal_md.type == DIR_BEGINNING &&
     pentry->object.dir_begin.has_been_readdir == CACHE_INODE_YES)
    {
      /* Do the getattr if it had not being done before */
      if(pfsal_handle == NULL)
        {
          pfsal_handle = &pentry->object.dir_begin.handle;

          /* Call FSAL to get the attributes */
          object_attributes.asked_attributes = FSAL_ATTRS_POSIX;

          fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);

          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);

              if(fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  LogEvent(COMPONENT_CACHE_INODE,
                      "cache_inode_resync_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                       pentry, __LINE__);

                  *pstatus = CACHE_INODE_FSAL_ESTALE;
                }

              return *pstatus;
            }
        }

      pentry->object.dir_begin.attributes = object_attributes;

      /* Set the directory content as "to be renewed" */
      /* Next call to cache_inode_readdir will repopulate the dirent array */
      pentry->object.dir_begin.has_been_readdir = CACHE_INODE_RENEW_NEEDED;

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if( pentry->internal_md.type == DIR_BEGINNING && ... */
  /* if the directory has not been readdir, only update its attributes */
  else if(pentry->internal_md.type == DIR_BEGINNING &&
          pentry->object.dir_begin.has_been_readdir != CACHE_INODE_YES)
    {
      pfsal_handle = &pentry->object.dir_begin.handle;

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = FSAL_ATTRS_POSIX;

      fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                   pentry, __LINE__);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          return *pstatus;
        }

      pentry->object.dir_begin.attributes = object_attributes;

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* else if( pentry->internal_md.type == DIR_BEGINNING && ... */
  /* Check for attributes expiration in other cases */
  else if(pentry->internal_md.type != DIR_CONTINUE &&
          pentry->internal_md.type != DIR_BEGINNING)
    {
      switch (pentry->internal_md.type)
        {
        case REGULAR_FILE:
          pfsal_handle = &pentry->object.file.handle;
          break;

        case SYMBOLIC_LINK:
          pfsal_handle = &pentry->object.symlink.handle;
          break;

        case SOCKET_FILE:
        case FIFO_FILE:
        case CHARACTER_FILE:
        case BLOCK_FILE:
          pfsal_handle = &pentry->object.special_obj.handle;
          break;
        }

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = FSAL_ATTRS_POSIX;
      fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_resync_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                   pentry, __LINE__);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          return *pstatus;
        }

      /* Keep the new attribute in cache */
      switch (pentry->internal_md.type)
        {
        case REGULAR_FILE:
          pentry->object.file.attributes = object_attributes;
          break;

        case SYMBOLIC_LINK:
          pentry->object.symlink.attributes = object_attributes;
          break;

        case SOCKET_FILE:
        case FIFO_FILE:
        case CHARACTER_FILE:
        case BLOCK_FILE:
          pentry->object.special_obj.attributes = object_attributes;
          break;
        }

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if(  pentry->internal_md.type   != DIR_CONTINUE && ... */
  /* Check for link content expiration */
  if(pentry->internal_md.type == SYMBOLIC_LINK)
    {
      pfsal_handle = &pentry->object.symlink.handle;

      FSAL_CLEAR_MASK(object_attributes.asked_attributes);
      FSAL_SET_MASK(object_attributes.asked_attributes, FSAL_ATTRS_POSIX);

      fsal_status =
          FSAL_readlink(pfsal_handle, pcontext, &link_content, &object_attributes);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          /* stats */

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_resync_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                   pentry, __LINE__);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

        }
      else
        {
          fsal_status = FSAL_pathcpy(&pentry->object.symlink.content, &link_content);
          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);
              /* stats */
            }
        }

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if( pentry->internal_md.type == SYMBOLIC_LINK && ... */
  return *pstatus;
}                               /* cache_inode_resync_entry */

/**
 * cache_inode_async_precreated_name: builds the name for a pre-created entry.
 * 
 * Builds the name for a pre-created entry.
 *
 * @param pname           [OUT]   pointer to the name to be nuilt
 * @param objindex        [IN]    an index associated with the object 
 * @param psyncletdata    [IN]    pointer to the data associated with the synclet 
 * @param object_type     [IN]    the type of object to be created
 * @param pexport_context [IN]    the export context to be used
 * 
 * @return the number of created objects, -1 if failed.
 *
 */
int cache_inode_async_precreated_name(fsal_name_t * pname,
                                      cache_inode_client_t * pclient,
                                      cache_inode_file_type_t object_type,
                                      fsal_export_context_t * pexport_context)
{
  char objname[MAXNAMLEN];
  pid_t pid = getpid();
  fsal_status_t fsal_status;

  switch (object_type)
    {
    case DIR_BEGINNING:
    case DIR_CONTINUE:
      snprintf(objname, MAXNAMLEN, "pre.create_dir.pid=%u.client=%u.exportid=%llu", pid, (caddr_t) pclient, FSAL_EXPORT_CONTEXT_SPECIFIC(pexport_context));     /* Don't forget this is a uint64_t */
      break;

    case REGULAR_FILE:
      snprintf(objname, MAXNAMLEN, "pre.create_file.pid=%u.client=%u.exportid=%llu", pid, (caddr_t) pclient, FSAL_EXPORT_CONTEXT_SPECIFIC(pexport_context));    /* Don't forget this is a uint64_t */
      break;

    default:
      return -1;
      break;                    /* Useless but required by some compilers */
    }

  fsal_status = FSAL_str2name(objname, MAXNAMLEN, pname);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogCrit(COMPONENT_CACHE_INODE,"cache_inode_async_precreate_object failed: error in FSAL_lookupPath");
      LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
      return -1;
    }

  return 0;
}                               /* cache_inode_async_precreated_name */

/**
 * cache_inode_async_precreate_object: pre-creates the directories for a synclet 
 *
 * Pre-creates the directories for a synclet 
 *
 * @param psyncletdata    [INOUT] pointer to the data associated with the synclet 
 * @param object_type     [IN]    the type of object to be created
 * @param pexport_context [IN]    the export context to be used
 * 
 * @return the number of created objects, -1 if failed.
 *
 */
int cache_inode_async_precreate_object(cache_inode_client_t * pclient,
                                       cache_inode_file_type_t object_type,
                                       fsal_export_context_t * pexport_context)
{
  unsigned int i = 0;
  pid_t pid = getpid();
  char objname[MAXNAMLEN];
  char destname[MAXNAMLEN];
  fsal_name_t name;
  fsal_name_t fileidname;
  fsal_path_t path;
  fsal_status_t fsal_status;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_src;
  fsal_attrib_list_t attr_dest;
  unsigned int cpt = 0;
  fsal_op_context_t root_fsal_context;

  /* Preallocated some stuffs.. Will exit if failed */
  cache_inode_async_alloc_precreated(pclient, object_type);

  /* Get a fsal context as root with no altgroupes */
  fsal_status =
      FSAL_GetClientContext(&root_fsal_context, pexport_context, 0, -1, NULL, 0);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogCrit(COMPONENT_CACHE_INODE,
          "cache_inode_async_precreate_object failed: error in FSAL_GetClientContext");
      LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
      return -1;
    }

  /* Get the precreated directory pool handle */
  fsal_status = FSAL_str2path(client_parameter.pre_create_obj_dir, MAXPATHLEN, &path);
  if(FSAL_IS_ERROR(fsal_status))
    {
      LogCrit(COMPONENT_CACHE_INODE,"cache_inode_async_precreate_object failed: error in FSAL_str2path");
      LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
      return -1;
    }

  if(once == 0)
    {
      attr.asked_attributes = FSAL_ATTRS_POSIX;
      fsal_status = FSAL_lookupPath(&path,
                                    &root_fsal_context, &pre_created_dir_handle, &attr);
      if(FSAL_IS_ERROR(fsal_status))
        {
          LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_async_precreate_object failed: error in FSAL_lookupPath");
          LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
          return -1;
        }
      once = 1;
    }

  switch (object_type)
    {
    case DIR_BEGINNING:
    case DIR_CONTINUE:
      for(i = 0; i < pclient->nb_pre_create_dirs; i++)
        {
          if(cache_inode_async_precreated_name(&name,
                                               pclient,
                                               object_type, pexport_context) != 0)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in creating name");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          attr.asked_attributes = FSAL_ATTRS_POSIX;
          fsal_status = FSAL_mkdir(&pre_created_dir_handle,
                                   &name,
                                   &root_fsal_context,
                                   (fsal_accessmode_t) 0777,
                                   &pclient->dir_pool_handle[i], &attr);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_mkdir");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          snprintf(destname, MAXNAMLEN, "dir.export=%llu.fileid=%llu",
                   FSAL_EXPORT_CONTEXT_SPECIFIC(pexport_context), attr.fileid);

          fsal_status = FSAL_str2name(destname, MAXNAMLEN, &fileidname);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_str2name");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          attr_src.asked_attributes = FSAL_ATTRS_POSIX;
          attr_dest.asked_attributes = FSAL_ATTRS_POSIX;
          fsal_status = FSAL_rename(&pre_created_dir_handle,
                                    &name,
                                    &pre_created_dir_handle,
                                    &fileidname,
                                    &root_fsal_context, &attr_src, &attr_dest);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_rename");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          pclient->dir_pool_fileid[i] = attr.fileid;

        }
      pclient->avail_precreated_dirs += pclient->nb_pre_create_dirs;
      cpt = pclient->nb_pre_create_dirs;

      break;

    case REGULAR_FILE:
      for(i = 0; i < pclient->nb_pre_create_files; i++)
        {
          if(cache_inode_async_precreated_name(&name,
                                               pclient,
                                               object_type, pexport_context) != 0)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in creating name");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          /* Create the object */
          attr.asked_attributes = FSAL_ATTRS_POSIX;
          fsal_status = FSAL_create(&pre_created_dir_handle,
                                    &name,
                                    &root_fsal_context,
                                    (fsal_accessmode_t) 0777,
                                    &pclient->file_pool_handle[i], &attr);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_create");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          pclient->file_pool_fileid[i] = attr.fileid;

          snprintf(destname, MAXNAMLEN, "file.export=%llu.fileid=%llu",
                   FSAL_EXPORT_CONTEXT_SPECIFIC(pexport_context), attr.fileid);

          fsal_status = FSAL_str2name(destname, MAXNAMLEN, &fileidname);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_str2name");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

          fsal_status = FSAL_rename(&pre_created_dir_handle,
                                    &name,
                                    &pre_created_dir_handle,
                                    &fileidname,
                                    &root_fsal_context, &attr_src, &attr_dest);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_async_precreate_object failed: error in FSAL_rename");
              LogError(COMPONENT_CACHE_INODE, ERR_FSAL, fsal_status.major, fsal_status.minor);
              return -1;
            }

        }
      pclient->avail_precreated_files += pclient->nb_pre_create_files;
      cpt = pclient->nb_pre_create_dirs;

      break;

    default:
      /* Something that is not a directory of a file... unsupported */
      LogCrit(COMPONENT_CACHE_INODE,
          "/!\\ cache_inode_async_precreate_object: I can't pre-create an object of type %x",
           object_type);
      return -1;
      break;                    /* useless... but if this statement is not there, the compiler may be weeping... */
    }                           /* switch( object_type ) */

  return cpt;
}                               /* cache_inode_async_precreate_object */

/**
 * cache_inode_synclet_thread: thread used for asynchronous cache inode management.
 *
 * This thread is used for managing asynchrous inode management
 *
 * @param IndexArg the index for the thread 
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *cache_inode_synclet_thread(void *Arg)
{
  long index = 0;
  unsigned int found = FALSE;
  LRU_entry_t *pentry_lru = NULL;
  cache_entry_t *pentry = NULL;
  char namestr[64];
  int passcounter = 0;
  int rc = 0;
  cache_inode_status_t cache_status;
  cache_inode_async_op_desc_t *piter_opdesc = NULL;
  cache_inode_async_op_desc_t *pasyncoplist = NULL;
  index = (long)Arg;
  sprintf(namestr, "Synclet #%d", index);
  SetNameFunction(namestr);
  LogDebug(COMPONENT_CACHE_INODE,"Started", index);

  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogMajor(COMPONENT_CACHE_INODE,"Memory manager could not be initialized, exiting...");
      exit(1);
    }
  LogEvent(COMPONENT_CACHE_INODE,"Memory manager successfully initialized");

  while(1)
    {
      /* Waiting for the ATD to submit some stuff in the op_lru pending list */
      P(synclet_data[index].mutex_op_condvar);
      while(synclet_data[index].op_lru->nb_entry ==
            synclet_data[index].op_lru->nb_invalid)
        pthread_cond_wait(&(synclet_data[index].op_condvar),
                          &(synclet_data[index].mutex_op_condvar));

      LogDebug(COMPONENT_CACHE_INODE, "I have an entry to deal with");

      for(pentry_lru = synclet_data[index].op_lru->LRU; pentry_lru != NULL;
          pentry_lru = pentry_lru->next)
        {
          if(pentry_lru->valid_state == LRU_ENTRY_VALID)
            {
              found = TRUE;
              break;
            }
        }

      if(!found)
        {
          V(synclet_data[index].mutex_op_condvar);
          LogMajor(COMPONENT_CACHE_INODE, "/!\\ Received a signal but no entry to manage... ");
          continue;             /* return to main loop */
        }

      /* An entry was found !!!! */
      pentry = (cache_entry_t *) (pentry_lru->buffdata.pdata);
      V(synclet_data[index].mutex_op_condvar);

      LogDebug(COMPONENT_CACHE_INODE, "I will proceed entry %p", pentry);

      /* Here: Proceed with the asyncop list */
      LogFullDebug(COMPONENT_CACHE_INODE, "=========> pending_ops = %p\n", pentry->pending_ops);
      P(pentry->lock);

      /* Possible to pending ops reduction */
      if(cache_inode_async_op_reduce(pentry->pending_ops, &pasyncoplist, &cache_status)
         != CACHE_INODE_SUCCESS)
        {
          LogMajor(COMPONENT_CACHE_INODE,
                          "Couldn't reduce pending async op list for pentry %p", pentry);
        }

      for(piter_opdesc = pasyncoplist; piter_opdesc != NULL;
          piter_opdesc = piter_opdesc->next)
        {
          LogDebug(COMPONENT_CACHE_INODE, "I will proceed Asyncop=%p on entry=%p",
                          piter_opdesc, pentry);

          cache_status = cache_inode_process_async_op(piter_opdesc,
                                                      pentry, &cache_status);
          LogFullDebug(COMPONENT_CACHE_INODE, "===============> cache_inode_process_async_op status=%d\n",
                 cache_status);

          /* Release this entry to the pool it came from */
          pthread_mutex_lock(piter_opdesc->ppool_lock);
          RELEASE_PREALLOC(piter_opdesc, piter_opdesc->origine_pool, next_alloc);
          pthread_mutex_unlock(piter_opdesc->ppool_lock);
        }

      if((cache_status = cache_inode_resync_entry(pentry,
                                                  pentry->pending_ops->ht,
                                                  &pentry->pending_ops->fsal_op_context,
                                                  &cache_status)) != CACHE_INODE_SUCCESS)
        {
          LogCrit(COMPONENT_CACHE_INODE,"/!\\ Could not resync pentry %p\n");
        }

      LogDebug(COMPONENT_CACHE_INODE,"===============> cache_inode_resync_entry status=%d\n", cache_status);

      /* Reset the pentry's pending_ops list to show async op were proceeded */
      pentry->pending_ops = NULL;

      V(pentry->lock);

      /* End of work: invalidate this LRU entry */
      P(synclet_data[index].mutex_op_condvar);
      if(LRU_invalidate(synclet_data[index].op_lru, pentry_lru) != LRU_LIST_SUCCESS)
        {
          LogCrit(COMPONENT_CACHE_INODE,
              "Incoherency: released entry for dispatch could not be tagged invalid");
        }
      V(synclet_data[index].mutex_op_condvar);

      /* Increment the passcounter */
      passcounter += 1;

      /* If neeeded, perform GC on LRU */
      if(client_parameter.nb_before_gc < passcounter)
        {
          /* Its time for LRU gc */
          passcounter = 0;

          if(LRU_gc_invalid(synclet_data[index].op_lru, (void *)NULL) != LRU_LIST_SUCCESS)
            {
              LogCrit(COMPONENT_CACHE_INODE,"/!\\  Could not recover invalid entries from LRU...");
            }
          else
            LogDebug(COMPONENT_CACHE_INODE, "LRU_gc_invalid OK");
        }

    }                           /* while ( 1 ) */

  /* Should not occure (neverending loop) */
  return NULL;
}                               /* cache_inode_synclet_thread */

/**
 * cache_inode_asynchronous_dispatcher_thread: this thread will assign asynchronous operation to the synclets.
 *
 *
 * @param Arg (unused)
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *cache_inode_asynchronous_dispatcher_thread(void *Arg)
{
  cache_entry_t *pentry_iter = NULL;
  unsigned int synclet_index = 0;
  LRU_entry_t *pentry_lru = NULL;
  LRU_status_t lru_status;

  SetNameFunction("ATD");
  while(1)
    {
     /** @todo: BUGAZOMEU a call to usleep may provide a finer control */
      sleep(client_parameter.atd_sleeptime);
      LogDebug(COMPONENT_CACHE_INODE, "Awakening...");

      P(mutex_async_pentries_list);

      for(pentry_iter = async_pentries_list; pentry_iter != NULL;
          pentry_iter = pentry_iter->next_asyncop)
        {
          LogDebug(COMPONENT_CACHE_INODE, "Pentry %p needs md-writeback operations to be made",
                          pentry_iter);

          /* Choose a synclet for this pentry */
          synclet_index = cache_inode_async_choose_synclet();

          P(synclet_data[synclet_index].mutex_op_condvar);
          if((pentry_lru =
              LRU_new_entry(synclet_data[synclet_index].op_lru, &lru_status)) == NULL)
            {
              LogMajor(COMPONENT_CACHE_INODE,
                              "Error while inserting entry to synclet #%u... exiting",
                              synclet_index);
              exit(1);
            }

          pentry_lru->buffdata.pdata = (caddr_t) pentry_iter;
          pentry_lru->buffdata.len = 0;

          if(pthread_cond_signal(&(synclet_data[synclet_index].op_condvar)) == -1)
            {
              V(synclet_data[synclet_index].mutex_op_condvar);
              LogCrit(COMPONENT_CACHE_INODE,"Cond signal failed for Synclet#%u , errno = %d", synclet_index,
                         errno);
              exit(1);
            }

          V(synclet_data[synclet_index].mutex_op_condvar);
        }                       /* for( pentry_iter = async_pentries_list ; pentry_iter != NULL ; pentry_iter = pentry_iter->next_asyncop ) */

      /* All entries are managed, nullify the list */
      async_pentries_list = NULL;
      V(mutex_async_pentries_list);

    }                           /* while( 1 ) */
  /* Should not occure (neverending loop) */
  return NULL;
}                               /* cache_inode_asynchronous_dispatcher_thread */
