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
 * \file    cache_inode_lookup.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:26 $
 * \version $Revision: 1.33 $
 * \brief   Perform lookup through the cache.
 *
 * cache_inode_lookup.c : Perform lookup through the cache.
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

/**
 *
 * cache_inode_lookup_sw: looks up for a name in a directory indicated by a
 * cached entry.
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory
 * should have been cached before.
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the
 *        cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this
 *        call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs
 * management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * @param use_mutex     [IN]    if TRUE, mutex management is done, not if equal
 * to FALSE.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the
 *  entry
 *
 */
cache_entry_t *cache_inode_lookup_sw(cache_entry_t        * pentry_parent,
                                     fsal_name_t          * pname,
                                     cache_inode_policy_t   policy,
                                     fsal_attrib_list_t   * pattr,
                                     hash_table_t         * ht,
                                     cache_inode_client_t * pclient,
                                     fsal_op_context_t    * pcontext,
                                     cache_inode_status_t * pstatus, 
                                     int use_mutex)
{

  cache_inode_dir_entry_t dirent_key[1], *dirent;
  struct avltree_node *dirent_node;
  cache_inode_dir_entry_t *new_dir_entry;
  cache_entry_t *pentry = NULL;
  fsal_status_t fsal_status;
#ifdef _USE_MFSL
  mfsl_object_t object_handle;
#else
  fsal_handle_t object_handle;
#endif
  fsal_handle_t dir_handle;
  fsal_attrib_list_t object_attributes;
  cache_inode_create_arg_t create_arg;
  cache_inode_file_type_t type;
  cache_inode_status_t cache_status;
  cache_inode_fsal_data_t new_entry_fsdata;
  fsal_accessflags_t access_mask = 0;

  memset( (char *)&new_entry_fsdata, 0, sizeof( new_entry_fsdata ) ) ; 

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  (pclient->stat.nb_call_total)++;
  (pclient->stat.func_stats.nb_call[CACHE_INODE_LOOKUP])++;

  /* We should not renew entries when !use_mutex (because unless we
   * make the flag explicit (shared vs. exclusive), we don't know
   * whether a mutating operation is safe--and, the caller should have
   * already renewed the entry */
  if(use_mutex == TRUE) {
      P_w(&pentry_parent->lock);

      cache_status = cache_inode_renew_entry(pentry_parent, pattr, ht,
                                             pclient, pcontext, pstatus);
      if(cache_status != CACHE_INODE_SUCCESS)
      {
          V_w(&pentry_parent->lock);
          inc_func_err_retryable(pclient, CACHE_INODE_GETATTR);
          LogFullDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_lookup: returning %d(%s) from cache_inode_renew_entry",
                       *pstatus, cache_inode_err_str(*pstatus));
          return NULL;
      }

      /* RW Lock goes for writer to reader */
      rw_lock_downgrade(&pentry_parent->lock);
  }

  if(pentry_parent->internal_md.type != DIRECTORY)
    {
      /* Parent is no directory base, return NULL */
      *pstatus = CACHE_INODE_NOT_A_DIRECTORY;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP])++;

      if(use_mutex == TRUE)
        V_r(&pentry_parent->lock);

      return NULL;
    }

  /* if name is ".", use the input value */
  if(!FSAL_namecmp(pname, (fsal_name_t *) & FSAL_DOT))
    {
      pentry = pentry_parent;
    }
  else if(!FSAL_namecmp(pname, (fsal_name_t *) & FSAL_DOT_DOT))
    {
      /* Directory do only have exactly one parent. This a limitation in all FS,
       * which implies that hard link are forbidden on directories (so that
       * they exists only in one dir).  Because of this, the parent list is
       * always limited to one element for a dir.  Clients SHOULD never 
       * 'lookup( .. )' in something that is no dir. */
      pentry =
          cache_inode_lookupp_no_mutex(pentry_parent, ht, pclient, pcontext,
                                       pstatus);
    }
  else
    {
      /* This is a "regular lookup" (not on "." or "..") */

      /* Check is user (as specified by the credentials) is authorized to 
       * lookup the directory or not */
      access_mask = FSAL_MODE_MASK_SET(FSAL_X_OK) |
                    FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
      if(cache_inode_access_no_mutex(pentry_parent,
                                     access_mask,
                                     ht,
                                     pclient,
                                     pcontext,
                                     pstatus) != CACHE_INODE_SUCCESS)
        {
          if(use_mutex == TRUE)
            V_r(&pentry_parent->lock);

          (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR])++;
          return NULL;
        }

      /* We first try avltree_lookup by name.  If that fails, we dispatch to
       * the fsal. */

      FSAL_namecpy(&dirent_key->name, pname);
      dirent_node = avltree_lookup(&dirent_key->node_n,
				   &pentry_parent->object.dir.dentries);
      if (dirent_node) {
      	  dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
					node_n);
	  pentry = dirent->pentry;
      }

      if(pentry == NULL)
        {
          LogFullDebug(COMPONENT_CACHE_INODE, "Cache Miss detected");

          dir_handle = pentry_parent->object.dir.handle;
          object_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL

#ifdef _USE_MFSL_ASYNC
          if(!mfsl_async_is_object_asynchronous(&pentry_parent->mobject))
            {
              /* If the parent is asynchronous, rely on the content of the cache
               * inode parent entry.
               *
               * /!\ If the fs behind the FSAL is touched in a non-nfs way,
               * there will be huge incoherencies.
               */
#endif                          /* _USE_MFSL_ASYNC */

              fsal_status = MFSL_lookup(&pentry_parent->mobject,
                                        pname,
                                        pcontext,
                                        &pclient->mfsl_context,
                                        &object_handle, &object_attributes, NULL);
#ifdef _USE_MFSL_ASYNC
            }
          else
            {
              LogDebug(COMPONENT_CACHE_INODE,
                       "cache_inode_lookup chose to bypass FSAL and trusted his cache for name=%s",
                       pname->name);
              fsal_status.major = ERR_FSAL_NOENT;
              fsal_status.minor = ENOENT;
            }
#endif                          /* _USE_MFSL_ASYNC */

#else
          fsal_status =
              FSAL_lookup(&dir_handle, pname, pcontext, &object_handle,
                          &object_attributes);
#endif                          /* _USE_MFSL */

          if(FSAL_IS_ERROR(fsal_status))
            {
              *pstatus = cache_inode_error_convert(fsal_status);

              if(use_mutex == TRUE)
                V_r(&pentry_parent->lock);

              /* Stale File Handle to be detected and managed */
              if(fsal_status.major == ERR_FSAL_STALE)
                {
                  cache_inode_status_t kill_status;

                  LogEvent(COMPONENT_CACHE_INODE,
                           "cache_inode_lookup: Stale FSAL File Handle detected for pentry = %p",
                           pentry_parent);

                  if(cache_inode_kill_entry(pentry_parent, NO_LOCK, ht, pclient, &kill_status) !=
                     CACHE_INODE_SUCCESS)
                    LogCrit(COMPONENT_CACHE_INODE,
                            "cache_inode_pentry_parent: Could not kill entry %p, status = %u",
                            pentry_parent, kill_status);

                  *pstatus = CACHE_INODE_FSAL_ESTALE;
                }

              /* stats */
              (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP])++;

              return NULL;
            }

          type = cache_inode_fsal_type_convert(object_attributes.type);

          /* If entry is a symlink, this value for be cached */
          if(type == SYMBOLIC_LINK)
            {
              if( CACHE_INODE_KEEP_CONTENT( policy ) )
#ifdef _USE_MFSL
               {
                fsal_status =
                    MFSL_readlink(&object_handle, pcontext, &pclient->mfsl_context,
                                  &create_arg.link_content, &object_attributes, NULL);
               }
#else
               {
                fsal_status =
                    FSAL_readlink(&object_handle, pcontext, &create_arg.link_content,
                                  &object_attributes);
               }
             else
              { 
                 fsal_status.major = ERR_FSAL_NO_ERROR ;
                 fsal_status.minor = 0 ;
              }
#endif
              if(FSAL_IS_ERROR(fsal_status))
                {
                  *pstatus = cache_inode_error_convert(fsal_status);
                  if(use_mutex == TRUE)
                    V_r(&pentry_parent->lock);

                  /* Stale File Handle to be detected and managed */
                  if(fsal_status.major == ERR_FSAL_STALE)
                    {
                      cache_inode_status_t kill_status;

                      LogEvent(COMPONENT_CACHE_INODE,
                               "cache_inode_lookup: Stale FSAL File Handle detected for pentry = %p",
                               pentry_parent);

                      if(cache_inode_kill_entry(pentry_parent, NO_LOCK, ht, pclient, &kill_status)
                         != CACHE_INODE_SUCCESS)
                        LogCrit(COMPONENT_CACHE_INODE,
                                "cache_inode_pentry_parent: Could not kill entry %p, status = %u",
                                pentry_parent, kill_status);

                      *pstatus = CACHE_INODE_FSAL_ESTALE;
                    }

                  /* stats */
                  (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP])++;

                  return NULL;
                }
            }

          /* Allocation of a new entry in the cache */
#ifdef _USE_MFSL
          new_entry_fsdata.handle = object_handle.handle;
#else
          new_entry_fsdata.handle = object_handle;
#endif
          new_entry_fsdata.cookie = 0;

          if((pentry = cache_inode_new_entry( &new_entry_fsdata, 
                                              &object_attributes,
                                              type, 
                                              policy,
                                              &create_arg, 
                                              NULL, 
                                              ht, 
                                              pclient, 
                                              pcontext, 
                                              FALSE,      /* This is a population and not a creation */
                                              pstatus ) ) == NULL )
            {
              if(use_mutex == TRUE)
                V_r(&pentry_parent->lock);

              /* stats */
              (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP])++;

              return NULL;
            }

          /* Entry was found in the FSAL, add this entry to the parent
           * directory */
          cache_status = cache_inode_add_cached_dirent(pentry_parent,
                                                       pname,
                                                       pentry,
                                                       ht,
						       &new_dir_entry,
						       pclient,
						       pcontext,
						       pstatus);

          if(cache_status != CACHE_INODE_SUCCESS
             && cache_status != CACHE_INODE_ENTRY_EXISTS)
            {
              if(use_mutex == TRUE)
                V_r(&pentry_parent->lock);

              /* stats */
              (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_LOOKUP])++;

              return NULL;
            }

        } /* cached lookup fail (try fsal) */
    }

  /* Return the attributes */
  cache_inode_get_attributes(pentry, pattr);

  *pstatus = cache_inode_valid(pentry_parent, CACHE_INODE_OP_GET, pclient);

  if(use_mutex == TRUE)
    V_r(&pentry_parent->lock);

  /* stat */
  if(*pstatus != CACHE_INODE_SUCCESS)
    (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_LOOKUP])++;
  else
    (pclient->stat.func_stats.nb_success[CACHE_INODE_LOOKUP])++;

  return pentry;
}                               /* cache_inode_lookup_sw */

/**
 *
 * cache_inode_lookup_no_mutex: looks up for a name in a directory indicated by a cached entry (no mutex management).
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 * This function has no mutex management and suppose that is it properly done in the clling function
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookup_no_mutex(cache_entry_t        * pentry_parent,
                                           fsal_name_t          * pname,
                                           cache_inode_policy_t   policy,
                                           fsal_attrib_list_t   * pattr,
                                           hash_table_t         * ht,
                                           cache_inode_client_t * pclient,
                                           fsal_op_context_t    * pcontext,
                                           cache_inode_status_t * pstatus)
{
  return cache_inode_lookup_sw( pentry_parent,
                                pname, 
                                policy,
                                pattr, 
                                ht, 
                                pclient, 
                                pcontext,  
                                pstatus, 
                                FALSE);
}                               /* cache_inode_lookup_no_mutex */

/**
 *
 * cache_inode_lookup: looks up for a name in a directory indicated by a cached entry.
 * 
 * Looks up for a name in a directory indicated by a cached entry. The directory should have been cached before.
 *
 * @param pentry_parent [IN]    entry for the parent directory to be managed.
 * @param name          [IN]    name of the entry that we are looking for in the cache.
 * @param pattr         [OUT]   attributes for the entry that we have found.
 * @param ht            [IN]    hash table used for the cache, unused in this call.
 * @param pclient       [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext         [IN]    FSAL credentials 
 * @param pstatus       [OUT]   returned status.
 * 
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_entry_t *cache_inode_lookup(cache_entry_t * pentry_parent,
                                  fsal_name_t * pname,
                                  cache_inode_policy_t policy,
                                  fsal_attrib_list_t * pattr,
                                  hash_table_t * ht,
                                  cache_inode_client_t * pclient,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_status_t * pstatus)
{
  return cache_inode_lookup_sw( pentry_parent,
                                pname, 
                                policy,
                                pattr, 
                                ht, 
                                pclient, 
                                pcontext, 
                                pstatus, 
                                TRUE);
}                               /* cache_inode_lookup */
