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
 * \file    cache_inode_setattr.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/14 11:47:40 $
 * \version $Revision: 1.19 $
 * \brief   Sets the attributes for an entry.
 *
 * cache_inode_setattr.c : Sets the attributes for an entry.
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
#include "nfs4_acls.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

/**
 *
 * cache_inode_setattrs: set the attributes for an entry located in the cache by its address. 
 * 
 * Sets the attributes for an entry located in the cache by its address. Attributes are provided 
 * with compliance to the underlying FSAL semantics. Attributes that are set are returned in "*pattr".
 *
 * @param pentry_parent [IN] entry for the parent directory to be managed.
 * @param pattr [INOUT] attributes for the entry that we have found. Out: attributes set.
 * @param ht [INOUT] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials 
 * @param pstatus [OUT] returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_setattr(cache_entry_t * pentry, fsal_attrib_list_t * pattr, hash_table_t * ht, /* Unused, kept for protototype's homogeneity */
                                         cache_inode_client_t * pclient,
                                         fsal_op_context_t * pcontext,
                                         cache_inode_status_t * pstatus)
{
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  fsal_attrib_list_t *p_object_attributes = NULL;
  fsal_attrib_list_t result_attributes;
  fsal_attrib_list_t truncate_attributes;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stat */
  pclient->stat.nb_call_total += 1;
  pclient->stat.func_stats.nb_call[CACHE_INODE_SETATTR] += 1;

  /* Lock the entry */
  P_w(&pentry->lock);

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      pfsal_handle = &pentry->object.file.handle;
      break;

    case SYMBOLIC_LINK:
      assert(pentry->object.symlink);
      pfsal_handle = &pentry->object.symlink->handle;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      pfsal_handle = &pentry->object.dir.handle;
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      pfsal_handle = &pentry->object.special_obj.handle;
      break;

    case UNASSIGNED:
    case RECYCLED:
      LogCrit(COMPONENT_CACHE_INODE,
              "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
              pentry->internal_md.type, __LINE__, __FILE__);
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Call FSAL to set the attributes */
  /* result_attributes.asked_attributes = pattr->asked_attributes ; */

  /* mod Th.Leibovici on 2006/02/13
   * We ask back all standard attributes, in case they have been modified
   * by another program (pftp, rcpd...)
   */

  memset(&result_attributes, 0, sizeof(fsal_attrib_list_t));
  result_attributes.asked_attributes = pclient->attrmask;
  /* end of mod */

#ifdef _USE_MFSL
  fsal_status =
      MFSL_setattrs(&pentry->mobject, pcontext, &pclient->mfsl_context, pattr,
                    &result_attributes, NULL);
#else
  fsal_status = FSAL_setattrs(pfsal_handle, pcontext, pattr, &result_attributes);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      V_w(&pentry->lock);

      /* stat */
      pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          LogEvent(COMPONENT_CACHE_INODE,
                   "cache_inode_setattr: Stale FSAL File Handle detected for pentry = %p",
                   pentry);

          if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            LogCrit(COMPONENT_CACHE_INODE,
                    "cache_inode_setattr: Could not kill entry %p, status = %u",
                    pentry, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }

      return *pstatus;
    }

  if(pattr->asked_attributes & FSAL_ATTR_SIZE)
    {
      truncate_attributes.asked_attributes = pclient->attrmask;

      fsal_status = FSAL_truncate(pfsal_handle,
                                  pcontext, pattr->filesize, NULL, &truncate_attributes);
      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          V_w(&pentry->lock);

          /* stat */
          pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_SETATTR] += 1;

          if(fsal_status.major == ERR_FSAL_STALE)
            {
              cache_inode_status_t kill_status;

              LogEvent(COMPONENT_CACHE_INODE,
                       "cache_inode_setattr: Stale FSAL File Handle detected for pentry = %p",
                       pentry);

              if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                 CACHE_INODE_SUCCESS)
                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_setattr: Could not kill entry %p, status = %u",
                        pentry, kill_status);

              *pstatus = CACHE_INODE_FSAL_ESTALE;
            }

          return *pstatus;
        }

    }

  /* Keep the new attribute in cache */
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      p_object_attributes = &(pentry->object.file.attributes);
      break;

    case SYMBOLIC_LINK:
      assert(pentry->object.symlink);
      p_object_attributes = &(pentry->object.symlink->attributes);
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      p_object_attributes = &(pentry->object.dir.attributes);
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      p_object_attributes = &(pentry->object.special_obj.attributes);
      break;

    case UNASSIGNED:
    case RECYCLED:
      LogCrit(COMPONENT_CACHE_INODE,
              "WARNING: unknown source pentry type: internal_md.type=%d, line %d in file %s",
              pentry->internal_md.type, __LINE__, __FILE__);
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Update the cached attributes */
  if((result_attributes.asked_attributes & FSAL_ATTR_SIZE) ||
     (result_attributes.asked_attributes & FSAL_ATTR_SPACEUSED))
    {

      if(pentry->internal_md.type == REGULAR_FILE)
        {
          if(pentry->object.file.pentry_content == NULL)
            {
              /* Operation on a non data cached file */
              p_object_attributes->filesize = result_attributes.filesize;
              p_object_attributes->spaceused = result_attributes.filesize;      /* Unclear hook here. BUGAZOMEU */
            }
          else
            {
              /* Data cached file */
              /* Do not set the p_object_attributes->filesize and p_object_attributes->spaceused  in this case 
               * This will lead to a situation where (for example) untar-ing a file will produced invalid files 
               * with a size of 0 despite the fact that they are not empty */

              LogFullDebug(COMPONENT_CACHE_INODE,
                           "cache_inode_setattr with FSAL_ATTR_SIZE on data cached entry");
            }
        }
      else if(pattr->asked_attributes & FSAL_ATTR_SIZE)
        LogCrit(COMPONENT_CACHE_INODE,
                "WARNING !!! cache_inode_setattr tried to set size on a non REGULAR_FILE type=%d",
                pentry->internal_md.type);
    }

  if(result_attributes.asked_attributes &
     (FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP))
    {
      if(result_attributes.asked_attributes & FSAL_ATTR_MODE)
        p_object_attributes->mode = result_attributes.mode;

      if(result_attributes.asked_attributes & FSAL_ATTR_OWNER)
        p_object_attributes->owner = result_attributes.owner;

      if(result_attributes.asked_attributes & FSAL_ATTR_GROUP)
        p_object_attributes->group = result_attributes.group;
    }

  if(result_attributes.asked_attributes &
     (FSAL_ATTR_ATIME | FSAL_ATTR_CTIME | FSAL_ATTR_MTIME))
    {
      if(result_attributes.asked_attributes & FSAL_ATTR_ATIME)
        p_object_attributes->atime = result_attributes.atime;

      if(result_attributes.asked_attributes & FSAL_ATTR_CTIME)
        p_object_attributes->ctime = result_attributes.ctime;

      if(result_attributes.asked_attributes & FSAL_ATTR_MTIME)
        p_object_attributes->mtime = result_attributes.mtime;
    }
  
#ifdef _USE_NFS4_ACL
  if(result_attributes.asked_attributes & FSAL_ATTR_ACL)
    {
      LogDebug(COMPONENT_CACHE_INODE, "cache_inode_setattr: old acl = %p, new acl = %p",
               p_object_attributes->acl, result_attributes.acl);

      /* Release previous acl entry. */
      if(p_object_attributes->acl)
        {
          fsal_acl_status_t status;
          nfs4_acl_release_entry(p_object_attributes->acl, &status);
          if(status != NFS_V4_ACL_SUCCESS)
            LogEvent(COMPONENT_CACHE_INODE, "cache_inode_setattr: Failed to release old acl:"
                     " status = %d", status);
        }

      /* Update with new acl entry. */
      p_object_attributes->acl = result_attributes.acl;
    }
#endif                          /* _USE_NFS4_ACL */

  /* Return the attributes as set */
  *pattr = *p_object_attributes;

  /* validate the entry */
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_SET, pclient);

  /* Release the entry */
  V_w(&pentry->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_SETATTR] += 1;
  else
    pclient->stat.func_stats.nb_success[CACHE_INODE_SETATTR] += 1;

  return *pstatus;
}                               /* cache_inode_setattr */
