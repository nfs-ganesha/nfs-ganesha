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
 * \file    cache_inode_renew_entry.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/16 08:22:29 $
 * \version $Revision: 1.13 $
 * \brief   Renews an entry in the cache inode.
 *
 * cache_inode_renew_entry.c : Renews an entry in the cache inode.
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
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * cache_inode_renew_entry: Renews the attributes for an entry.
 *
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided
 * with compliance to the underlying FSAL semantics. Attributes that are set are returned in "*pattr".
 *
 * @param pentry_parent [IN] entry for the parent directory to be managed.
 * @param pattr [OUT] renewed attributes for the entry that we have found.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
   @return Other errors shows a FSAL error.
 *
 */
cache_inode_status_t cache_inode_renew_entry(cache_entry_t * pentry,
                                             fsal_attrib_list_t * pattr,
                                             hash_table_t * ht,
                                             cache_inode_client_t * pclient,
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

  if(isFullDebug(COMPONENT_CACHE_INODE))
    {
      char *type;
      char grace[20], grace2[20];
      unsigned int elapsed = (unsigned int)current_time - (unsigned int)entry_time;
      int print = 1;

      cache_inode_expire_to_str(pclient->expire_type_attr, pclient->grace_period_attr, grace);

      switch(pentry->internal_md.type)
        {
          case UNASSIGNED:
            type = "UNASSIGNED";
            break;
          case REGULAR_FILE:
            type = "REGULAR_FILE";
            break;
          case CHARACTER_FILE:
            type = "CHARACTER_FILE";
            break;
          case BLOCK_FILE:
            type = "BLOCK_FILE";
            break;
          case SYMBOLIC_LINK:
            print = 0;
            cache_inode_expire_to_str(pclient->expire_type_link, pclient->grace_period_link, grace2);
            LogDebug(COMPONENT_CACHE_INODE,
                     "Renew Entry test of %p for SYMBOLIC_LINK elapsed time=%u, grace_period_attr=%s, grace_period_link=%s",
                     pentry, elapsed, grace, grace2);
            break;
          case SOCKET_FILE:
            type = "SOCKET_FILE";
            break;
          case FIFO_FILE:
            type = "FIFO_FILE";
            break;
          case DIRECTORY:
            print = 0;
            cache_inode_expire_to_str(pclient->expire_type_dirent, pclient->grace_period_dirent, grace2);
            LogDebug(COMPONENT_CACHE_INODE,
                     "Renew Entry test of %p for DIRECTORY elapsed time=%u, grace_period_attr=%s, grace_period_dirent=%s, has_been_readdir=%u",
                     pentry, elapsed, grace, grace2,
                     pentry->object.dir.has_been_readdir);
            break;
          case FS_JUNCTION:
            type = "FS_JUNCTION";
            break;
          case RECYCLED:
            type = "RECYCLED";
            break;
          default:
            type = "UNKNOWN";
            break;
        }
      if(print)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Renew Entry test of %p for %s elapsed time=%u, grace_period_attr=%s",
                   pentry, type, elapsed, grace);
        }
    }

  /* An entry that is a regular file with an associated File Content Entry won't
   * expire until data exists in File Content Cache, to avoid attributes incoherency */

  /* @todo: BUGAZOMEU: I got serious doubts on the following blocks: possible trouble if using data caching */
  if(pentry->internal_md.type == REGULAR_FILE &&
     pentry->object.file.pentry_content != NULL)
    {
      /* Successfull exit without having nothing to do ... */

      LogDebug(COMPONENT_CACHE_INODE,
               "Entry %p is a REGULAR_FILE with associated data cached %p, no expiration",
               pentry, pentry->object.file.pentry_content);

      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }

  LogDebug(COMPONENT_CACHE_INODE,
           "cache_inode_renew_entry use getattr/mtime checking %d, is dir "
	   "beginning %d, has bit in mask %d, has been readdir %d state %d",
           pclient->getattr_dir_invalidation,
           pentry->internal_md.type == DIRECTORY,
           (int) FSAL_TEST_MASK(pclient->attrmask, FSAL_ATTR_MTIME),
	   pentry->object.dir.has_been_readdir,
	   pentry->internal_md.valid_state);
  /* Do we use getattr/mtime checking */
  if(pclient->getattr_dir_invalidation &&
     pentry->internal_md.type == DIRECTORY &&
     FSAL_TEST_MASK(pclient->attrmask, FSAL_ATTR_MTIME) /*&&
     pentry->object.dir.has_been_readdir == CACHE_INODE_YES*/)
    {
      /* This checking is to be done ... */
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_renew_entry testing directory mtime");
      pfsal_handle = &pentry->object.dir.handle;

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = pclient->attrmask;

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

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_renew_entry: Could not kill entry %p, status = %u",
                         pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }
          /* stats */
          (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY])++;

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_renew_entry: returning %d (%s) from FSAL_getattrs for getattr/mtime checking",
                   *pstatus, cache_inode_err_str(*pstatus));
          return *pstatus;
        }

      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_renew_entry: Entry=%p, type=%d, Cached Time=%d, FSAL Time=%d",
               pentry, pentry->internal_md.type,
               pentry->object.dir.attributes.mtime.seconds,
               object_attributes.mtime.seconds);

      /* Compare the FSAL mtime and the cached mtime */
      if(pentry->object.dir.attributes.mtime.seconds <
         object_attributes.mtime.seconds)
        {
          /* Cached directory content is obsolete, it must be renewed */
          cache_inode_set_attributes(pentry, &object_attributes);

          /* Return the attributes as set */
          if(pattr != NULL)
            *pattr = object_attributes;

          /* Set the directory content as "to be renewed" */
          /* Next call to cache_inode_readdir will repopulate the dirent array */
          pentry->object.dir.has_been_readdir = CACHE_INODE_RENEW_NEEDED;

          /* Set the refresh time for the cache entry */
          pentry->internal_md.refresh_time = time(NULL);

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_renew_entry: cached directory content for entry %p must be renewed, due to getattr mismatch",
                   pentry);

          if(cache_inode_invalidate_all_cached_dirent(pentry, ht, pclient, pstatus)
             != CACHE_INODE_SUCCESS)
            {
              /* Should never happen */
              LogCrit(COMPONENT_CACHE_INODE,
                      "cache_inode_invalidate_all_cached_dirent returned %d (%s)",
                      *pstatus, cache_inode_err_str(*pstatus));
              return *pstatus;
            }

        }                       /* if( pentry->object.dir.attributes.mtime < object_attributes.asked_attributes.mtime ) */
    }

  /* if( pclient->getattr_dir_invalidation && ... */
  /* Check for dir content expiration and/or staleness */
  if(pentry->internal_md.type == DIRECTORY &&
     pclient->expire_type_dirent != CACHE_INODE_EXPIRE_NEVER &&
     pentry->object.dir.has_been_readdir == CACHE_INODE_YES &&
     ((current_time - entry_time >= pclient->grace_period_dirent)
      || (pentry->internal_md.valid_state == STALE)))
    {
      /* Would be better if state was a flag that we could and/or the bits but
       * in any case we need to get rid of stale so we only go through here
       * once.
       */
      if ( pentry->internal_md.valid_state == STALE )
	pentry->internal_md.valid_state = VALID;

      /* stats */
      (pclient->stat.func_stats.nb_call[CACHE_INODE_RENEW_ENTRY])++;

      /* Log */
      LogDebug(COMPONENT_CACHE_INODE,
	       "Case 1: cached directory entries for entry %p must be renewed"
	       " (has been readdir)", pentry);

      if(isFullDebug(COMPONENT_CACHE_INODE))
        {
          char name[1024];
	  struct avltree_node *d_node;
	  cache_inode_dir_entry_t *d_dirent;
	  int i = 0;
	  
	  d_node = avltree_first(&pentry->object.dir.dentries);
      	  do {
              d_dirent = avltree_container_of(d_node, cache_inode_dir_entry_t,
					      node_n);
	      if (d_dirent->pentry->internal_md.valid_state == VALID) {
	          FSAL_name2str(&(d_dirent->name), name, 1023);
                  LogDebug(COMPONENT_CACHE_INODE,
                           "cache_inode_renew_entry: Entry %d %s",
                           i, name);
	      }
	      i++;
          } while ((d_node = avltree_next(d_node)));
        }

      /* Do the getattr if it had not being done before */
      if(pfsal_handle == NULL)
        {
          pfsal_handle = &pentry->object.dir.handle;

          /* Call FSAL to get the attributes */
          object_attributes.asked_attributes = pclient->attrmask;

          fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);

          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);

              /* stats */
              (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY])++;

              if(fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  LogEvent(COMPONENT_CACHE_INODE,
                           "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                           pentry, __LINE__);

                  if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                     CACHE_INODE_SUCCESS)
                    LogCrit(COMPONENT_CACHE_INODE,
                            "cache_inode_renew_entry: Could not kill entry %p, status = %u",
                            pentry, kill_status);

                  *pstatus = CACHE_INODE_FSAL_ESTALE;
                }

              LogDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_renew_entry returning %d (%s) from FSAL_getattrs for directory entries (1)",
                       *pstatus, cache_inode_err_str(*pstatus));
              return *pstatus;
            }
        }

      cache_inode_set_attributes(pentry, &object_attributes);

      /* Return the attributes as set */
      if(pattr != NULL)
        *pattr = object_attributes;

      /* Set the directory content as "to be renewed" */
      /* Next call to cache_inode_readdir will repopulate the dirent array */
      pentry->object.dir.has_been_readdir = CACHE_INODE_RENEW_NEEDED;

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if( pentry->internal_md.type == DIRECTORY && ... */
  /* if the directory has not been readdir, only update its attributes */
  else if(pentry->internal_md.type == DIRECTORY &&
          pclient->expire_type_attr != CACHE_INODE_EXPIRE_NEVER &&
          pentry->object.dir.has_been_readdir != CACHE_INODE_YES &&
	  ((current_time - entry_time >= pclient->grace_period_attr) || (pentry->internal_md.valid_state == STALE)))
    {
      /* Would be better if state was a flag that we could and/or the bits but
       * in any case we need to get rid of stale so we only go through here
       * once.
       */
      if ( pentry->internal_md.valid_state == STALE )
         pentry->internal_md.valid_state = VALID;

      /* stats */
      (pclient->stat.func_stats.nb_call[CACHE_INODE_RENEW_ENTRY])++;

      /* Log */
      LogDebug(COMPONENT_CACHE_INODE,
	       "Case 2: cached directory entries for entry %p must be renewed (has not been readdir)",
               pentry);

      if(isFullDebug(COMPONENT_CACHE_INODE))
        {
          char name[1024];
	  struct avltree_node *d_node;
	  cache_inode_dir_entry_t *d_dirent;
	  int i = 0;
	  
	  d_node = avltree_first(&pentry->object.dir.dentries);
      	  do {
              d_dirent = avltree_container_of(d_node, cache_inode_dir_entry_t,
					      node_n);
	      if (d_dirent->pentry->internal_md.valid_state == VALID) {
	          FSAL_name2str(&(d_dirent->name), name, 1023);
                  LogDebug(COMPONENT_CACHE_INODE,
                           "cache_inode_renew_entry: Entry %d %s",
                           i, name);
	      }
	      i++;
          } while ((d_node = avltree_next(d_node)));
        }

      pfsal_handle = &pentry->object.dir.handle;

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = pclient->attrmask;

      fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          /* stats */
          (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY])++;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                       pentry, __LINE__);

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_renew_entry: Could not kill entry %p, status = %u",
                         pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_renew_entry returning %d (%s) from FSAL_getattrs for directory entries (2)",
                   *pstatus, cache_inode_err_str(*pstatus));
          return *pstatus;
        }

      cache_inode_set_attributes(pentry, &object_attributes);

      /* Return the attributes as set */
      if(pattr != NULL)
        *pattr = object_attributes;

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* else if( pentry->internal_md.type == DIRECTORY && ... */
  /* Check for attributes expiration in other cases */
  else if(pentry->internal_md.type != DIRECTORY &&
          pclient->expire_type_attr != CACHE_INODE_EXPIRE_NEVER &&
	  ((current_time - entry_time >= pclient->grace_period_attr)
	   || (pentry->internal_md.valid_state == STALE)))
    {
      /* Would be better if state was a flag that we could and/or the bits but
       * in any case we need to get rid of stale so we only go through here
       * once.
       */
      if ( pentry->internal_md.valid_state == STALE )
	pentry->internal_md.valid_state = VALID;
      
      /* stats */
      (pclient->stat.func_stats.nb_call[CACHE_INODE_RENEW_ENTRY])++;

      /* Log */
      LogDebug(COMPONENT_CACHE_INODE,
               "Attributes for entry %p must be renewed", pentry);

      switch (pentry->internal_md.type)
        {
        case REGULAR_FILE:
          pfsal_handle = &pentry->object.file.handle;
          break;

        case SYMBOLIC_LINK:
          assert(pentry->object.symlink);
          pfsal_handle = &pentry->object.symlink->handle;
          break;

        case SOCKET_FILE:
        case FIFO_FILE:
        case CHARACTER_FILE:
        case BLOCK_FILE:
          pfsal_handle = &pentry->object.special_obj.handle;
          break;

        case DIRECTORY:
        case FS_JUNCTION:
        case UNASSIGNED:
        case RECYCLED:
          LogCrit(COMPONENT_CACHE_INODE,
                  "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
                  pentry->internal_md.type, __LINE__, __FILE__);
          *pstatus = CACHE_INODE_BAD_TYPE;
          return *pstatus;
        }

      /* Call FSAL to get the attributes */
      object_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
      fsal_status = FSAL_getattrs_descriptor(&(cache_inode_fd(pentry)->fsal_file), pfsal_handle, pcontext, &object_attributes);
#else
      fsal_status = FSAL_getattrs_descriptor(cache_inode_fd(pentry), pfsal_handle, pcontext, &object_attributes);
#endif
      if(FSAL_IS_ERROR(fsal_status) && fsal_status.major == ERR_FSAL_NOT_OPENED)
        {
          //TODO: LOOKATME !!!!!
          fsal_status = FSAL_getattrs(pfsal_handle, pcontext, &object_attributes);
        }
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);

          /* stats */
          (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY])++;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                       pentry, __LINE__);

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_renew_entry: Could not kill entry %p, status = %u",
                        pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_renew_entry returning %d (%s) from FSAL_getattrs for non directories",
                   *pstatus, cache_inode_err_str(*pstatus));
          return *pstatus;
        }

      /* Keep the new attribute in cache */
      cache_inode_set_attributes(pentry, &object_attributes);

      /* Return the attributes as set */
      if(pattr != NULL)
        *pattr = object_attributes;

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if(  pentry->internal_md.type   != DIR_CONTINUE && ... */
  /* Check for link content expiration */
  if(pentry->internal_md.type == SYMBOLIC_LINK &&
     pclient->expire_type_link != CACHE_INODE_EXPIRE_NEVER &&
     ((current_time - entry_time >= pclient->grace_period_link)
      || (pentry->internal_md.valid_state == STALE)))
    {
      assert(pentry->object.symlink);
      pfsal_handle = &pentry->object.symlink->handle;
      /* Would be better if state was a flag that we could and/or the bits but
       * in any case we need to get rid of stale so we only go through here
       * once.
       */
      if ( pentry->internal_md.valid_state == STALE )
	pentry->internal_md.valid_state = VALID;

      assert(pentry->object.symlink);
      pfsal_handle = &pentry->object.symlink->handle;

      /* Log */
      LogDebug(COMPONENT_CACHE_INODE,
               "cached link content for entry %p must be renewed", pentry);

      FSAL_CLEAR_MASK(object_attributes.asked_attributes);
      FSAL_SET_MASK(object_attributes.asked_attributes, pclient->attrmask);

      if( CACHE_INODE_KEEP_CONTENT( pentry->policy ) )
       {
#ifdef _USE_MFSL
      fsal_status =
          MFSL_readlink(&pentry->mobject, pcontext, &pclient->mfsl_context, &link_content,
                        &object_attributes, NULL);
#else
      fsal_status =
          FSAL_readlink(pfsal_handle, pcontext, &link_content, &object_attributes);
#endif
        }
      else
        { 
          fsal_status.major = ERR_FSAL_NO_ERROR ;
          fsal_status.minor = 0 ;
        }

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          /* stats */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY] += 1;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_renew_entry: Stale FSAL File Handle detected for pentry = %p, line %u",
                       pentry, __LINE__);

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                       "cache_inode_renew_entry: Could not kill entry %p, status = %u",
                       pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

        }
      else
        {
	  assert(pentry->object.symlink);
          fsal_status = FSAL_pathcpy(&pentry->object.symlink->content, &link_content); /* copy ctor? */
          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);
              /* stats */
              pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_RENEW_ENTRY] += 1;
            }
        }

      /* Set the refresh time for the cache entry */
      pentry->internal_md.refresh_time = time(NULL);

    }

  /* if( pentry->internal_md.type == SYMBOLIC_LINK && ... */
  LogDebug(COMPONENT_CACHE_INODE, "cache_inode_renew_entry returning %d (%s)",
           *pstatus, cache_inode_err_str(*pstatus));
  return *pstatus;
}                               /* cache_inode_renew_entry */
