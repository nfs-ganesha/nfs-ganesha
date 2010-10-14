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
 * \file    cache_inode_link.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.16 $
 * \brief   Creation of an hardlink.  
 *
 * cache_inode_link.c : Creation of an hardlink. 
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "LRU_List.h"
#include "log_macros.h"
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

/**
 *
 * cache_inode_async_link: hardlinks an entry (to be called from a synclet)
 *
 * hardlinks an entry.
 *
 * @param popasyncdesc [IN] Cache Inode Asynchonous Operation descriptor
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
fsal_status_t cache_inode_async_link(cache_inode_async_op_desc_t * popasyncdesc)
{
  fsal_status_t fsal_status;

  fsal_status = FSAL_link(popasyncdesc->op_args.link.pfsal_handle_src,
                          popasyncdesc->op_args.link.pfsal_handle_dirdest,
                          &popasyncdesc->op_args.link.name_link,
                          &popasyncdesc->fsal_op_context,
                          &popasyncdesc->op_res.link.attr);

  return fsal_status;
}                               /* cache_inode_aync_rename_src */

/**
 *
 * cache_inode_link: hardlinks a pentry to another. 
 * 
 * Hard links a pentry to another. This is basically a equivalent of FSAL_link in the cache inode layer.
 *
 * @param pentry_src [IN] entry pointer the entry to be linked. This can't be a directory.
 * @param pentry_dir_dest [INOUT] entry pointer for the destination directory in which the link will be created.
 * @param plink_name [IN] pointer to the name of the object in the destination directory.
 * @param pattr [OUT] attributes for the linked attributes after the operation.
 * @param ht [INOUT] hash table used for the cache.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry\n
 * @return CACHE_INODE_BAD_TYPE either source or destination have incorrect type\n
 * @return CACHE_INODE_ENTRY_EXISTS entry of that name already exists in destination.
 *
 */
cache_inode_status_t cache_inode_link(cache_entry_t * pentry_src,
                                      cache_entry_t * pentry_dir_dest,
                                      fsal_name_t * plink_name,
                                      fsal_attrib_list_t * pattr,
                                      hash_table_t * ht,
                                      cache_inode_client_t * pclient,
                                      fsal_op_context_t * pcontext,
                                      cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;
  fsal_handle_t handle_src;
  fsal_handle_t handle_dest;
  fsal_attrib_list_t link_attributes;
  fsal_attrib_list_t dirdest_attr;
  cache_inode_status_t status;
  cache_entry_t *pentry = NULL;
  cache_entry_t *pentry_lookup = NULL;
  fsal_attrib_list_t lookup_attributes;
  hash_buffer_t key;
  hash_buffer_t value;
  cache_inode_fsal_data_t fsdata;
  cache_inode_async_op_desc_t *pasyncopdesc = NULL;

  fsal_size_t save_size;
  fsal_size_t save_spaceused;
  fsal_time_t save_mtime;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LINK] += 1;

  /* Is the destination a directory ? */
  if(pentry_dir_dest->internal_md.type != DIR_BEGINNING &&
     pentry_dir_dest->internal_md.type != DIR_CONTINUE)
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

  /* Check if an entry of the same name doesn't exist in the destination directory */
  if((pentry_lookup = cache_inode_lookup(pentry_dir_dest,
                                         plink_name,
                                         &lookup_attributes,
                                         ht, pclient, pcontext, pstatus)) != NULL)
    {
      /* There exists such an entry... */
      *pstatus = CACHE_INODE_ENTRY_EXISTS;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

  /* The pentry to be hardlinked can't be a DIR_BEGINNING or a DIR_CONTINUE */
  if(pentry_src->internal_md.type == DIR_BEGINNING ||
     pentry_src->internal_md.type == DIR_CONTINUE)
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

  /* At this point, we know that the entry does not exist in destination directory, we know that the 
   * destination is actually a directory and that the source is no directory */

  /* Lock the source */
  P(pentry_src->lock);

  /* Lock the target dir */
  P(pentry_dir_dest->lock);

  /* Get the handles */
  switch (pentry_src->internal_md.type)
    {
    case REGULAR_FILE:
      handle_src = pentry_src->object.file.handle;
      break;

    case SYMBOLIC_LINK:
      handle_src = pentry_src->object.symlink.handle;
      break;

    case DIR_BEGINNING:
      handle_src = pentry_src->object.dir_begin.handle;
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P(pentry_src->object.dir_cont.pdir_begin->lock);
      handle_src = pentry_src->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V(pentry_src->object.dir_cont.pdir_begin->lock);
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      handle_src = pentry_src->object.special_obj.handle;
      break;

    default:
      LogCrit(COMPONENT_CACHE_INODE, 
                        "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
                        pentry_src->internal_md.type, __LINE__, __FILE__);
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;
      return *pstatus;
    }

  switch (pentry_dir_dest->internal_md.type)
    {
    case DIR_BEGINNING:
      handle_dest = pentry_dir_dest->object.dir_begin.handle;
      break;

    case DIR_CONTINUE:
      /* lock the related dir_begin (dir begin are garbagge collected AFTER their related dir_cont)
       * this means that if a DIR_CONTINUE exists, its pdir pointer is not endless */
      P(pentry_dir_dest->object.dir_cont.pdir_begin->lock);
      handle_dest = pentry_src->object.dir_cont.pdir_begin->object.dir_begin.handle;
      V(pentry_dir_dest->object.dir_cont.pdir_begin->lock);
      break;
    }

  /* If object is a data cached regular file, keeps it mtime and size, STEP 1 */
  if((pentry_src->internal_md.type == REGULAR_FILE)
     && (pentry_src->object.file.pentry_content != NULL))
    {
      save_mtime = pentry_src->object.file.attributes.mtime;
      save_size = pentry_src->object.file.attributes.filesize;
      save_spaceused = pentry_src->object.file.attributes.spaceused;
    }

  /* Do the link at FSAL level */
  cache_inode_get_attributes(pentry_dir_dest, &dirdest_attr);

  fsal_status = FSAL_link_access(pcontext, &dirdest_attr);
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V(pentry_dir_dest->lock);
      V(pentry_src->lock);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;
          fsal_status_t getattr_status;

          LogEvent(COMPONENT_CACHE_INODE,
              "cache_inode_link: Stale FSAL File Handle detected for at least one in  pentry = %p and pentry = %p",
               pentry_src, pentry_dir_dest);

          /* Use FSAL_getattrs to find which entry is staled */
          getattr_status = FSAL_getattrs(&handle_src, pcontext, &link_attributes);
          if(getattr_status.major == ERR_FSAL_ACCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_link: Stale FSAL File Handle detected for pentry = %p",
                   pentry_src);

              if(cache_inode_kill_entry(pentry_src, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,"cache_inode_link: Could not kill entry %p, status = %u",
                           pentry_src, kill_status);
            }

          getattr_status = FSAL_getattrs(&handle_dest, pcontext, &link_attributes);
          if(getattr_status.major == ERR_FSAL_ACCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                  "cache_inode_link: Stale FSAL File Handle detected for pentry = %p",
                   pentry_dir_dest);

              if(cache_inode_kill_entry(pentry_dir_dest, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,"cache_inode_link: Could not kill entry %p, status = %u",
                           pentry_dir_dest, kill_status);
            }

        }
      *pstatus = CACHE_INODE_FSAL_ESTALE;

      return *pstatus;
    }

  /* Post an asynchronous operation */
  P(pclient->pool_lock);
  GetFromPool(pasyncopdesc, &pclient->pool_async_op, cache_inode_async_op_desc_t);
  V(pclient->pool_lock);

  if(pasyncopdesc == NULL)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      *pstatus = CACHE_INODE_MALLOC_ERROR;

      V(pentry_dir_dest->lock);
      V(pentry_src->lock);

      return *pstatus;
    }

  pasyncopdesc->op_type = CACHE_INODE_ASYNC_OP_LINK;
  pasyncopdesc->op_args.link.pfsal_handle_src = &handle_src;
  pasyncopdesc->op_args.link.pfsal_handle_dirdest = &handle_dest;
  pasyncopdesc->op_args.link.name_link = *plink_name;
  pasyncopdesc->op_res.link.attr.asked_attributes = FSAL_ATTRS_POSIX;
  pasyncopdesc->op_func = cache_inode_async_link;

  pasyncopdesc->fsal_op_context = *pcontext;
  pasyncopdesc->fsal_export_context = *(pcontext->export_context);
  pasyncopdesc->fsal_op_context.export_context = &pasyncopdesc->fsal_export_context;

  pasyncopdesc->ht = ht;
  pasyncopdesc->origine_pool = pclient->pool_async_op;
  pasyncopdesc->ppool_lock = &pclient->pool_lock;

  if(gettimeofday(&pasyncopdesc->op_time, NULL) != 0)
    {
      /* Could'not get time of day... Stopping, this may need a major failure */
      LogCrit(COMPONENT_CACHE_INODE,"cache_inode_setattr: cannot get time of day... exiting");
      exit(1);
    }

  /* Affect the operation to a synclet */
  if(cache_inode_post_async_op(pasyncopdesc, pentry_src, pstatus) != CACHE_INODE_SUCCESS)
    {
      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      LogCrit(COMPONENT_CACHE_INODE,"WARNING !!! cache_inode_link could not post async op....");

      *pstatus = CACHE_INODE_ASYNC_POST_ERROR;
      V(pentry_dir_dest->lock);
      V(pentry_src->lock);

      return *pstatus;
    }

  /* Update cached attributes */
  switch (pentry_src->internal_md.type)
    {
    case REGULAR_FILE:
      /* If object is a data cached regular file, keeps it mtime and size, STEP 2 */
      if(pentry_src->object.file.pentry_content != NULL)
        {
          pentry_src->object.file.attributes.mtime = save_mtime;
          pentry_src->object.file.attributes.filesize = save_size;
          pentry_src->object.file.attributes.spaceused = save_spaceused;
        }
      pentry_src->object.file.attributes.numlinks += 1;
      pentry_src->object.file.attributes.ctime.seconds = pasyncopdesc->op_time.tv_sec;
      pentry_src->object.file.attributes.ctime.nseconds = pasyncopdesc->op_time.tv_usec;
      *pattr = pentry_src->object.file.attributes;
      break;

    case SYMBOLIC_LINK:
      pentry_src->object.symlink.attributes.numlinks += 1;
      pentry_src->object.symlink.attributes.ctime.seconds = pasyncopdesc->op_time.tv_sec;
      pentry_src->object.symlink.attributes.ctime.nseconds =
          pasyncopdesc->op_time.tv_usec;
      *pattr = pentry_src->object.symlink.attributes;
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      pentry_src->object.special_obj.attributes.numlinks += 1;
      pentry_src->object.special_obj.attributes.ctime.seconds =
          pasyncopdesc->op_time.tv_sec;
      pentry_src->object.special_obj.attributes.ctime.nseconds =
          pasyncopdesc->op_time.tv_usec;
      *pattr = pentry_src->object.special_obj.attributes;
      break;

    default:
      LogCrit(COMPONENT_CACHE_INODE, 
                        "WARNING: Major type incoherency line %d in file %s", __LINE__,
                        __FILE__);
      break;
    }

  /* Add the new entry in the destination directory */
  if(cache_inode_add_cached_dirent(pentry_dir_dest,
                                   plink_name,
                                   pentry_src,
                                   NULL,
                                   ht, pclient, pcontext, &status) != CACHE_INODE_SUCCESS)
    {
      V(pentry_dir_dest->lock);
      V(pentry_src->lock);
      return *pstatus;
    }

  /* Regular exit */

  /* Validate the entries */
  *pstatus = cache_inode_valid(pentry_src, CACHE_INODE_OP_SET, pclient);

  /* Release the target dir */
  V(pentry_dir_dest->lock);

  /* Release the source */
  V(pentry_src->lock);

  /* stats */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LINK] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_LINK] += 1;

  return *pstatus;
}
