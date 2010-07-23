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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "stuff_alloc.h"
#ifdef _USE_PNFS
#include "pnfs.h"
#endif                          /* _USE_PNFS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_create: creates an entry through the cache.
 *
 * Creates an entry through the cache.
 *
 * @param pentry_parent [IN] pointer to the pentry parent
 * @param pname         [IN] pointer to the name of the object in the destination directory.
 * @param type          [IN] type of the object to be created.
 * @param mode          [IN] mode to be used at file creation
 * @param pcreate_arg   [IN] additional argument for object creation
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
#ifdef _USE_MFSL
  mfsl_object_t object_handle;
#else
  fsal_handle_t object_handle;
#endif
  fsal_attrib_list_t parent_attributes;
  fsal_attrib_list_t object_attributes;
  fsal_handle_t dir_handle;
  cache_inode_fsal_data_t fsal_data;
  cache_inode_status_t status;
  int pnfs_status;

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

  /* Check if caller is allowed to perform the operation */
  if((status = cache_inode_access(pentry_parent,
                                  FSAL_W_OK,
                                  ht, pclient, pcontext, &status)) != CACHE_INODE_SUCCESS)
    {
      *pstatus = status;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      /* pentry is a directory */
      return NULL;
    }

  /* Check if an entry of the same name exists */
  if((pentry = cache_inode_lookup(pentry_parent,
                                  pname,
                                  &object_attributes,
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

  /* Get the lock for the parent */
  P_w(&pentry_parent->lock);

  if(pentry_parent->internal_md.type == DIR_BEGINNING)
    dir_handle = pentry_parent->object.dir_begin.handle;

  if(pentry_parent->internal_md.type == DIR_CONTINUE)
    {
      P_r(&pentry_parent->object.dir_cont.pdir_begin->lock);
      dir_handle = pentry_parent->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V_r(&pentry_parent->object.dir_cont.pdir_begin->lock);
    }

  object_attributes.asked_attributes = pclient->attrmask;
  switch (type)
    {
    case REGULAR_FILE:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &parent_attributes);
      fsal_status = MFSL_create(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                &object_handle, &object_attributes, &parent_attributes);
#else
      fsal_status = FSAL_create(&dir_handle,
                                pname,
                                pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case DIR_BEGINNING:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &parent_attributes);
      fsal_status = MFSL_mkdir(&pentry_parent->mobject,
                               pname,
                               pcontext,
                               &pclient->mfsl_context,
                               mode,
                               &object_handle, &object_attributes, &parent_attributes);
#else
      fsal_status = FSAL_mkdir(&dir_handle,
                               pname, pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case SYMBOLIC_LINK:
#ifdef _USE_MFSL
      cache_inode_get_attributes(pentry_parent, &object_attributes);
      fsal_status = MFSL_symlink(&pentry_parent->mobject,
                                 pname,
                                 &pcreate_arg->link_content,
                                 pcontext,
                                 &pclient->mfsl_context,
                                 mode, &object_handle, &object_attributes);
#else
      fsal_status = FSAL_symlink(&dir_handle,
                                 pname,
                                 &pcreate_arg->link_content,
                                 pcontext, mode, &object_handle, &object_attributes);
#endif
      break;

    case SOCKET_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject, pname, pcontext, &pclient->mfsl_context, mode, FSAL_TYPE_SOCK, NULL,   /* no dev_t needed for socket file */
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle, pname, pcontext, mode, FSAL_TYPE_SOCK, NULL,       /* no dev_t needed for socket file */
                                &object_handle, &object_attributes);
#endif
      break;

    case FIFO_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject, pname, pcontext, &pclient->mfsl_context, mode, FSAL_TYPE_FIFO, NULL,   /* no dev_t needed for FIFO file */
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle, pname, pcontext, mode, FSAL_TYPE_FIFO, NULL,       /* no dev_t needed for FIFO file */
                                &object_handle, &object_attributes);
#endif
      break;

    case BLOCK_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                FSAL_TYPE_BLK,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle,
                                pname,
                                pcontext,
                                mode,
                                FSAL_TYPE_BLK,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#endif
      break;

    case CHARACTER_FILE:
#ifdef _USE_MFSL
      fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                pname,
                                pcontext,
                                &pclient->mfsl_context,
                                mode,
                                FSAL_TYPE_CHR,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#else
      fsal_status = FSAL_mknode(&dir_handle,
                                pname,
                                pcontext,
                                mode,
                                FSAL_TYPE_CHR,
                                &pcreate_arg->dev_spec,
                                &object_handle, &object_attributes);
#endif
      break;

    default:
      /* we should never go there */
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
      V_w(&pentry_parent->lock);

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
      break;
    }

  /* Check for the result */
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V_w(&pentry_parent->lock);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          DisplayLog
              ("cache_inode_create: Stale FSAL File Handle detected for pentry = %p",
               pentry_parent);

          if(cache_inode_kill_entry(pentry_parent, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            DisplayLog("cache_inode_create: Could not kill entry %p, status = %u",
                       pentry_parent, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

      return NULL;
    }
  else
    {
#ifdef _USE_MFSL
      fsal_data.handle = object_handle.handle;
#else
      fsal_data.handle = object_handle;
#endif
      fsal_data.cookie = DIR_START;

      /* This call will return NULL if failed */
      if((pentry = cache_inode_new_entry(&fsal_data, &object_attributes, type, pcreate_arg, NULL, ht, pclient, pcontext, TRUE,  /* This is a creation and not a population */
                                         pstatus)) == NULL)
        {
          *pstatus = CACHE_INODE_INSERT_ERROR;
          V_w(&pentry_parent->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
#ifdef _USE_MFSL
      /* Copy the MFSL object to the cache */
      memcpy((char *)&(pentry->mobject), (char *)&object_handle, sizeof(mfsl_object_t));
#endif

      /* Add this entry to the directory */
      if(cache_inode_add_cached_dirent(pentry_parent,
                                       pname,
                                       pentry,
                                       NULL,
                                       ht,
                                       pclient, pcontext, pstatus) != CACHE_INODE_SUCCESS)
        {
          V_w(&pentry_parent->lock);

          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_CREATE] += 1;

          return NULL;
        }
    }

#ifdef _USE_PNFS
  if(type == REGULAR_FILE)
    if(pcreate_arg != NULL)
      if(pcreate_arg->use_pnfs == TRUE)
        if((pnfs_status = pnfs_create_ds_file(&pclient->pnfsclient,
                                              pentry->object.file.attributes.fileid,
                                              &pentry->object.file.pnfs_file.ds_file)) !=
           NFS4_OK)
          {
            V_w(&pentry_parent->lock);

            DisplayLogLevel(NIV_DEBUG, "OPEN PNFS CREATE DS FILE : Error %u",
                            pnfs_status);

            *pstatus = CACHE_INODE_IO_ERROR;
            return NULL;
          }
#endif

  /* Update the parent cached attributes */
  if(pentry_parent->internal_md.type == DIR_BEGINNING)
    {
      pentry_parent->object.dir_begin.attributes.mtime.seconds = time(NULL);
      pentry_parent->object.dir_begin.attributes.mtime.nseconds = 0;
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
  V_w(&pentry_parent->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_CREATE] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_CREATE] += 1;

  return pentry;
}                               /* cache_inode_create */

/**
 *
 * cache_inode_create_open: creates a file and opens it at the same time (for NFSv4 semantics)
 *
 * Creates an entry through the cache.
 *
 * @param pentry_parent [IN] pointer to the pentry parent
 * @param pname         [IN] pointer to the name of the object in the destination directory.
 * @param type          [IN] type of the object to be created.
 * @param mode          [IN] mode to be used at file creation
 * @param openflags     [IN] flags to be used during file open
 * @param pcreate_arg   [IN] additional argument for object creation
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
cache_entry_t *cache_inode_create_open(cache_entry_t * pentry_parent,
                                       fsal_name_t * pname,
                                       cache_inode_file_type_t type,
                                       fsal_accessmode_t mode,
                                       cache_inode_create_arg_t * pcreate_arg,
                                       fsal_openflags_t openflags,
                                       fsal_attrib_list_t * pattr,
                                       hash_table_t * ht,
                                       cache_inode_client_t * pclient,
                                       fsal_op_context_t * pcontext,
                                       cache_inode_status_t * pstatus)
{
  return NULL;
}                               /* cache_inode_create */
