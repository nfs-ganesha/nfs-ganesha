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
 * \file    cache_inode_readdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:05 $
 * \version $Revision: 1.50 $
 * \brief   Reads the content of a directory.
 *
 * cache_inode_readdir.c : Reads the content of a directory. Contains also the needed function for directory browsing.
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
#include "stuff_alloc.h"
#include "fsal.h"
#include "cache_inode.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_readdir_nonamecache: Reads a directory without populating the name cache (no dirents created).
 *
 * Reads a directory without populating the name cache (no dirents created).
 *
 * @param pentry [IN] entry for the parent directory to be read.
 * @param cookie [IN] cookie for the readdir operation (basically the offset).
 * @param nbwanted [IN] Maximum number of directory entries wanted.
 * @param peod_met [OUT] A flag to know if end of directory was met during this call.
 * @param dirent_array [OUT] the resulting array of found directory entries.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param unlock [OUT] the caller shall release read-lock on pentry when done
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 *
 */
static cache_inode_status_t cache_inode_readdir_nonamecache( cache_entry_t * pentry_dir,
                                                             cache_inode_policy_t policy,
                                                             uint64_t cookie,
                                                             unsigned int nbwanted,
                                                             unsigned int *pnbfound,
                                                             uint64_t *pend_cookie,
                                                             cache_inode_endofdir_t *peod_met,
                                                             cache_inode_dir_entry_t **dirent_array,
                                                             hash_table_t *ht,
                                                             int *unlock,
                                                             cache_inode_client_t *pclient,
                                                             fsal_op_context_t *pcontext,
                                                             cache_inode_status_t *pstatus)
{
  fsal_dir_t fsal_dirhandle;
  fsal_status_t fsal_status;
  fsal_attrib_list_t dir_attributes;

  fsal_cookie_t begin_cookie;
  fsal_cookie_t end_cookie;
  fsal_count_t iter;
  fsal_boolean_t fsal_eod;
  fsal_dirent_t fsal_dirent_array[FSAL_READDIR_SIZE + 20];

  cache_inode_fsal_data_t entry_fsdata;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only DIRECTORY entries are concerned */
  if(pentry_dir->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  LogFullDebug(COMPONENT_NFS_READDIR,
               "About to readdir in  cache_inode_readdir_nonamecache: pentry=%p "
	       "cookie=%"PRIu64, pentry_dir, cookie ) ;

  /* Open the directory */
  dir_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
  fsal_status = MFSL_opendir(&pentry_dir->mobject,
                             pcontext,
                             &pclient->mfsl_context, &fsal_dirhandle, &dir_attributes, NULL);
#else
  fsal_status = FSAL_opendir(&pentry_dir->object.dir.handle,
                             pcontext, &fsal_dirhandle, &dir_attributes);
#endif

  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          LogEvent(COMPONENT_CACHE_INODE,
                   "cache_inode_readdir: Stale FSAL File Handle detected for pentry = %p",
                   pentry_dir);

          if(cache_inode_kill_entry(pentry_dir, WT_LOCK, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            LogCrit(COMPONENT_CACHE_INODE,
                    "cache_inode_readdir: Could not kill entry %p, status = %u",
                    pentry_dir, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }
      return *pstatus;
    }

  /* Loop for readding the directory */
  // memcpy( &(begin_cookie.data), &cookie, sizeof( uint64_t ) ) ;
  FSAL_SET_COOKIE_BY_OFFSET( begin_cookie, cookie ) ;
  fsal_eod = FALSE;

#ifdef _USE_MFSL
  fsal_status = MFSL_readdir( &fsal_dirhandle,
                              begin_cookie,
                              pclient->attrmask,
                              nbwanted * sizeof(fsal_dirent_t),
                              fsal_dirent_array,
                              &end_cookie,
                              (fsal_count_t *)pnbfound, 
                              &fsal_eod, 
                              &pclient->mfsl_context, 
                              NULL);
#else
  fsal_status = FSAL_readdir( &fsal_dirhandle,
                              begin_cookie,
                              pclient->attrmask,
                              nbwanted * sizeof(fsal_dirent_t),
                              fsal_dirent_array,
                              &end_cookie, 
                              (fsal_count_t *)pnbfound, 
                              &fsal_eod);
#endif
  if(FSAL_IS_ERROR(fsal_status))
   {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
   }

  for( iter = 0 ; iter < *pnbfound ; iter ++ )
   {
      /* cache_inode_readdir does not return . or .. */
      if(!FSAL_namecmp(&(fsal_dirent_array[iter].name), (fsal_name_t *) & FSAL_DOT) ||
         !FSAL_namecmp(&(fsal_dirent_array[iter].name), (fsal_name_t *) & FSAL_DOT_DOT))
              continue;

      /* Get the related pentry without populating the name cache (but eventually populating the attrs cache */
      entry_fsdata.handle = fsal_dirent_array[iter].handle;
      entry_fsdata.cookie = 0; /* XXX needed? */

      /* Allocate a dirent to be returned to the client */
      /** @todo Make sure this piece of memory once the data are used */
      GetFromPool( dirent_array[iter], &pclient->pool_dir_entry, cache_inode_dir_entry_t);
      if( dirent_array[iter] == NULL ) 
       {
         *pstatus = CACHE_INODE_MALLOC_ERROR;
         return *pstatus;
       }


      /* fills in the dirent_array */
      if( ( dirent_array[iter]->pentry= cache_inode_get( &entry_fsdata,
                                                         policy,
                                                         &fsal_dirent_array[iter].attributes,
                                                         ht,
                                                         pclient,
                                                         pcontext,
                                                         pstatus ) ) == NULL )
          return *pstatus ;

      fsal_status = FSAL_namecpy( &dirent_array[iter]->name, &fsal_dirent_array[iter].name ) ;
      if(FSAL_IS_ERROR(fsal_status))
       {
         *pstatus = cache_inode_error_convert(fsal_status);
         return *pstatus;
       }
  
      (void) FSAL_cookie_to_uint64( &fsal_dirent_array[iter].handle,
                                    pcontext, 
                                    &fsal_dirent_array[iter].cookie,
                                    &dirent_array[iter]->fsal_cookie);

       dirent_array[iter]->cookie = dirent_array[iter]->fsal_cookie ;

   } /* for( iter = 0 ; iter < nbfound ; iter ++ ) */

  if( fsal_eod == TRUE )
    *peod_met = END_OF_DIR ;
  else
    *peod_met = TO_BE_CONTINUED ;

  /* Do not forget to set returned end cookie */
  //memcpy( pend_cookie, &(end_cookie.data), sizeof( uint64_t ) ) ; 
  FSAL_SET_POFFSET_BY_COOKIE( end_cookie, pend_cookie ) ;

  LogFullDebug(COMPONENT_NFS_READDIR,
               "End of readdir in  cache_inode_readdir_nonamecache: pentry=%p "
	       "cookie=%"PRIu64, pentry_dir, *pend_cookie ) ;


  /* Close the directory */
#ifdef _USE_MFSL
  fsal_status = MFSL_closedir(&fsal_dirhandle, &pclient->mfsl_context, NULL);
#else
  fsal_status = FSAL_closedir(&fsal_dirhandle);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
    }

  return CACHE_INODE_SUCCESS ;
} /* cache_inode_readdir_nomanecache */


/**
 *
 * cache_inode_release_dirent: releases dirents allocated by cache_inode_readdir_nonamecache.
 *
 * Releases dirents allocated by cache_inode_readdir_nonamecache. This is to be called only if the 
 * related entry as a policy that prevents name to be cached.
 *
 * @param dirent_array [INOUT] array of pointers of dirents to be released
 * @param howmuch [IN] size of dirent_array
 * @param pclient [INOUT] resource allocated by the client for the nfs management.
 *
 * @ return nothing (void function)
 *
 */
void cache_inode_release_dirent(  cache_inode_dir_entry_t ** dirent_array,
                                  unsigned int howmuch,
                                  cache_inode_client_t * pclient )
{
  unsigned int i = 0 ;

  for( i = 0 ; i < howmuch ; i++ )
   {
      if( dirent_array[i] != NULL )
        ReleaseToPool( dirent_array[i],  &pclient->pool_dir_entry ) ;
      else
        break ;
   }

} /* cache_inode_release_dirent */

/**
 *
 * cache_inode_operate_cached_dirent: locates a dirent in the cached dirent,
 * and perform an operation on it.
 *
 * Looks up for an dirent in the cached dirent. Thus function searches only in
 * the entries listed in the dir_entries array. Some entries may be missing but
 * existing and not be cached (if no readdir was ever performed on the entry for
 * example. This function provides a way to operate on the dirent.
 *
 * @param pentry_parent [IN] directory entry to be searched.
 * @param name [IN] name for the searched entry.
 * @param newname [IN] newname if function is used to rename a dirent
 * @param pclient [INOUT] resource allocated by the client for the nfs management.
 * @param dirent_op [IN] operation (ADD, LOOKUP or REMOVE) to do on the dirent
 *        if found.
 * @pstatus [OUT] returned status.
 *
 * @return the found entry if its exists and NULL if it is not in the dirent
 *         cache. REMOVE always returns NULL.
 *
 */
cache_entry_t *cache_inode_operate_cached_dirent(cache_entry_t * pentry_parent,
                                                 fsal_name_t * pname,
                                                 fsal_name_t * newname,
						 cache_inode_client_t * pclient,
                                                 cache_inode_dirent_op_t dirent_op,
                                                 cache_inode_status_t * pstatus)
{
  cache_entry_t *pentry = NULL;
  cache_inode_dir_entry_t dirent_key[1], *dirent;
  struct avltree_node *dirent_node, *tmpnode;
  LRU_List_state_t vstate;

  /* Directory mutation generally invalidates outstanding 
   * readdirs, hence any cached cookies, so in these cases we 
   * clear the cookie avl */

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return NULL;
    }

  /* If no active entry, do nothing */
  if (pentry_parent->object.dir.nbactive == 0) {
      *pstatus = CACHE_INODE_NOT_FOUND;
      return NULL;
  }

  FSAL_namecpy(&dirent_key->name, pname);
  dirent_node = avltree_lookup(&dirent_key->node_n,
			       &pentry_parent->object.dir.dentries);
  if (! dirent_node) {
      *pstatus = CACHE_INODE_NOT_FOUND; /* Right error code (see above)? */
      return NULL;
  }

  /* unpack avl node */
  dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
				node_n);

  /* check state of cached dirent */
  vstate = dirent->pentry->internal_md.valid_state;
  if (vstate == VALID || vstate == STALE) {
  
      if (vstate == STALE)
      	LogDebug(COMPONENT_NFS_READDIR,
		"DIRECTORY: found STALE cache entry");

	/* Entry was found */
        pentry = dirent->pentry;
        *pstatus = CACHE_INODE_SUCCESS;
  }

  /* Did we find something */
  if(pentry != NULL)
    {
      /* Yes, we did ! */
      switch (dirent_op)
        {
        case CACHE_INODE_DIRENT_OP_REMOVE:
	    avltree_remove(&dirent->node_n,
                           &pentry_parent->object.dir.dentries);
	    /* release to pool */
	    ReleaseToPool(dirent, &pclient->pool_dir_entry);
	    pentry_parent->object.dir.nbactive--;
	    *pstatus = CACHE_INODE_SUCCESS;
          break;

        case CACHE_INODE_DIRENT_OP_RENAME:
	  /* change the installed inode only the rename can succeed */
	  FSAL_namecpy(&dirent_key->name, newname);
  	  tmpnode = avltree_lookup(&dirent_key->node_n,
				   &pentry_parent->object.dir.dentries);
	  if (tmpnode) {
	    /* rename would cause a collision */
	    *pstatus = CACHE_INODE_ENTRY_EXISTS;

	  } else {
	      /* remove, rename, and re-insert the object with new keys */
	      avltree_remove(&dirent->node_n,
                             &pentry_parent->object.dir.dentries);

	      FSAL_namecpy(&dirent->name, newname);
	      tmpnode = avltree_insert(&dirent->node_n,
				       &pentry_parent->object.dir.dentries);
	      if (tmpnode) {
		  /* collision, tree state unchanged--this won't happen */
		  *pstatus = CACHE_INODE_ENTRY_EXISTS;

		  /* still, try to revert the change in place */
		  FSAL_namecpy(&dirent->name, pname);
		  tmpnode = avltree_insert(&dirent->node_n,
					   &pentry_parent->object.dir.dentries);
	      } else {
		  *pstatus = CACHE_INODE_SUCCESS;
	      }
	  } /* !found */
          break;

        default:
          /* Should never occurs, in any case, it cost nothing to handle
	   * this situation */
          *pstatus = CACHE_INODE_INVALID_ARGUMENT;
          break;

        }                       /* switch */
    }

  if (*pstatus == CACHE_INODE_SUCCESS) {
      /* As noted, if a mutating operation was performed, we must
       * invalidate cached cookies. */
      cache_inode_release_dirents(
          pentry_parent, pclient, CACHE_INODE_AVL_COOKIES);

      /* Someone has to repopulate the avl cookie cache.  Populating it
       * lazily is ok, but the logic to do it makes supporting simultaneous
       * readers more involved.  Another approach would be to do it in the
       * background, scheduled from here. */
  }

  return pentry;
}                               /* cache_inode_operate_cached_dirent */

#ifdef _TOTO
/**
 *
 * cache_inode_lookup_cached_dirent: looks up for an dirent in the cached
 * dirent.
 *
 * Looks up for an dirent in the cached dirent. Thus function searches only in
 * the entries listed in the dir_entries array. Some entries may be missing but
 * existing and not be cached (if no readdir was ever performed on the entry for
 * example.
 *
 * @param pentry_parent [IN] directory entry to be looked.
 * @param name [IN] name for the searched entry.
 * @param pclient [INOUT] resource allocated by the client for the nfs
 *        management.
 * @pstatus [OUT] returned status. CACHE_INODE_SUCCESS=entry found,
 *          CACHE_INODE_NOT_FOUND=entry is not in the dirent
 *
 * @return the found entry if its exists and NULL if it is not in the dirent
 * cache.
 *
 */
cache_entry_t *cache_inode_lookup_cached_dirent(cache_entry_t * pentry_parent,
                                                fsal_name_t * pname,
                                                cache_inode_client_t * pclient,
                                                cache_inode_status_t * pstatus)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return NULL;
    }
  return cache_inode_operate_cached_dirent(pentry_parent, *pname, NULL,
					   pclient,
                                           CACHE_INODE_DIRENT_OP_LOOKUP,
					   pstatus);
}                               /* cache_inode_lookup_cached_dirent */
#endif

/**
 *
 * cache_inode_add_cached_dirent: Adds a directory entry to a cached directory.
 *
 * Adds a directory entry to a cached directory. This is use when creating a
 * new entry through nfs and keep it to the cache. It also allocates and caches
 * the entry.  This function can be call iteratively, within a loop (like what
 * is done in cache_inode_readdir_populate).  In this case, pentry_parent should
 * be set to the value returned in *pentry_next.  This function should never be
 * used for managing a junction.
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be
 *                              managed.
 * @param name          [IN]    name of the entry to add.
 * @param pentry_added  [IN]    the pentry added to the dirent array
 * @param pentry_next   [OUT]   the next pentry to use for next call.
 * @param ht            [IN]    hash table used for the cache, unused in this
 *                              call.
 * @param pclient       [INOUT] resource allocated by the client for the nfs
 *                              management.
 * @param pstatus       [OUT]   returned status.
 *
 * @return the DIRECTORY that contain this entry in its array_dirent\n
 * @return NULL if failed, see *pstatus for error's meaning.
 *
 */
cache_inode_status_t cache_inode_add_cached_dirent(
    cache_entry_t * pentry_parent,
    fsal_name_t * pname,
    cache_entry_t * pentry_added,
    hash_table_t * ht,
    cache_inode_dir_entry_t **pnew_dir_entry,
    cache_inode_client_t * pclient,
    fsal_op_context_t * pcontext,
    cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;
  cache_inode_parent_entry_t *next_parent_entry = NULL;
  cache_inode_dir_entry_t *new_dir_entry = NULL;
  struct avltree_node *tmpnode;

  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIRECTORY)
    {
 
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }
    
  /* in cache inode avl, we always insert on pentry_parent */
  GetFromPool(new_dir_entry, &pclient->pool_dir_entry, cache_inode_dir_entry_t);

  if(new_dir_entry == NULL)
    {
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }

  fsal_status = FSAL_namecpy(&new_dir_entry->name, pname);
  if(FSAL_IS_ERROR(fsal_status))
  {
    *pstatus = CACHE_INODE_FSAL_ERROR;
    return *pstatus;
  }

  /* still need the parent list */
  GetFromPool(next_parent_entry, &pclient->pool_parent,
              cache_inode_parent_entry_t);
  if(next_parent_entry == NULL)
    {
      *pstatus = CACHE_INODE_MALLOC_ERROR;
      return *pstatus;
    }

  /* Init the next_parent_entry variable */
  next_parent_entry->parent = NULL;
  next_parent_entry->next_parent = NULL;

  /* add to avl */
  tmpnode = avltree_insert(&new_dir_entry->node_n,
			   &pentry_parent->object.dir.dentries);
  if (tmpnode) {
  	/* collision, tree not updated--release both pool objects and return
         * err */
	ReleaseToPool(next_parent_entry, &pclient->pool_parent);
	ReleaseToPool(new_dir_entry, &pclient->pool_dir_entry);
	*pstatus = CACHE_INODE_ENTRY_EXISTS;
	return *pstatus;
  }

  *pnew_dir_entry = new_dir_entry;

  /* we're going to succeed */
  pentry_parent->object.dir.nbactive++;  
  new_dir_entry->pentry = pentry_added;

  /* link with the parent entry (insert as first entry) */
  next_parent_entry->parent = pentry_parent;
  next_parent_entry->next_parent = pentry_added->parent_list;
  pentry_added->parent_list = next_parent_entry;

  return *pstatus;
}                               /* cache_inode_add_cached_dirent */

/*
 * cache_inode_invalidate_all_cached_dirent: Invalidates all the entries for a
 * cached directory.
 *
 * Invalidates all the entries for a cached directory.  No MT Safety managed
 * here !!
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be
 * managed.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs
 * management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_invalidate_all_cached_dirent(
    cache_entry_t *pentry,
    hash_table_t *ht,
    cache_inode_client_t *pclient,
    cache_inode_status_t *pstatus)
{
  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only DIRECTORY entries are concerned */
  if(pentry->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* Get ride of entries cached in the DIRECTORY */
  cache_inode_release_dirents(pentry, pclient, CACHE_INODE_AVL_BOTH);

  /* Reinit the fields */
  pentry->object.dir.has_been_readdir = CACHE_INODE_NO;
  *pstatus = CACHE_INODE_SUCCESS;

  return *pstatus;
}                               /* cache_inode_invalidate_all_cached_dirent */

/**
 *
 * cache_inode_remove_cached_dirent: Removes a directory entry to a cached
 * directory.
 *
 * Removes a directory entry to a cached directory. No MT safety managed here !!
 *
 * @param pentry_parent [INOUT] cache entry representing the directory to be
 * managed.
 * @param name [IN] name of the entry to remove.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs
 * management.
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_inode_status_t cache_inode_remove_cached_dirent(
    cache_entry_t * pentry_parent,
    fsal_name_t * pname,
    hash_table_t * ht,
    cache_inode_client_t * pclient,
    cache_inode_status_t * pstatus)
{
  cache_entry_t *removed_pentry = NULL;
  cache_inode_parent_entry_t *parent_iter = NULL;
  cache_inode_parent_entry_t *previous_iter = NULL;
  int found = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Sanity check */
  if(pentry_parent->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }

  /* BUGAZOMEU: Ne pas oublier de jarter un dir dont toutes les entrees sont
   * inactives */
  if((removed_pentry = cache_inode_operate_cached_dirent(pentry_parent,
                                                         pname,
                                                         NULL,
							 pclient,
                                                         CACHE_INODE_DIRENT_OP_REMOVE,
                                                         pstatus)) == NULL)
    return *pstatus;

  /* Remove the parent entry from the entry whose dirent is removed */
  for(previous_iter = NULL, parent_iter = removed_pentry->parent_list;
      (parent_iter != NULL) && (parent_iter->parent != NULL);
      previous_iter = parent_iter, parent_iter = parent_iter->next_parent)
    {
      if(parent_iter->parent == pentry_parent)
        {
          found = 1;
          break;
        }
    }

  /* Check for pentry cache inconsistency */
  if(!found)
    {
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
    }
  else
    {
      if(previous_iter == NULL)
        {
          /* this is the first parent */
          removed_pentry->parent_list = parent_iter->next_parent;
        }
      else
        {
          /* This is not the first parent */
          previous_iter->next_parent = parent_iter->next_parent;
        }

      /* It is now time to put parent_iter back to its pool */
      ReleaseToPool(parent_iter, &pclient->pool_parent);

    }
  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_remove_cached_dirent */


static void debug_print_dirents(cache_entry_t *dir_pentry)
{
    struct avltree_node *dirent_node;
    cache_inode_dir_entry_t *dirent;
    int size, ix;

    size = avltree_size(&dir_pentry->object.dir.cookies);

    LogCrit(COMPONENT_CACHE_INODE,
            "cookie avl size: %d",
            size);

    dirent_node = avltree_first(&dir_pentry->object.dir.cookies);

    ix = 0;
    while (dirent_node) {
        dirent = avltree_container_of(dirent_node,
                                      cache_inode_dir_entry_t, 
                                      node_c);

        LogCrit(COMPONENT_CACHE_INODE,
                "cookie: ix %d %p (%s, %"PRIu64")",
                ix,
                dirent,
                dirent->name.name,
                dirent->cookie);

        dirent_node = avltree_next(dirent_node);
        ++ix;
    }

    size = avltree_size(&dir_pentry->object.dir.dentries);

    LogCrit(COMPONENT_CACHE_INODE,
            "name avl size: %d",
            size);

    dirent_node = avltree_first(&dir_pentry->object.dir.dentries);

    ix = 0;
    while (dirent_node) {
        dirent = avltree_container_of(dirent_node,
                                      cache_inode_dir_entry_t,
                                      node_n);

        LogCrit(COMPONENT_CACHE_INODE,
                "name: ix %d %p (%s, %"PRIu64")",
                ix,
                dirent,
                dirent->name.name,
                dirent->cookie);

        dirent_node = avltree_next(dirent_node);
        ++ix;
    }
}

/**
 *
 * cache_inode_readdir_populate: fully reads a directory in FSAL and caches
 * the related entries.
 *
 * fully reads a directory in FSAL and caches the related entries. No MT
 * safety managed here !!
 *
 * @param pentry [IN]  entry for the parent directory to be read. This must be
 * a DIRECTORY
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param pclient [INOUT] ressource allocated by the client for the nfs
 * management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 */
cache_inode_status_t cache_inode_readdir_populate(
    cache_entry_t * pentry_dir,
    cache_inode_policy_t policy,
    hash_table_t * ht,
    cache_inode_client_t * pclient,
    fsal_op_context_t * pcontext,
    cache_inode_status_t * pstatus)
{
  fsal_dir_t fsal_dirhandle;
  fsal_status_t fsal_status;
  fsal_attrib_list_t dir_attributes;

  fsal_cookie_t begin_cookie;
  fsal_cookie_t end_cookie;
  fsal_count_t nbfound;
  fsal_count_t iter;
  fsal_boolean_t fsal_eod;

  cache_entry_t *pentry = NULL;
  cache_entry_t *pentry_parent = pentry_dir;
  fsal_attrib_list_t object_attributes;

  cache_inode_create_arg_t create_arg;
  cache_inode_file_type_t type;
  cache_inode_status_t cache_status;
  fsal_dirent_t array_dirent[FSAL_READDIR_SIZE + 20];
  cache_inode_fsal_data_t new_entry_fsdata;
  cache_inode_dir_entry_t *new_dir_entry = NULL;
  uint64_t i = 0;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* Only DIRECTORY entries are concerned */
  if(pentry_dir->internal_md.type != DIRECTORY)
    {
      *pstatus = CACHE_INODE_BAD_TYPE;
      return *pstatus;
    }
#ifdef _USE_MFSL_ASYNC
  /* If entry is asynchronous (via MFSL), it should not be repopulated until
     it is synced */
  if(MFSL_ASYNC_is_synced(&pentry_dir->mobject) == FALSE)
    {
      /* Directory is asynchronous, do not repopulate it and let it
       * in the state 'has_been_readdir == FALSE' */
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }
#endif

  /* If directory is already populated , there is no job to do */
  if(pentry_dir->object.dir.has_been_readdir == CACHE_INODE_YES)
    {
      *pstatus = CACHE_INODE_SUCCESS;
      return *pstatus;
    }

  /* Invalidate all the dirents */
  if(cache_inode_invalidate_all_cached_dirent(pentry_dir,
                                              ht,
                                              pclient,
					      pstatus) != CACHE_INODE_SUCCESS)
    return *pstatus;

  /* Open the directory */
  dir_attributes.asked_attributes = pclient->attrmask;
#ifdef _USE_MFSL
  fsal_status = MFSL_opendir(&pentry_dir->mobject,
                             pcontext,
                             &pclient->mfsl_context, &fsal_dirhandle, &dir_attributes, NULL);
#else
  fsal_status = FSAL_opendir(&pentry_dir->object.dir.handle,
                             pcontext, &fsal_dirhandle, &dir_attributes);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);

      if(fsal_status.major == ERR_FSAL_STALE)
        {
          cache_inode_status_t kill_status;

          LogEvent(COMPONENT_CACHE_INODE,
                   "cache_inode_readdir: Stale FSAL File Handle detected for pentry = %p",
                   pentry_dir);

          if(cache_inode_kill_entry(pentry_dir, WT_LOCK, ht, pclient, &kill_status) !=
             CACHE_INODE_SUCCESS)
            LogCrit(COMPONENT_CACHE_INODE,
                    "cache_inode_readdir: Could not kill entry %p, status = %u",
                    pentry_dir, kill_status);

          *pstatus = CACHE_INODE_FSAL_ESTALE;
        }
      return *pstatus;
    }

  /* Loop for readding the directory */
  FSAL_SET_COOKIE_BEGINNING(begin_cookie);
  FSAL_SET_COOKIE_BEGINNING(end_cookie);
  fsal_eod = FALSE;

  do
    {
#ifdef _USE_MFSL
      fsal_status = MFSL_readdir(&fsal_dirhandle,
                                 begin_cookie,
                                 pclient->attrmask,
                                 FSAL_READDIR_SIZE * sizeof(fsal_dirent_t),
                                 array_dirent,
                                 &end_cookie,
                                 &nbfound, &fsal_eod, &pclient->mfsl_context, NULL);
#else
      fsal_status = FSAL_readdir(&fsal_dirhandle,
                                 begin_cookie,
                                 pclient->attrmask,
                                 FSAL_READDIR_SIZE * sizeof(fsal_dirent_t),
                                 array_dirent, &end_cookie, &nbfound, &fsal_eod);
#endif

      if(FSAL_IS_ERROR(fsal_status))
        {
          *pstatus = cache_inode_error_convert(fsal_status);
          return *pstatus;
        }

      for(iter = 0; iter < nbfound; iter++)
        {
          LogFullDebug(COMPONENT_NFS_READDIR,
                       "cache readdir populate found entry %s",
                       array_dirent[iter].name.name);

          /* It is not needed to cache '.' and '..' */
          if(!FSAL_namecmp(&(array_dirent[iter].name), (fsal_name_t *) & FSAL_DOT) ||
             !FSAL_namecmp(&(array_dirent[iter].name), (fsal_name_t *) & FSAL_DOT_DOT))
            {
              LogFullDebug(COMPONENT_NFS_READDIR,
                           "cache readdir populate : do not cache . and ..");
              continue;
            }

          /* If dir entry is a symbolic link, its content has to be read */
          if((type =
              cache_inode_fsal_type_convert(array_dirent[iter].attributes.type)) ==
             SYMBOLIC_LINK)
            {
#ifdef _USE_MFSL
              mfsl_object_t tmp_mfsl;
#endif
              /* Let's read the link for caching its value */
              object_attributes.asked_attributes = pclient->attrmask;
              if( CACHE_INODE_KEEP_CONTENT( pentry_dir->policy ) )
                {
#ifdef _USE_MFSL
                  tmp_mfsl.handle = array_dirent[iter].handle;
                  fsal_status = MFSL_readlink(&tmp_mfsl,
                                              pcontext,
                                              &pclient->mfsl_context,
                                              &create_arg.link_content, &object_attributes, NULL);
#else
                  fsal_status = FSAL_readlink(&array_dirent[iter].handle,
                                              pcontext,
                                              &create_arg.link_content, &object_attributes);
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

                  if(fsal_status.major == ERR_FSAL_STALE)
                    {
                      cache_inode_status_t kill_status;

                      LogEvent(COMPONENT_CACHE_INODE,
                               "cache_inode_readdir: Stale FSAL File Handle detected for pentry = %p",
                               pentry_dir);

                      if(cache_inode_kill_entry(pentry_dir, WT_LOCK, ht, pclient, &kill_status) !=
                         CACHE_INODE_SUCCESS)
                        LogCrit(COMPONENT_CACHE_INODE,
                                "cache_inode_readdir: Could not kill entry %p, status = %u",
                                pentry_dir, kill_status);

                      *pstatus = CACHE_INODE_FSAL_ESTALE;
                    }

                  return *pstatus;
                }
            }

          /* Try adding the entry, if it exists then this existing entry is
             returned */
          new_entry_fsdata.handle = array_dirent[iter].handle;
	  new_entry_fsdata.cookie = 0; /* XXX needed? */

          if((pentry = cache_inode_new_entry( &new_entry_fsdata,
		                              &array_dirent[iter].attributes,
		                              type, 
                                              policy,
		                              &create_arg,
		                              NULL, 
		                              ht, 
		                              pclient, 
		                              pcontext, 
		                              FALSE,  /* This is population and no creation */
		                              pstatus)) == NULL)
            return *pstatus;

          cache_status = cache_inode_add_cached_dirent(
	      pentry_parent,
	      &(array_dirent[iter].name),
	      pentry,
	      ht,
	      &new_dir_entry,
	      pclient,
	      pcontext,
	      pstatus);

          if(cache_status != CACHE_INODE_SUCCESS
             && cache_status != CACHE_INODE_ENTRY_EXISTS)
            return *pstatus;

          /*
           * Remember the FSAL readdir cookie associated with this dirent.  This
           * is needed for partial directory reads.
           * 
           * to_uint64 should be a lightweight operation--it is in the current
           * default implementation.  We think the right thing -should- happen
           * therefore with if _USE_MFSL. 
           *
           * I'm ignoring the status because the default operation is a memcmp--
           * we lready -have- the cookie. */

          if (cache_status != CACHE_INODE_ENTRY_EXISTS) {

              (void) FSAL_cookie_to_uint64(&array_dirent[iter].handle,
                                           pcontext, &array_dirent[iter].cookie,
                                           &new_dir_entry->fsal_cookie);

              /* we are filling in all entries, and the cookie avl was
               * cleared before adding dirents */
              new_dir_entry->cookie = i; /* still an offset */
              (void) avltree_insert(&new_dir_entry->node_c,
                                    &pentry_parent->object.dir.cookies);
          } /* !exist */

        } /* iter */
      
      /* Get prepared for next step */
      begin_cookie = end_cookie;

      /* next offset */
      i++;
    }
  while(fsal_eod != TRUE);

  /* Close the directory */
#ifdef _USE_MFSL
  fsal_status = MFSL_closedir(&fsal_dirhandle, &pclient->mfsl_context, NULL);
#else
  fsal_status = FSAL_closedir(&fsal_dirhandle);
#endif
  if(FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
      return *pstatus;
    }

  /* End of work */
  pentry_dir->object.dir.has_been_readdir = CACHE_INODE_YES;
  *pstatus = CACHE_INODE_SUCCESS;
  return *pstatus;
}                               /* cache_inode_readdir_populate */


/**
 *
 * revalidate_cookie_cache:  sync cookie avl offsets with the dentry name avl.
 *
 * If no cookies are cached, add those of any dirents in the name avl.  The
 * entry is assumed to be write locked.
 *
 * @param pentry [IN] entry for the parent directory to be read.
 *
 * @return void
 *
 */
static void revalidate_cookie_cache(cache_entry_t *dir_pentry,
                                    cache_inode_client_t *pclient)
{
  struct avltree_node *dirent_node;
  cache_inode_dir_entry_t *dirent;
  int size_n, size_c, ix;

  /* we'll try to add entries to any directory whose cookie
   * avl cache is currently empty (mutating dirent operations
   * clear it) */
#if 0
  if (avltree_size(&dir_pentry->object.dir.cookies) > 0)
      return;
#else
  size_c = avltree_size(&dir_pentry->object.dir.cookies);
  size_n = avltree_size(&dir_pentry->object.dir.dentries);
  if (size_c == size_n)
      return;
  /* the following is safe to call arbitrarily many times, with
   * CACHE_INODE_AVL_COOKIES -only-. */
      cache_inode_release_dirents(dir_pentry,
                                  pclient,
                                  CACHE_INODE_AVL_COOKIES);      
#endif

  dirent_node = avltree_first(&dir_pentry->object.dir.dentries);
  dirent = avltree_container_of(dirent_node,
				cache_inode_dir_entry_t, 
				node_n);

  ix = 3; /* first non-reserved cookie value */
  dirent_node = &dirent->node_n;
  while (dirent_node) {
      dirent = avltree_container_of(dirent_node,
				    cache_inode_dir_entry_t, 
				    node_n);
      dirent->cookie = ix;
      /* XXX we could optimize this somewhat (saving an internal
       * lookup in avltree_insert), by adding an avl append
       * operation */
      (void) avltree_insert(&dirent->node_c,
			    &dir_pentry->object.dir.cookies);
      dirent_node = avltree_next(dirent_node);
      ++ix;
  }

  return;
}

/**
 *
 * cache_inode_readdir: Reads a directory.
 *
 * Looks up for a name in a directory indicated by a cached entry. The 
 * directory should have been cached before.
 *
 * NEW: pending new (C-language) callback based dirent unpacking into caller
 * structures, we eliminate copies by returning dir entries by pointer.  To
 * permit this, we introduce lock donation.  If new int pointer argument
 * unlock is 1 on return, the calling thread holds pentry read-locked and
 * must release this lock after dirent processing.
 *
 * This is the only function in the cache_inode_readdir.c file that manages MT
 * safety on a directory cache entry.
 *
 * @param pentry [IN] entry for the parent directory to be read.
 * @param cookie [IN] cookie for the readdir operation (basically the offset).
 * @param nbwanted [IN] Maximum number of directory entries wanted.
 * @param peod_met [OUT] A flag to know if end of directory was met during this call.
 * @param dirent_array [OUT] the resulting array of found directory entries.
 * @param ht [IN] hash table used for the cache, unused in this call.
 * @param unlock [OUT] the caller shall release read-lock on pentry when done
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials
 * @param pstatus [OUT] returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success \n
 * @return CACHE_INODE_BAD_TYPE if entry is not related to a directory\n
 * @return CACHE_INODE_LRU_ERROR if allocation error occured when validating the entry
 *
 */
cache_inode_status_t cache_inode_readdir(cache_entry_t * dir_pentry,
                                         cache_inode_policy_t policy,
                                         uint64_t cookie,
                                         unsigned int nbwanted,
                                         unsigned int *pnbfound,
                                         uint64_t *pend_cookie,
                                         cache_inode_endofdir_t *peod_met,
                                         cache_inode_dir_entry_t **dirent_array,
                                         hash_table_t *ht,
                                         int *unlock,
                                         cache_inode_client_t *pclient,
                                         fsal_op_context_t *pcontext,
                                         cache_inode_status_t *pstatus)
{
  cache_inode_dir_entry_t dirent_key[1], *dirent;
  struct avltree_node *dirent_node;
  fsal_accessflags_t access_mask = 0;
  uint64_t inoff = cookie;
  int i = 0;

  /* Guide to parameters:
   * the first cookie is parameter 'cookie'
   * number of entries queried is set by parameter 'nbwanted'
   * number of found entries before eod is return is '*pnbfound'
   * '*peod_met' is set if end of directory is encountered */

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;
  dirent = NULL;

  /* Set initial value of unlock */
  *unlock = FALSE;

  /* end cookie initial value is the begin cookie */
  LogFullDebug(COMPONENT_NFS_READDIR,
               "--> Cache_inode_readdir: setting pend_cookie to cookie=%"
	       PRIu64,
               cookie);
  *pend_cookie = cookie;

  /* stats */
  pclient->stat.nb_call_total++;
  (pclient->stat.func_stats.nb_call[CACHE_INODE_READDIR])++;

  LogFullDebug(COMPONENT_NFS_READDIR,
               "--> Cache_inode_readdir: parameters are cookie=%"PRIu64
	       "nbwanted=%u",
               cookie, nbwanted);

  /* Sanity check */
  if(nbwanted == 0)
    {
      /* Asking for nothing is not a crime !!!!!
       * build a 'dummy' return in this case */
      *pstatus = CACHE_INODE_SUCCESS;
      *pnbfound = 0;
      *peod_met = TO_BE_CONTINUED;

      /* stats */
      (pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR])++;

      return *pstatus;
    }

  /* Force dir content invalidation if policy enforced no name cache */
  if( !CACHE_INODE_KEEP_CONTENT( dir_pentry->policy ) )
    return  cache_inode_readdir_nonamecache( dir_pentry,
                                             policy,
                                             cookie, 
                                             nbwanted, 
                                             pnbfound, 
                                             pend_cookie,
                                             peod_met,
                                             dirent_array, 
                                             ht, 
                                             unlock,
                                             pclient,
                                             pcontext,
                                             pstatus ) ;    
  
  P_w(&dir_pentry->lock);

  /* Renew the entry (to avoid having it being garbagged */
  if(cache_inode_renew_entry(dir_pentry, NULL, ht, pclient, pcontext,
			     pstatus) != CACHE_INODE_SUCCESS)
    {
      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_GETATTR])++;
      V_w(&dir_pentry->lock);
      return *pstatus;
    }

  /* readdir can be done only with a directory */
  if(dir_pentry->internal_md.type != DIRECTORY)
    {
      V_w(&dir_pentry->lock);
      *pstatus = CACHE_INODE_BAD_TYPE;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR])++;

      return *pstatus;
    }

  /* Check is user (as specified by the credentials) is authorized to read
   * the directory or not */
  access_mask = FSAL_MODE_MASK_SET(FSAL_R_OK) |
                FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
  if(cache_inode_access_no_mutex(dir_pentry,
                                 access_mask,
                                 ht, pclient,
				 pcontext,
				 pstatus) != CACHE_INODE_SUCCESS)
    {
      V_w(&dir_pentry->lock);

      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READDIR])++;
      return *pstatus;
    }


  /* Is the directory fully cached (this is done if a readdir call is done on the directory) */
  if(dir_pentry->object.dir.has_been_readdir != CACHE_INODE_YES)
  {

    /* populate the cache */
    if(cache_inode_readdir_populate(dir_pentry,
                                    policy,
		  		    ht,
				    pclient,
				    pcontext, pstatus) != CACHE_INODE_SUCCESS)
    {
      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_READDIR])++;

      V_w(&dir_pentry->lock);
      return *pstatus;
    }
  }

  /* deal with dentry cache invalidates */
  revalidate_cookie_cache(dir_pentry, pclient);

  /* Downgrade Writer lock to a reader one. */
  rw_lock_downgrade(&dir_pentry->lock);

  /* deal with initial cookie value:
   * 1. cookie is invalid (-should- be checked by caller)
   * 2. cookie is 0 (first cookie) -- ok
   * 3. cookie is > than highest dirent position (error) 
   * 4. cookie <= highest dirent position but > highest cached cookie
   *    (currently equivalent to #2, because we pre-populate the cookie avl)
   * 5. cookie is in cached range -- ok */

  if (cookie > 0) {

      if (cookie < 3) {
	  *pstatus = CACHE_INODE_BAD_COOKIE;
	  V_r(&dir_pentry->lock);
	  return *pstatus;
      }

      if ((inoff-3) > avltree_size(&dir_pentry->object.dir.dentries)) {
          LogCrit(COMPONENT_NFS_V4, "Bad initial cookie %"PRIu64,
                  inoff);
	  *pstatus = CACHE_INODE_BAD_COOKIE;
	  V_r(&dir_pentry->lock);
	  return *pstatus;
      }

      /* we assert this can now succeed */
      dirent_key->cookie = inoff;
      dirent_node = avltree_lookup(&dirent_key->node_c,
				   &dir_pentry->object.dir.cookies);

      if (! dirent_node) {
	  LogCrit(COMPONENT_NFS_READDIR,
		  "%s: seek to cookie=%"PRIu64" fail",
		  __func__,
		  inoff);
	  *pstatus = CACHE_INODE_NOT_FOUND;
	  V_r(&dir_pentry->lock);
	  return *pstatus;
      }

      /* switch avls */
      dirent = avltree_container_of(dirent_node,
				    cache_inode_dir_entry_t, 
				    node_c);
      dirent_node = &dirent->node_n;

      /* client wants the cookie -after- the last we sent, and
       * the Linux 3.0 and 3.1.0-rc7 clients misbehave if we
       * resend the last one */
      dirent_node = avltree_next(dirent_node);

  } else {
      /* initial readdir */
      dirent_node = avltree_first(&dir_pentry->object.dir.dentries);
  }

  LogFullDebug(COMPONENT_NFS_READDIR,
               "About to readdir in  cache_inode_readdir: pentry=%p "
	       "cookie=%"PRIu64,
               dir_pentry,
	       cookie);

  /* Now satisfy the request from the cached readdir--stop when either
   * the requested sequence or dirent sequence is exhausted */
  *pnbfound = 0;
  *peod_met = TO_BE_CONTINUED;

  for(i = 0; i < nbwanted; ++i)
  {
      if (!dirent_node)
	  break;

      dirent = avltree_container_of(dirent_node,
				    cache_inode_dir_entry_t, 
				    node_n);

      dirent_array[i] = dirent;
      (*pnbfound)++;

      dirent_node = avltree_next(dirent_node);
  }

  if (*pnbfound > 0)
  {
      if (!dirent)
      {
         LogCrit(COMPONENT_CACHE_INODE, "cache_inode_readdir: "
                 "UNEXPECTED CASE: dirent is NULL whereas nbfound>0");
         *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
         return CACHE_INODE_INCONSISTENT_ENTRY;
      }
      *pend_cookie = dirent->cookie;
  }

  if (! dirent_node)
      *peod_met = END_OF_DIR;

  *pstatus = cache_inode_valid(dir_pentry, CACHE_INODE_OP_GET, pclient);

  /* stats */
  if(*pstatus != CACHE_INODE_SUCCESS) {
      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_READDIR])++;
      V_r(&dir_pentry->lock);
  }
  else {
      (pclient->stat.func_stats.nb_success[CACHE_INODE_READDIR])++;
      *unlock = TRUE;
  }

  return *pstatus;
}                               /* cache_inode_readdir */

/**
 *
 * cache_inode_get_cookieverf: get a cookie verifier
 *
 * Get the cookie verifier for a directory
 *
 * @param pentry   [IN]  entry for the directory to be read.
 * @param pcontext [IN]  FSAL credentials
 * @param pverf    [OUT] Verifier
 * @param pstatus  [OUT] Returned status.
 *
 * @return CACHE_INODE_SUCCESS if operation is a success
 *
 */
cache_inode_status_t cache_inode_cookieverf(cache_entry_t * pentry,
                                            fsal_op_context_t * pcontext,
                                            uint64_t * pverf,
                                            cache_inode_status_t * pstatus)
{
  fsal_status_t fsal_status;
  fsal_handle_t* handle = cache_inode_get_fsal_handle(pentry,
                                                      pstatus);

  if (*pstatus != CACHE_INODE_SUCCESS)
    {
      return *pstatus;
    }

  fsal_status = FSAL_get_cookieverf(handle, pcontext, pverf);
  if (FSAL_IS_ERROR(fsal_status))
    {
      *pstatus = cache_inode_error_convert(fsal_status);
    }
  else
    {
      *pstatus = CACHE_INODE_SUCCESS;
    }

  return *pstatus;
}

