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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include "LRU_List.h"
#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

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
                                      cache_inode_policy_t policy,
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
  cache_inode_dir_entry_t *new_dir_entry;
#ifdef _USE_MFSL
  fsal_attrib_list_t dirdest_attributes;
#endif
  cache_inode_status_t status;
  cache_entry_t *pentry_lookup = NULL;
  fsal_attrib_list_t lookup_attributes;

  fsal_size_t save_size = 0;
  fsal_size_t save_spaceused = 0;
  fsal_time_t save_mtime = {
    .seconds = 0,
    .nseconds = 0
  };

  fsal_accessflags_t access_mask = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_LINK] += 1;

  /* Is the destination a directory ? */
  if(pentry_dir_dest->internal_md.type != DIRECTORY)
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

  /* Check if caller is allowed to perform the operation */
  access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
  if((status = cache_inode_access(pentry_dir_dest,
                                  access_mask,
                                  ht, pclient, pcontext, &status)) != CACHE_INODE_SUCCESS)
    {
      *pstatus = status;

      /* stats */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      /* pentry is a directory */
      return *pstatus;
    }

  /* Check if an entry of the same name doesn't exist in the destination directory */
  if((pentry_lookup = cache_inode_lookup( pentry_dir_dest,
                                          plink_name,
                                          policy,
                                          &lookup_attributes,
                                          ht,
                                          pclient, 
                                          pcontext, 
                                          pstatus ) ) != NULL )
    {
      /* There exists such an entry... */
      *pstatus = CACHE_INODE_ENTRY_EXISTS;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

  /* The pentry to be hardlinked can't be a DIRECTORY */
  if(pentry_src->internal_md.type == DIRECTORY)
    {
      /* Bad type .... */
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;

      return *pstatus;
    }

#if 0
  if( pentry_src->internal_md.type == REGULAR_FILE )
    printf( "=== link === %p | inode=%llu\n", pentry_src, pentry_src->object.file.attributes.fileid ) ;
#endif

  /* At this point, we know that the entry does not exist in destination directory, we know that the
   * destination is actually a directory and that the source is no directory */

  /* Lock the source */
  P_w(&pentry_src->lock);

  /* Lock the target dir */
  P_w(&pentry_dir_dest->lock);

  /* Get the handles */
  switch (pentry_src->internal_md.type)
    {
    case REGULAR_FILE:
      handle_src = pentry_src->object.file.handle;
      break;

    case SYMBOLIC_LINK:
      assert(pentry_src->object.symlink);
      handle_src = pentry_src->object.symlink->handle;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      handle_src = pentry_src->object.dir.handle;
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      handle_src = pentry_src->object.special_obj.handle;
      break;

    case UNASSIGNED:
    case RECYCLED:
      LogCrit(COMPONENT_CACHE_INODE,
              "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
              pentry_src->internal_md.type, __LINE__, __FILE__);
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;
      return *pstatus;
    }

  switch (pentry_dir_dest->internal_md.type)
    {
    case FS_JUNCTION:
    case DIRECTORY:
      handle_dest = pentry_dir_dest->object.dir.handle;
      break;

    default:
      LogCrit(COMPONENT_CACHE_INODE,
              "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
              pentry_src->internal_md.type, __LINE__, __FILE__);
      *pstatus = CACHE_INODE_BAD_TYPE;
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LINK] += 1;
      return *pstatus;
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
  link_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
  cache_inode_get_attributes(pentry_src, &link_attributes);
  cache_inode_get_attributes(pentry_dir_dest, &dirdest_attributes);
  fsal_status =
      MFSL_link(&pentry_src->mobject, &pentry_dir_dest->mobject, plink_name, pcontext,
                &pclient->mfsl_context, &link_attributes, NULL);
#else
  fsal_status =
      FSAL_link(&handle_src, &handle_dest, plink_name, pcontext, &link_attributes);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V_w(&pentry_dir_dest->lock);
      V_w(&pentry_src->lock);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;
          fsal_status_t getattr_status;

          LogEvent(COMPONENT_CACHE_INODE,
                   "cache_inode_link: Stale FSAL File Handle detected for at least one in pentry = %p and pentry = %p",
                   pentry_src, pentry_dir_dest);

          /* Use FSAL_getattrs to find which entry is staled */
          getattr_status = FSAL_getattrs(&handle_src, pcontext, &link_attributes);
          if(getattr_status.major == ERR_FSAL_ACCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_link: Stale FSAL File Handle detected for pentry = %p",
                       pentry_src);

              if(cache_inode_kill_entry(pentry_src, WT_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_link: Could not kill entry %p, status = %u",
                        pentry_src, kill_status);
            }

          getattr_status = FSAL_getattrs(&handle_dest, pcontext, &link_attributes);
          if(getattr_status.major == ERR_FSAL_ACCESS)
            {
              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_link: Stale FSAL File Handle detected for pentry = %p",
                       pentry_dir_dest);

              if(cache_inode_kill_entry(pentry_dir_dest, WT_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_link: Could not kill entry %p, status = %u",
                        pentry_dir_dest, kill_status);
            }

        }
      *pstatus = CACHE_INODE_FSAL_ESTALE;

      return *pstatus;
    }

  /* If object is a data cached regular file, keeps it mtime and size, STEP 2 */
  if((pentry_src->internal_md.type == REGULAR_FILE)
     && (pentry_src->object.file.pentry_content != NULL))
    {
      link_attributes.mtime = save_mtime;
      link_attributes.filesize = save_size;
      link_attributes.spaceused = save_spaceused;
    }

  /* Update cached attributes */
  cache_inode_set_attributes(pentry_src, &link_attributes);

  /* Add the new entry in the destination directory */
  if(cache_inode_add_cached_dirent(pentry_dir_dest,
                                   plink_name,
                                   pentry_src,
                                   ht,
				   &new_dir_entry,
                                   pclient,
                                   pcontext,
                                   &status) != CACHE_INODE_SUCCESS)
    {
      V_w(&pentry_dir_dest->lock);
      V_w(&pentry_src->lock);
      return *pstatus;
    }

  /* Regular exit */

  /* return the attributes */
  *pattr = link_attributes;

  /* Validate the entries */
  *pstatus = cache_inode_valid(pentry_src, CACHE_INODE_OP_SET, pclient);

  /* Release the target dir */
  V_w(&pentry_dir_dest->lock);

  /* Release the source */
  V_w(&pentry_src->lock);

  /* stats */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LINK] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_LINK] += 1;

  return *pstatus;
}
