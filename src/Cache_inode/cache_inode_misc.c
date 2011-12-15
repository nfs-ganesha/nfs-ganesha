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
 *
 * \File    cache_inode_misc.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:51 $
 * \version $Revision: 1.63 $
 * \brief   Some routines for management of the cache_inode layer, shared by other calls.
 *
 * HashTable.c : Some routines for management of the cache_inode layer, shared by other calls.
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
#include "cache_content.h"
#include "stuff_alloc.h"
#include "nfs4_acls.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>

char *cache_inode_function_names[] = {
  "cache_inode_access",
  "cache_inode_getattr",
  "cache_inode_mkdir",
  "cache_inode_remove",
  "cache_inode_statfs",
  "cache_inode_link",
  "cache_inode_readdir",
  "cache_inode_rename",
  "cache_inode_symlink",
  "cache_inode_create",
  "cache_inode_lookup",
  "cache_inode_lookupp",
  "cache_inode_readlink",
  "cache_inode_truncate",
  "cache_inode_get",
  "cache_inode_release",
  "cache_inode_setattr",
  "cache_inode_new_entry",
  "cache_inode_read_data",
  "cache_inode_write_data",
  "cache_inode_add_data_cache",
  "cache_inode_release_data_cache",
  "cache_inode_renew_entry",
  "cache_inode_commit"
  "cache_inode_add_state",
  "cache_inode_add_state",
  "cache_inode_get_state",
  "cache_inode_set_state",
};

const char *cache_inode_err_str(cache_inode_status_t err)
{
  switch(err)
    {
      case CACHE_INODE_SUCCESS:               return "CACHE_INODE_SUCCESS";
      case CACHE_INODE_MALLOC_ERROR:          return "CACHE_INODE_MALLOC_ERROR";
      case CACHE_INODE_POOL_MUTEX_INIT_ERROR: return "CACHE_INODE_POOL_MUTEX_INIT_ERROR";
      case CACHE_INODE_GET_NEW_LRU_ENTRY:     return "CACHE_INODE_GET_NEW_LRU_ENTRY";
      case CACHE_INODE_UNAPPROPRIATED_KEY:    return "CACHE_INODE_UNAPPROPRIATED_KEY";
      case CACHE_INODE_INIT_ENTRY_FAILED:     return "CACHE_INODE_INIT_ENTRY_FAILED";
      case CACHE_INODE_FSAL_ERROR:            return "CACHE_INODE_FSAL_ERROR";
      case CACHE_INODE_LRU_ERROR:             return "CACHE_INODE_LRU_ERROR";
      case CACHE_INODE_HASH_SET_ERROR:        return "CACHE_INODE_HASH_SET_ERROR";
      case CACHE_INODE_NOT_A_DIRECTORY:       return "CACHE_INODE_NOT_A_DIRECTORY";
      case CACHE_INODE_INCONSISTENT_ENTRY:    return "CACHE_INODE_INCONSISTENT_ENTRY";
      case CACHE_INODE_BAD_TYPE:              return "CACHE_INODE_BAD_TYPE";
      case CACHE_INODE_ENTRY_EXISTS:          return "CACHE_INODE_ENTRY_EXISTS";
      case CACHE_INODE_DIR_NOT_EMPTY:         return "CACHE_INODE_DIR_NOT_EMPTY";
      case CACHE_INODE_NOT_FOUND:             return "CACHE_INODE_NOT_FOUND";
      case CACHE_INODE_INVALID_ARGUMENT:      return "CACHE_INODE_INVALID_ARGUMENT";
      case CACHE_INODE_INSERT_ERROR:          return "CACHE_INODE_INSERT_ERROR";
      case CACHE_INODE_HASH_TABLE_ERROR:      return "CACHE_INODE_HASH_TABLE_ERROR";
      case CACHE_INODE_FSAL_EACCESS:          return "CACHE_INODE_FSAL_EACCESS";
      case CACHE_INODE_IS_A_DIRECTORY:        return "CACHE_INODE_IS_A_DIRECTORY";
      case CACHE_INODE_FSAL_EPERM:            return "CACHE_INODE_FSAL_EPERM";
      case CACHE_INODE_NO_SPACE_LEFT:         return "CACHE_INODE_NO_SPACE_LEFT";
      case CACHE_INODE_CACHE_CONTENT_ERROR:   return "CACHE_INODE_CACHE_CONTENT_ERROR";
      case CACHE_INODE_CACHE_CONTENT_EXISTS:  return "CACHE_INODE_CACHE_CONTENT_EXISTS";
      case CACHE_INODE_CACHE_CONTENT_EMPTY:   return "CACHE_INODE_CACHE_CONTENT_EMPTY";
      case CACHE_INODE_READ_ONLY_FS:          return "CACHE_INODE_READ_ONLY_FS";
      case CACHE_INODE_IO_ERROR:              return "CACHE_INODE_IO_ERROR";
      case CACHE_INODE_FSAL_ESTALE:           return "CACHE_INODE_FSAL_ESTALE";
      case CACHE_INODE_FSAL_ERR_SEC:          return "CACHE_INODE_FSAL_ERR_SEC";
      case CACHE_INODE_STATE_CONFLICT:        return "CACHE_INODE_STATE_CONFLICT";
      case CACHE_INODE_QUOTA_EXCEEDED:        return "CACHE_INODE_QUOTA_EXCEEDED";
      case CACHE_INODE_DEAD_ENTRY:            return "CACHE_INODE_DEAD_ENTRY";
      case CACHE_INODE_ASYNC_POST_ERROR:      return "CACHE_INODE_ASYNC_POST_ERROR";
      case CACHE_INODE_NOT_SUPPORTED:         return "CACHE_INODE_NOT_SUPPORTED";
      case CACHE_INODE_STATE_ERROR:           return "CACHE_INODE_STATE_ERROR";
      case CACHE_INODE_FSAL_DELAY:            return "CACHE_INODE_FSAL_DELAY";
      case CACHE_INODE_NAME_TOO_LONG:         return "CACHE_INODE_NAME_TOO_LONG";
      case CACHE_INODE_BAD_COOKIE:            return "CACHE_INODE_BAD_COOKIE";
      case CACHE_INODE_FILE_BIG:              return "CACHE_INODE_FILE_BIG";
    }
  return "unknown";
}

#ifdef _USE_PROXY
void cache_inode_print_srvhandle(char *comment, cache_entry_t * pentry);
#endif

static void cache_inode_init_attributes(cache_entry_t *pentry, fsal_attrib_list_t *pattr);

/**
 *
 * ci_avl_dir_name_cmp
 *
 * Compare dir entry avl nodes by name.
 *
 * @param lhs [IN] first key
 * @param rhs [IN] second key
 * @return -1, 0, or 1, as strcmp(3)
 *
 */
static int ci_avl_dir_name_cmp(const struct avltree_node *lhs,
			       const struct avltree_node *rhs)
{
    cache_inode_dir_entry_t *lhe = avltree_container_of(
	lhs, cache_inode_dir_entry_t, node_n);
    cache_inode_dir_entry_t *rhe = avltree_container_of(
	rhs, cache_inode_dir_entry_t, node_n);

    return FSAL_namecmp(&lhe->name, &rhe->name);
}

/**
 *
 * ci_avl_dir_ck_cmp
 *
 * Compare dir entry avl nodes by cookie (offset).
 *
 * @param lhs [IN] first key
 * @param rhs [IN] second key
 * @return -1, 0, or 1, as strcmp(3)
 *
 */
static int ci_avl_dir_ck_cmp(const struct avltree_node *lhs,
			     const struct avltree_node *rhs)
{
    cache_inode_dir_entry_t *lhe = avltree_container_of(
	lhs, cache_inode_dir_entry_t, node_c);
    cache_inode_dir_entry_t *rhe = avltree_container_of(
	rhs, cache_inode_dir_entry_t, node_c);

    if (lhe->cookie < rhe->cookie)
	return (-1);

    if (lhe->cookie == rhe->cookie)
	return (0);

    /* r > l */
    return 1;
}

/**
 *
 * cache_inode_compare_key_fsal: Compares two keys used in cache inode.
 *
 * Compare two keys used in cache inode. These keys are basically made from FSAL
 * related information.
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 * @return 0 if keys are the same, 1 otherwise
 *
 * @see FSAL_handlecmp
 *
 */
int cache_inode_compare_key_fsal(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  fsal_status_t status;
  cache_inode_fsal_data_t *pfsdata1 = NULL;
  cache_inode_fsal_data_t *pfsdata2 = NULL;

  /* Test if one of teh entries are NULL */
  if(buff1->pdata == NULL)
    return (buff2->pdata == NULL) ? 0 : 1;
  else
    {
      if(buff2->pdata == NULL)
        return -1;              /* left member is the greater one */
      else
        {
          int rc;
          pfsdata1 = (cache_inode_fsal_data_t *) (buff1->pdata);
          pfsdata2 = (cache_inode_fsal_data_t *) (buff2->pdata);

          rc = (!FSAL_handlecmp(&pfsdata1->handle, &pfsdata2->handle, &status)
                && (pfsdata1->cookie == pfsdata2->cookie)) ? 0 : 1;

          return rc;
        }

    }
  /* This line should never be reached */
}                               /* cache_inode_compare_key_fsal */


/**
 *
 * cache_inode_set_time_current: set the fsal_time in a pentry struct to the current time.
 *
 * Sets the fsal_time in a pentry struct to the current time. This function is using gettimeofday.
 *
 * @param ptime [OUT] pointer to time to be set 
 *
 * @return 0 if keys if successfully build, -1 otherwise
 *
 */ 
int cache_inode_set_time_current( fsal_time_t * ptime )
{
  struct timeval t ;

  if( ptime == NULL )
    return -1 ;

  if( gettimeofday( &t, NULL ) != 0 )
    return -1 ;

  ptime->seconds  = t.tv_sec ;
  ptime->nseconds = 1000*t.tv_usec ;

  return 0 ;
} /* cache_inode_set_time_current */


/**
 *
 * cache_inode_fsaldata_2_key: builds a key from the FSAL data.
 *
 * Builds a key from the FSAL data. If the key is used for reading and stay local to the function
 * pclient can be NULL (psfsdata in the scope of the current calling routine is used). If the key
 * must survive after the end of the calling routine, a new key is allocated and ressource in *pclient are used
 *
 * @param pkey [OUT] computed key
 * @param pfsdata [IN] FSAL data to be used to compute the key
 * @param pclient [INOUT] if NULL, pfsdata is used to build the key (that stay local), if not pool_key is used to
 * allocate a new key
 *
 * @return 0 if keys if successfully build, 1 otherwise
 *
 */
int cache_inode_fsaldata_2_key(hash_buffer_t * pkey, cache_inode_fsal_data_t * pfsdata,
                               cache_inode_client_t * pclient)
{
  cache_inode_fsal_data_t *ppoolfsdata = NULL;

  /* Allocate a new key for storing data, if this a set, not a get
   * in the case of a 'get' key, pclient == NULL and * pfsdata is used */

  if(pclient != NULL)
    {
      GetFromPool(ppoolfsdata, &pclient->pool_key, cache_inode_fsal_data_t);
      if(ppoolfsdata == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Can't allocate a new key from cache pool");
          return 1;
        }

      *ppoolfsdata = *pfsdata;

      pkey->pdata = (caddr_t) ppoolfsdata;
    }
  else
    pkey->pdata = (caddr_t) pfsdata;

  pkey->len = sizeof(cache_inode_fsal_data_t);

  return 0;
}                               /* cache_inode_fsaldata_2_key */

/**
 *
 * cache_inode_release_fsaldata_key: release a fsal key used to access the cache inode
 *
 * Release a fsal key used to access the cache inode.
 *
 * @param pkey    [IN]    pointer to the key to be freed
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_release_fsaldata_key(hash_buffer_t * pkey,
                                      cache_inode_client_t * pclient)
{
  cache_inode_fsal_data_t *ppoolfsdata = NULL;

  ppoolfsdata = (cache_inode_fsal_data_t *) pkey->pdata;

  ReleaseToPool(ppoolfsdata, &pclient->pool_key);
}                               /* cache_inode_release_fsaldata_key */

/**
 *
 * cache_inode_new_entry: adds a new entry to the cache_inode.
 *
 * adds a new entry to the cache_inode. These function os used to allocate entries of any kind. Some
 * parameter are meaningless for some types or used for others.
 *
 * @param pfsdata [IN] FSAL data for the entry to be created (used to build the key)
 * @param pfsal_attr [in] attributes for the entry (unused if value == NULL). Used for caching.
 * @param type [IN] type of the entry to be created.
 * @param link_content [IN] if type == SYMBOLIC_LINK, this is the content of the link. Unused otherwise
 * @param pentry_dir_prev [IN] if type == DIR_CONTINUE, this is the previous entry in the dir_chain. Unused otherwise.
 * @param ht [INOUT] hash table used for the cache.
 * @param pclient [INOUT]ressource allocated by the client for the nfs management.
 * @param pcontext [IN] FSAL credentials for the operation.
 * @param create_flag [IN] a flag which shows if the entry is newly created or not
 * @param pstatus [OUT] returned status.
 *
 * @return the same as *pstatus
 *
 */
cache_entry_t *cache_inode_new_entry(cache_inode_fsal_data_t   * pfsdata,
                                     fsal_attrib_list_t        * pfsal_attr,
                                     cache_inode_file_type_t     type,
                                     cache_inode_policy_t        policy,
                                     cache_inode_create_arg_t  * pcreate_arg,
                                     cache_entry_t             * pentry_dir_prev,
                                     hash_table_t              * ht,
                                     cache_inode_client_t      * pclient,
                                     fsal_op_context_t         * pcontext,
                                     unsigned int                create_flag,
                                     cache_inode_status_t      * pstatus)
{
  cache_entry_t *pentry = NULL;
  hash_buffer_t key, value;
  fsal_attrib_list_t fsal_attributes;
  fsal_status_t fsal_status;
  int rc = 0;
  off_t size_in_cache;
  cache_content_status_t cache_content_status ;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  pclient->stat.nb_call_total++;
  (pclient->stat.func_stats.nb_call[CACHE_INODE_NEW_ENTRY])++;

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, pfsdata, NULL))
    {
      *pstatus = CACHE_INODE_UNAPPROPRIATED_KEY;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;
      cache_inode_release_fsaldata_key(&key, pclient);

      return NULL;
    }

  /* Check if the entry doesn't already exists */
  if(HashTable_Get(ht, &key, &value) == HASHTABLE_SUCCESS)
    {
      /* Entry is already in the cache, do not add it */
      pentry = (cache_entry_t *) value.pdata;
      *pstatus = CACHE_INODE_ENTRY_EXISTS;

      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Trying to add an already existing entry. Found entry %p type: %d State: %d, New type: %d",
               pentry, pentry->internal_md.type, pentry->internal_md.valid_state, type);

      /* stats */
      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_NEW_ENTRY])++;

      return pentry;
    }

  GetFromPool(pentry, &pclient->pool_entry, cache_entry_t);
  if(pentry == NULL)
    {
      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_new_entry: Can't allocate a new entry from cache pool");
      *pstatus = CACHE_INODE_MALLOC_ERROR;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;

      return NULL;
    }

  /*  if( type == DIRECTORY ) */

  if(rw_lock_init(&(pentry->lock)) != 0)
    {
      ReleaseToPool(pentry, &pclient->pool_entry);
      LogCrit(COMPONENT_CACHE_INODE,
              "cache_inode_new_entry: rw_lock_init returned %d (%s)",
              errno, strerror(errno));
      *pstatus = CACHE_INODE_INIT_ENTRY_FAILED;

      /* stats */
      (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_NEW_ENTRY])++;

      return NULL;
    }

  /* Call FSAL to get information about the object if not provided.  If attributes 
   * are provided as pfsal_attr parameter, use them. Call FSAL_getattrs otherwise. */
  if(pfsal_attr == NULL)
    {
       fsal_attributes.asked_attributes = pclient->attrmask;
       fsal_status = FSAL_getattrs(&pfsdata->handle, pcontext, &fsal_attributes);

       if(FSAL_IS_ERROR(fsal_status))
         {
           /* Put the entry back in its pool */
           LogCrit(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: FSAL_getattrs failed for pentry = %p",
                   pentry);
           ReleaseToPool(pentry, &pclient->pool_entry);
           *pstatus = cache_inode_error_convert(fsal_status);

           if(fsal_status.major == ERR_FSAL_STALE)
             {
                cache_inode_status_t kill_status;

                LogCrit(COMPONENT_CACHE_INODE,
                        "cache_inode_new_entry: Stale FSAL File Handle detected for pentry = %p",
                        pentry);

                if(cache_inode_kill_entry(pentry, NO_LOCK, ht, pclient, &kill_status) !=
                   CACHE_INODE_SUCCESS)
                    LogCrit(COMPONENT_CACHE_INODE,
                            "cache_inode_new_entry: Could not kill entry %p, status = %u",
                             pentry, kill_status);

              }
              /* stats */
              (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;

              return NULL;
         }
    }
  else
    {
      /* Use the provided attributes */
      fsal_attributes = *pfsal_attr;
    }

  /* Init the internal metadata */
  pentry->internal_md.type = type;
  pentry->internal_md.valid_state = VALID;
  pentry->internal_md.read_time = 0;
  pentry->internal_md.mod_time = pentry->internal_md.alloc_time = time(NULL);
  pentry->internal_md.refresh_time = pentry->internal_md.alloc_time;

  pentry->gc_lru_entry = NULL;
  pentry->gc_lru = NULL;

  pentry->policy = policy ;

  /* No parent for now, it will be added in cache_inode_add_cached_dirent */
  pentry->parent_list = NULL;

  switch (type)
    {
    case REGULAR_FILE:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a REGULAR_FILE pentry=%p policy=%u",
               pentry, policy );

      pentry->object.file.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.file.handle;
#ifdef _USE_MFSL_PROXY
      pentry->mobject.plock = &pentry->lock;
#endif
#endif
      pentry->object.file.pentry_content = NULL;    /* Not yet a File Content entry associated with this entry */
      init_glist(&pentry->object.file.state_list);  /* No associated states yet */
      init_glist(&pentry->object.file.lock_list);   /* No associated locks yet */
      if(pthread_mutex_init(&pentry->object.file.lock_list_mutex, NULL) != 0)
        {
          ReleaseToPool(pentry, &pclient->pool_entry);

          LogCrit(COMPONENT_CACHE_INODE,
                  "cache_inode_new_entry: pthread_mutex_init of lock_list_mutex returned %d (%s)",
                  errno, strerror(errno));

          *pstatus = CACHE_INODE_INIT_ENTRY_FAILED;

          /* stat */
          (pclient->stat.func_stats.nb_err_retryable[CACHE_INODE_NEW_ENTRY])++;
          return NULL;
        }
      pentry->object.file.open_fd.fileno = 0;
      pentry->object.file.open_fd.last_op = 0;
      pentry->object.file.open_fd.openflags = 0;
#ifdef _USE_MFSL
      memset(&(pentry->object.file.open_fd.mfsl_fd), 0, sizeof(mfsl_file_t));
#else
      memset(&(pentry->object.file.open_fd.fd), 0, sizeof(fsal_file_t));
#endif
      memset(&(pentry->object.file.unstable_data), 0,
             sizeof(cache_inode_unstable_data_t));
#ifdef _USE_PROXY
      pentry->object.file.pname = NULL;
      pentry->object.file.pentry_parent_open = NULL;
#endif

#ifdef _USE_PNFS_SPNFS_LIKE /** @todo do the thing in a cleaner way here */
      pentry->object.file.pnfs_file.ds_file.allocated = FALSE;
#endif

      break;

    case DIRECTORY:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a DIRECTORY pentry=%p policy=%u",
               pentry, policy);

      pentry->object.dir.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.dir.handle;
#endif

      pentry->object.dir.has_been_readdir = CACHE_INODE_NO;
      pentry->object.dir.nbactive = 0;
      pentry->object.dir.referral = NULL;

      /* init avl trees */
      avltree_init(&pentry->object.dir.dentries, ci_avl_dir_name_cmp,
		   0 /* flags */);
      avltree_init(&pentry->object.dir.cookies, ci_avl_dir_ck_cmp,
		   0 /* flags */);
      break;

    case SYMBOLIC_LINK:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a SYMBOLIC_LINK pentry=%p policy=%u",
               pentry, policy );
      GetFromPool(pentry->object.symlink, &pclient->pool_entry_symlink,
                  cache_inode_symlink_t);
      if(pentry->object.symlink == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "Can't allocate entry symlink from symlink pool");
          break;
        }
      pentry->object.symlink->handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.symlink->handle;
#endif
     if( CACHE_INODE_KEEP_CONTENT( policy ) )
      {  
        fsal_status =
            FSAL_pathcpy(&pentry->object.symlink->content, &pcreate_arg->link_content);
        if(FSAL_IS_ERROR(fsal_status))
         {
             *pstatus = cache_inode_error_convert(fsal_status);
             LogDebug(COMPONENT_CACHE_INODE,
                      "cache_inode_new_entry: FSAL_pathcpy failed");
             cache_inode_release_symlink(pentry, &pclient->pool_entry_symlink);
             ReleaseToPool(pentry, &pclient->pool_entry);
         }
       }
      break;

    case SOCKET_FILE:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a SOCKET_FILE pentry = %p",
               pentry);

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      break;

    case FIFO_FILE:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a FIFO_FILE pentry = %p",
               pentry);

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      break;

    case BLOCK_FILE:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a BLOCK_FILE pentry=%p policy=%u",
               pentry, policy);

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      break;

    case CHARACTER_FILE:
      LogDebug(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: Adding a CHARACTER_FILE pentry=%p policy=%u",
               pentry, policy);

      pentry->object.special_obj.handle = pfsdata->handle;
#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.special_obj.handle;
#endif
      break;

    case FS_JUNCTION:
        LogDebug(COMPONENT_CACHE_INODE,
                 "cache_inode_new_entry: Adding a FS_JUNCTION pentry=%p policy=%u",
                 pentry, policy);

        fsal_status = FSAL_lookupJunction( &pfsdata->handle, pcontext,
					   &pentry->object.dir.handle,
					   NULL);
        if( FSAL_IS_ERROR( fsal_status ) )
         {
           *pstatus = cache_inode_error_convert(fsal_status);
           LogDebug(COMPONENT_CACHE_INODE,
                    "cache_inode_new_entry: FSAL_lookupJunction failed");
           ReleaseToPool(pentry, &pclient->pool_entry);
         }

      fsal_attributes.asked_attributes = pclient->attrmask;
      fsal_status = FSAL_getattrs( &pentry->object.dir.handle, pcontext,
				   &fsal_attributes);
      if( FSAL_IS_ERROR( fsal_status ) )
         {
           *pstatus = cache_inode_error_convert(fsal_status);
           LogDebug(COMPONENT_CACHE_INODE,
                    "cache_inode_new_entry: FSAL_getattrs on junction fh failed");
           ReleaseToPool(pentry, &pclient->pool_entry);
         }


      /* XXX Fake FS_JUNCTION into directory */
      pentry->internal_md.type = DIRECTORY;

#ifdef _USE_MFSL
      pentry->mobject.handle = pentry->object.dir.handle;
#endif

      pentry->object.dir.has_been_readdir = CACHE_INODE_NO;
      pentry->object.dir.nbactive = 0;
      pentry->object.dir.referral = NULL;

      /* init avl trees */
      avltree_init(&pentry->object.dir.dentries, ci_avl_dir_name_cmp,
		   0 /* flags */);
      avltree_init(&pentry->object.dir.cookies, ci_avl_dir_ck_cmp,
		   0 /* flags */);
      break ;

    default:
      /* Should never happen */
      *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
      LogMajor(COMPONENT_CACHE_INODE,
               "cache_inode_new_entry: unknown type %u provided",
               type);
      ReleaseToPool(pentry, &pclient->pool_entry);

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;

      return NULL;
    }

  /* Turn the input to a hash key */
  if(cache_inode_fsaldata_2_key(&key, pfsdata, pclient))
    {
      *pstatus = CACHE_INODE_UNAPPROPRIATED_KEY;

      /* stats */
      (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;
      cache_inode_release_fsaldata_key(&key, pclient);

      return NULL;
    }

  /* Adding the entry in the cache */
  value.pdata = (caddr_t) pentry;
  value.len = sizeof(cache_entry_t);

  if((rc =
      HashTable_Test_And_Set(ht, &key, &value,
                             HASHTABLE_SET_HOW_SET_NO_OVERWRITE)) != HASHTABLE_SUCCESS)
    {
      /* Put the entry back in its pool */
      if (pentry->object.symlink)
         cache_inode_release_symlink(pentry, &pclient->pool_entry_symlink);
      ReleaseToPool(pentry, &pclient->pool_entry);
      LogWarn(COMPONENT_CACHE_INODE,
              "cache_inode_new_entry: entry could not be added to hash, rc=%d",
              rc);

      if( rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS )
       {
         *pstatus = CACHE_INODE_HASH_SET_ERROR;

         /* stats */
         (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;

         return NULL;
       }
     else
      {
        LogDebug(COMPONENT_CACHE_INODE,
                 "cache_inode_new_entry: concurrency detected during cache insertion");

        /* This situation occurs when several threads try to init the same uncached entry
         * at the same time. The first creates the entry and the others got  HASHTABLE_ERROR_KEY_ALREADY_EXISTS
         * In this case, the already created entry (by the very first thread) is returned */
        if( ( rc = HashTable_Get( ht, &key, &value ) ) != HASHTABLE_SUCCESS )
         {
            *pstatus = CACHE_INODE_HASH_SET_ERROR ;
            (pclient->stat.func_stats.nb_err_unrecover[CACHE_INODE_NEW_ENTRY])++;
            return NULL ;
         }

        pentry = (cache_entry_t *) value.pdata ;
        *pstatus = CACHE_INODE_SUCCESS ;
        return pentry ;
      }
    }

  /* Now that added as a new entry, init attribute. */
  cache_inode_init_attributes(pentry, &fsal_attributes);

  /* if entry is a REGULAR_FILE and has a related data cache entry from a previous server instance that crashed, recover it */
  /* This is done only when this is not a creation (when creating a new file, it is impossible to have it cached)           */
  if(type == REGULAR_FILE && create_flag == FALSE)
    {
      cache_content_test_cached(pentry,
                                (cache_content_client_t *) pclient->pcontent_client,
                                pcontext, &cache_content_status);

      if(cache_content_status == CACHE_CONTENT_SUCCESS)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_new_entry: Entry %p is already datacached, recovering...",
                   pentry);

          /* Adding the cached entry to the data cache */
          if((pentry->object.file.pentry_content = cache_content_new_entry(pentry,
                                                                           NULL,
                                                                           (cache_content_client_t
                                                                            *)
                                                                           pclient->
                                                                           pcontent_client,
                                                                           RECOVER_ENTRY,
                                                                           pcontext,
                                                                           &cache_content_status))
             == NULL)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                      "Error recovering cached data for pentry %p",
                      pentry);
            }
          else
            LogDebug(COMPONENT_CACHE_INODE,
                     "Cached data added successfully for pentry %p",
                     pentry);

          /* Recover the size from the data cache too... */
          if((size_in_cache =
              cache_content_get_cached_size((cache_content_entry_t *) pentry->object.
                                            file.pentry_content)) == -1)
            {
              LogCrit(COMPONENT_CACHE_INODE,
                      "Error when recovering size in cache for pentry %p",
                      pentry);
            }
          else
            pentry->object.file.attributes.filesize = (fsal_size_t) size_in_cache;

        }
    }

  /* Final step */
  P_w(&pentry->lock);
  *pstatus = cache_inode_valid(pentry, CACHE_INODE_OP_GET, pclient);
  V_w(&pentry->lock);

  LogDebug(COMPONENT_CACHE_INODE,
           "cache_inode_new_entry: New entry %p added",
           pentry);
  *pstatus = CACHE_INODE_SUCCESS;

  /* stats */
  (pclient->stat.func_stats.nb_success[CACHE_INODE_NEW_ENTRY])++;

  return pentry;
}                               /* cache_inode_new_entry */

/**
 *
 * cache_clean_entry: cleans an entry when it is garbagge collected.
 *
 * Cleans an entry when it is garbagge collected.
 *
 * @param pentry [INOUT] the entry to be cleaned.
 *
 * @return CACHE_INODE_SUCCESS in all cases.
 *
 */
cache_inode_status_t cache_inode_clean_entry(cache_entry_t * pentry)
{
  pentry->internal_md.type = RECYCLED;
  pentry->internal_md.valid_state = INVALID;
  pentry->internal_md.read_time = 0;
  pentry->internal_md.mod_time = 0;
  pentry->internal_md.refresh_time = 0;
  pentry->internal_md.alloc_time = 0;
  return CACHE_INODE_SUCCESS;
}

/**
 *
 * cache_inode_error_convert: converts an FSAL error to the corresponding cache_inode error.
 *
 * Converts an FSAL error to the corresponding cache_inode error.
 *
 * @param fsal_status [IN] fsal error to be converted.
 *
 * @return the result of the conversion.
 *
 */
cache_inode_status_t cache_inode_error_convert(fsal_status_t fsal_status)
{
  switch (fsal_status.major)
    {
    case ERR_FSAL_NO_ERROR:
      return CACHE_INODE_SUCCESS;

    case ERR_FSAL_NOENT:
      return CACHE_INODE_NOT_FOUND;

    case ERR_FSAL_EXIST:
      return CACHE_INODE_ENTRY_EXISTS;

    case ERR_FSAL_ACCESS:
      return CACHE_INODE_FSAL_EACCESS;

    case ERR_FSAL_PERM:
      return CACHE_INODE_FSAL_EPERM;

    case ERR_FSAL_NOSPC:
      return CACHE_INODE_NO_SPACE_LEFT;

    case ERR_FSAL_NOTEMPTY:
      return CACHE_INODE_DIR_NOT_EMPTY;

    case ERR_FSAL_ROFS:
      return CACHE_INODE_READ_ONLY_FS;

    case ERR_FSAL_NOTDIR:
      return CACHE_INODE_NOT_A_DIRECTORY;

    case ERR_FSAL_IO:
    case ERR_FSAL_NXIO:
      return CACHE_INODE_IO_ERROR;

    case ERR_FSAL_STALE:
    case ERR_FSAL_BADHANDLE:
    case ERR_FSAL_FHEXPIRED:
      return CACHE_INODE_FSAL_ESTALE;

    case ERR_FSAL_INVAL:
    case ERR_FSAL_OVERFLOW:
      return CACHE_INODE_INVALID_ARGUMENT;

    case ERR_FSAL_DQUOT:
      return CACHE_INODE_QUOTA_EXCEEDED;

    case ERR_FSAL_SEC:
      return CACHE_INODE_FSAL_ERR_SEC;

    case ERR_FSAL_NOTSUPP:
    case ERR_FSAL_ATTRNOTSUPP:
      return CACHE_INODE_NOT_SUPPORTED;

    case ERR_FSAL_DELAY:
      return CACHE_INODE_FSAL_DELAY;

    case ERR_FSAL_NAMETOOLONG:
      return CACHE_INODE_NAME_TOO_LONG;

    case ERR_FSAL_NOMEM:
      return CACHE_INODE_MALLOC_ERROR;

    case ERR_FSAL_BADCOOKIE:
      return CACHE_INODE_BAD_COOKIE;

    case ERR_FSAL_NOT_OPENED:
      LogDebug(COMPONENT_CACHE_INODE,
               "Conversion of ERR_FSAL_NOT_OPENED to CACHE_INODE_FSAL_ERROR");
      return CACHE_INODE_FSAL_ERROR;

    case ERR_FSAL_SYMLINK:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_BADTYPE:
      return CACHE_INODE_BAD_TYPE;

    case ERR_FSAL_FBIG:
      return CACHE_INODE_FILE_BIG;

    case ERR_FSAL_DEADLOCK:
    case ERR_FSAL_BLOCKED:
    case ERR_FSAL_INTERRUPT:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_NOT_INIT:
    case ERR_FSAL_ALREADY_INIT:
    case ERR_FSAL_BAD_INIT:
    case ERR_FSAL_NO_QUOTA:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_MLINK:
    case ERR_FSAL_TOOSMALL:
    case ERR_FSAL_TIMEOUT:
    case ERR_FSAL_SERVERFAULT:
      /* These errors should be handled inside Cache Inode (or should never be seen by Cache Inode) */
      LogDebug(COMPONENT_CACHE_INODE,
               "Conversion of FSAL error %d,%d to CACHE_INODE_FSAL_ERROR",
               fsal_status.major, fsal_status.minor);
      return CACHE_INODE_FSAL_ERROR;
    }

  /* We should never reach this line, this may produce a warning with certain compiler */
  LogCrit(COMPONENT_CACHE_INODE,
          "cache_inode_error_convert: default conversion to CACHE_INODE_FSAL_ERROR for error %d, line %u should never be reached",
           fsal_status.major, __LINE__);
  return CACHE_INODE_FSAL_ERROR;
}                               /* cache_inode_error_convert */

/**
 *
 * cache_inode_valid: validates an entry to update its garbagge status.
 *
 * Validates an error to update its garbagge status.
 * Entry is supposed to be locked when this function is called !!
 *
 * @param pentry [INOUT] entry to be validated.
 * @param op [IN] can be set to CACHE_INODE_OP_GET or CACHE_INODE_OP_SET to show the type of operation done.
 * @param pclient [INOUT] ressource allocated by the client for the nfs management.
 *
 * @return CACHE_INODE_SUCCESS if successful
 * @return CACHE_INODE_LRU_ERROR if an errorr occured in LRU management.
 *
 */
cache_inode_status_t cache_inode_valid(cache_entry_t * pentry,
                                       cache_inode_op_t op,
                                       cache_inode_client_t * pclient)
{
  /* /!\ NOTE THIS CAREFULLY: entry is supposed to be locked when this function is called !! */

  cache_inode_status_t cache_status;
  cache_content_status_t cache_content_status;
  LRU_status_t lru_status;
  LRU_entry_t *plru_entry = NULL;
  cache_content_client_t *pclient_content = NULL;
  cache_content_entry_t *pentry_content = NULL;
#ifndef _NO_BUDDY_SYSTEM
  buddy_stats_t __attribute__ ((__unused__)) bstats;
#endif

  if(pentry == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Invalidate former entry if needed */
  if(pentry->gc_lru != NULL && pentry->gc_lru_entry)
    {
      if(LRU_invalidate(pentry->gc_lru, pentry->gc_lru_entry) != LRU_LIST_SUCCESS)
        {
          if (pentry->object.symlink)
            cache_inode_release_symlink(pentry, &pclient->pool_entry_symlink);
          ReleaseToPool(pentry, &pclient->pool_entry);
          return CACHE_INODE_LRU_ERROR;
        }
    }

  if((plru_entry = LRU_new_entry(pclient->lru_gc, &lru_status)) == NULL)
    {
      if (pentry->object.symlink)
        cache_inode_release_symlink(pentry, &pclient->pool_entry_symlink);
      ReleaseToPool(pentry, &pclient->pool_entry);
      return CACHE_INODE_LRU_ERROR;
    }
  plru_entry->buffdata.pdata = (caddr_t) pentry;
  plru_entry->buffdata.len = sizeof(cache_entry_t);

  /* Setting the anchors */
  pentry->gc_lru = pclient->lru_gc;
  pentry->gc_lru_entry = plru_entry;

  /* Update internal md */
  /*
   * If the cache invalidate code has marked this entry as STALE,
   * don't overwrite it with VALID.
   */
  if (pentry->internal_md.valid_state != STALE)
    pentry->internal_md.valid_state = VALID;

  if(op == CACHE_INODE_OP_GET)
    pentry->internal_md.read_time = time(NULL);

  if(op == CACHE_INODE_OP_SET)
    {
      pentry->internal_md.mod_time = time(NULL);
      pentry->internal_md.refresh_time = pentry->internal_md.mod_time;
    }

  /* Add a call to the GC counter */
  pclient->call_since_last_gc++;

  /* If open/close fd cache is used for FSAL, manage it here */
    LogFullDebug(COMPONENT_CACHE_INODE_GC,
                 "--------> use_cache=%u fileno=%d last_op=%u time(NULL)=%u delta=%u retention=%u",
                 pclient->use_cache, pentry->object.file.open_fd.fileno,
                 (unsigned int)pentry->object.file.open_fd.last_op, (unsigned int)time(NULL),
                 (unsigned int)(time(NULL) - pentry->object.file.open_fd.last_op), (unsigned int)pclient->retention);

  if(pentry->internal_md.type == REGULAR_FILE)
    {
      if(pclient->use_cache == 1)
        {
          if(pentry->object.file.open_fd.fileno != 0)
            {
              if(time(NULL) - pentry->object.file.open_fd.last_op > pclient->retention)
                {
                  if(cache_inode_close(pentry, pclient, &cache_status) !=
                     CACHE_INODE_SUCCESS)
                    {
                      /* Bad close */
                      return cache_status;
                    }
                }
            }
        }

      /* Same of local fd cache */
      pclient_content = (cache_content_client_t *) pclient->pcontent_client;
      pentry_content = (cache_content_entry_t *) pentry->object.file.pentry_content;

      if(pentry_content != NULL)
        if(pclient_content->use_cache == 1)
          if(pentry_content->local_fs_entry.opened_file.local_fd > 0)
            if(time(NULL) - pentry_content->local_fs_entry.opened_file.last_op >
               pclient_content->retention)
              if(cache_content_close
                 (pentry_content, pclient_content,
                  &cache_content_status) != CACHE_CONTENT_SUCCESS)
                return CACHE_INODE_CACHE_CONTENT_ERROR;
    }


#ifndef _NO_BUDDY_SYSTEM

#if 0
  BuddyGetStats(&bstats);
  LogFullDebug(COMPONENT_CACHE_INODE,
               "(pthread_self=%u) NbStandard=%lu  NbStandardUsed=%lu  InsideStandard(nb=%lu, size=%lu)",
               (unsigned int)pthread_self(),
               (long unsigned int)bstats.NbStdPages,
               (long unsigned int)bstats.NbStdUsed,
               (long unsigned int)bstats.StdUsedSpace,
               (long unsigned int)bstats.NbStdUsed);
#endif

#endif
  LogFullDebug(COMPONENT_CACHE_INODE_GC,
               "(pthread_self=%p) LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d",
               (caddr_t)pthread_self(),
               pclient->lru_gc->nb_entry,
               pclient->lru_gc->nb_invalid,
               pclient->lru_gc->nb_call_gc,
               pclient->lru_gc->parameter.nb_call_gc_invalid);

  LogFullDebug(COMPONENT_CACHE_INODE_GC,
               "LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d",
               pclient->lru_gc->nb_entry,
               pclient->lru_gc->nb_invalid,
               pclient->lru_gc->nb_call_gc,
               pclient->lru_gc->parameter.nb_call_gc_invalid);

  LogFullDebug(COMPONENT_CACHE_INODE_GC,
               "LRU GC state: nb_entries=%d nb_invalid=%d nb_call_gc=%d param.nb_call_gc_invalid=%d",
               pclient->lru_gc->nb_entry,
               pclient->lru_gc->nb_invalid,
               pclient->lru_gc->nb_call_gc,
               pclient->lru_gc->parameter.nb_call_gc_invalid);


  /* Call LRU_gc_invalid to get ride of the unused invalid lru entries */
  if(LRU_gc_invalid(pclient->lru_gc, NULL) != LRU_LIST_SUCCESS)
    return CACHE_INODE_LRU_ERROR;

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_valid */

/**
 *
 * cache_inode_get_attributes: gets the attributes cached in the entry.
 *
 * Gets the attributes cached in the entry.
 *
 * @param pentry [IN] the entry to deal with.
 * @param pattr [OUT] the attributes for this entry.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_get_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr)
{
  /* The pentry is supposed to be locked */
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      *pattr = pentry->object.file.attributes;
      break;

    case SYMBOLIC_LINK:
      assert(pentry->object.symlink);
      *pattr = pentry->object.symlink->attributes;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      *pattr = pentry->object.dir.attributes;
      break;

    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
      *pattr = pentry->object.special_obj.attributes;
      break;

    case UNASSIGNED:
    case RECYCLED:
      memset(pattr, 0, sizeof(fsal_attrib_list_t));
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "Unexpected UNNASIGNED or RECYLCED type in cache_inode_get_attributes");
    }
}                               /* cache_inode_get_attributes */

/**
 *
 * cache_inode_init_attributes: sets the initial attributes cached in the entry.
 *
 * Sets the initial attributes cached in the entry. If NFS4 ACL is used,
 * update the reference counter of ACL.
 *
 * @param pentry [OUT] the entry to deal with.
 * @param pattr [IN] the attributes to be set for this entry.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_init_attributes( cache_entry_t       * pentry, 
                                  fsal_attrib_list_t  * pattr)
{
  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      pentry->object.file.attributes = *pattr;
      break;

    case SYMBOLIC_LINK:
      assert(pentry->object.symlink);
      pentry->object.symlink->attributes = *pattr;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      pentry->object.dir.attributes = *pattr;
      break;

    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
      pentry->object.special_obj.attributes = *pattr;
      break;
    case UNASSIGNED:
    case RECYCLED:
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "Unexpected UNNASIGNED or RECYLCED type in cache_inode_set_attributes");
      break;
    }

#ifdef _USE_NFS4_ACL
  LogDebug(COMPONENT_CACHE_INODE, "init_attributes: md_type=%d, acl=%p",
           pentry->internal_md.type, pattr->acl);

  /* Bump up reference counter of new acl. */
  if(pattr->acl)
    nfs4_acl_entry_inc_ref(pattr->acl);
#endif                          /* _USE_NFS4_ACL */
}

/**
 *
 * cache_inode_set_attributes: sets the attributes cached in the entry.
 *
 * Sets the attributes cached in the entry.
 *
 * @param pentry [OUT] the entry to deal with.
 * @param pattr [IN] the attributes to be set for this entry.
 *
 * @return nothing (void function).
 *
 */
void cache_inode_set_attributes(cache_entry_t * pentry, fsal_attrib_list_t * pattr)
{
#ifdef _USE_NFS4_ACL
  fsal_acl_t *p_oldacl = NULL;
  fsal_acl_t *p_newacl = pattr->acl;
#endif                          /* _USE_NFS4_ACL */

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
#ifdef _USE_NFS4_ACL
      p_oldacl = pentry->object.file.attributes.acl;
#endif                          /* _USE_NFS4_ACL */
      pentry->object.file.attributes = *pattr;
      break;

    case SYMBOLIC_LINK:
      assert(pentry->object.symlink);
#ifdef _USE_NFS4_ACL
      p_oldacl = pentry->object.symlink->attributes.acl;
#endif                          /* _USE_NFS4_ACL */
      pentry->object.symlink->attributes = *pattr;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
#ifdef _USE_NFS4_ACL
      p_oldacl = pentry->object.dir.attributes.acl;
#endif                          /* _USE_NFS4_ACL */
      pentry->object.dir.attributes = *pattr;
      break;

    case SOCKET_FILE:
    case FIFO_FILE:
    case BLOCK_FILE:
    case CHARACTER_FILE:
#ifdef _USE_NFS4_ACL
      p_oldacl = pentry->object.special_obj.attributes.acl;
#endif                          /* _USE_NFS4_ACL */
      pentry->object.special_obj.attributes = *pattr;
      break;
    case UNASSIGNED:
    case RECYCLED:
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "Unexpected UNNASIGNED or RECYLCED type in cache_inode_set_attributes");
      break;
    }

#ifdef _USE_NFS4_ACL
  /* If acl has been changed, release old acl and increase the reference
   * counter of new acl. */
  if(p_oldacl != p_newacl)
    {
      fsal_acl_status_t status;
      LogDebug(COMPONENT_CACHE_INODE, "acl has been changed: old acl=%p, new acl=%p",
               p_oldacl, p_newacl);

      /* Release old acl. */
      if(p_oldacl)
        {
          LogDebug(COMPONENT_CACHE_INODE, "md_type = %d, release old acl = %p",
                   pentry->internal_md.type, p_oldacl);

          nfs4_acl_release_entry(p_oldacl, &status);

          if(status != NFS_V4_ACL_SUCCESS)
            LogEvent(COMPONENT_CACHE_INODE, "Failed to release old acl, status=%d", status);
        }

      /* Bump up reference counter of new acl. */
      if(p_newacl)
        nfs4_acl_entry_inc_ref(p_newacl);
    }
#endif                          /* _USE_NFS4_ACL */
}                               /* cache_inode_set_attributes */

/**
 *
 * cache_inode_fsal_type_convert: converts an FSAL type to the cache_inode type to be used.
 *
 * Converts an FSAL type to the cache_inode type to be used.
 *
 * @param type [IN] the input FSAL type.
 *
 * @return the result of the conversion.
 *
 */
cache_inode_file_type_t cache_inode_fsal_type_convert(fsal_nodetype_t type)
{
  cache_inode_file_type_t rctype;

  switch (type)
    {
    case FSAL_TYPE_DIR:
      rctype = DIRECTORY;
      break;

    case FSAL_TYPE_FILE:
      rctype = REGULAR_FILE;
      break;

    case FSAL_TYPE_LNK:
      rctype = SYMBOLIC_LINK;
      break;

    case FSAL_TYPE_BLK:
      rctype = BLOCK_FILE;
      break;

    case FSAL_TYPE_FIFO:
      rctype = FIFO_FILE;
      break;

    case FSAL_TYPE_CHR:
      rctype = CHARACTER_FILE;
      break;

    case FSAL_TYPE_SOCK:
      rctype = SOCKET_FILE;
      break;

    case FSAL_TYPE_JUNCTION:
      rctype = FS_JUNCTION ;
      break ;

    default:
      rctype = UNASSIGNED;
      break;
    }

  return rctype;
}                               /* cache_inode_fsal_type_convert */

/**
 *
 * cache_inode_get_fsal_handle: gets the FSAL handle from a pentry.
 *
 * Gets the FSAL handle from a pentry. The entry should be lock BEFORE this
 * call is done (no lock is managed in this function).
 *
 * @param pentry [IN] the input pentry.
 * @param pstatus [OUT] the status for the extraction (If not
 * CACHE_INODE_SUCCESS, there is an error).
 *
 * @return the result of the conversion. NULL shows an error.
 *
 */
fsal_handle_t *cache_inode_get_fsal_handle(cache_entry_t * pentry,
                                           cache_inode_status_t * pstatus)
{
  fsal_handle_t *preturned_handle = NULL;

  /* Set the return default to CACHE_INODE_SUCCESS */
  *pstatus = CACHE_INODE_SUCCESS;

  if(pentry == NULL)
    {
      preturned_handle = NULL;
      *pstatus = CACHE_INODE_INVALID_ARGUMENT;
    }
  else
    {
      switch (pentry->internal_md.type)
        {
        case REGULAR_FILE:
          preturned_handle = &pentry->object.file.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case SYMBOLIC_LINK:
          assert(pentry->object.symlink);
          preturned_handle = &pentry->object.symlink->handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case DIRECTORY:
          preturned_handle = &pentry->object.dir.handle;
          *pstatus = CACHE_INODE_SUCCESS;
          break;

        case SOCKET_FILE:
        case FIFO_FILE:
        case BLOCK_FILE:
        case CHARACTER_FILE:
          preturned_handle = &pentry->object.special_obj.handle;
          break;

        default:
          preturned_handle = NULL;
          *pstatus = CACHE_INODE_BAD_TYPE;
          break;
        }                       /* switch( pentry->internal_md.type ) */
    }

  return preturned_handle;
}                               /* cache_inode_get_fsal_handle */

/**
 *
 * cache_inode_type_are_rename_compatible: test if an existing entry could be scrtached during a rename.
 *
 * test if an existing entry could be scrtached during a rename. No mutext management.
 *
 * @param pentry_src  [IN] the source pentry (the one to be renamed)
 * @param pentry_dest [IN] the dest pentry (the one to be scratched during the rename)
 *
 * @return TRUE if rename if allowed (types are compatible), FALSE if not.
 *
 */
int cache_inode_type_are_rename_compatible(cache_entry_t * pentry_src,
                                           cache_entry_t * pentry_dest)
{
  /* TRUE is both entries are non directories or to directories and the second is empty */
  if(pentry_src->internal_md.type == DIRECTORY)
    {
      if(pentry_dest->internal_md.type == DIRECTORY)
        {
          if(cache_inode_is_dir_empty(pentry_dest) == CACHE_INODE_SUCCESS)
            return TRUE;
          else
            return FALSE;
        }
      else
        return FALSE;
    }
  else
    {
      /* pentry_src is not a directory */
      if(pentry_dest->internal_md.type == DIRECTORY)
        return FALSE;
      else
        return TRUE;
    }
}                               /* cache_inode_type_are_rename_compatible */

/**
 *
 * cache_inode_mutex_destroy: destroys the pthread_mutex associated with a pentry when it is put back to the spool.
 *
 * Destroys the pthread_mutex associated with a pentry when it is put back to the spool
 *
 * @param pentry [INOUT] the input pentry.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_mutex_destroy(cache_entry_t * pentry)
{
  rw_lock_destroy(&pentry->lock);
}                               /* cache_inode_mutex_destroy */

/**
 *
 * cache_inode_print_dir: prints the content of a pentry that is a directory segment.
 *
 * Prints the content of a pentry that is a DIRECTORY.
 * /!\ This function is provided for debugging purpose only, it makes no sanity check on the arguments.
 *
 * @param pentry [IN] the input pentry.
 *
 * @return nothing (void function)
 *
 */
void cache_inode_print_dir(cache_entry_t * cache_entry_root)
{
  struct avltree_node *dirent_node;
  cache_inode_dir_entry_t *dirent;
  int i = 0;

  if(cache_entry_root->internal_md.type != DIRECTORY)
    {
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "This entry is not a directory");
      return;
    }

  dirent_node = avltree_first(&cache_entry_root->object.dir.dentries);
  do {
      dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
				    node_n);
      LogFullDebug(COMPONENT_CACHE_INODE,
		   "Name = %s, DIRECTORY entry = %p, i=%d",
		   dirent->name.name,
		   dirent->pentry,
		   i);
      i++;
  } while ((dirent_node = avltree_next(dirent_node)));

  LogFullDebug(COMPONENT_CACHE_INODE, "------------------");
}                               /* cache_inode_print_dir */

/**
 *
 * cache_inode_dump_content: dumps the content of a pentry to a local file
 * (used for File Content index files).
 *
 * Dumps the content of a pentry to a local file (used for File Content index
 * files).
 *
 * @param path [IN] the full path to the file that will contain the data.
 * @param pentry [IN] the input pentry.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related to a REGULAR_FILE
 * @return CACHE_INODE_INVALID_ARGUMENT if path is inconsistent
 * @return CACHE_INODE_SUCCESS if operation succeded.
 *
 */
cache_inode_status_t cache_inode_dump_content(char *path, cache_entry_t * pentry)
{
  FILE *stream = NULL;

  char buff[CACHE_INODE_DUMP_LEN];

  if(pentry->internal_md.type != REGULAR_FILE)
    return CACHE_INODE_BAD_TYPE;

  /* Open the index file */
  if((stream = fopen(path, "w")) == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* Dump the information */
  fprintf(stream, "internal:read_time=%d\n", (int)pentry->internal_md.read_time);
  fprintf(stream, "internal:mod_time=%d\n", (int)pentry->internal_md.mod_time);
  fprintf(stream, "internal:export_id=%d\n", 0);

  snprintHandle(buff, CACHE_INODE_DUMP_LEN, &(pentry->object.file.handle));
  fprintf(stream, "file: FSAL handle=%s", buff);

  /* Close the handle */
  fclose(stream);

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_dump_content */

/**
 *
 * cache_inode_reload_content: reloads the content of a pentry from a local file (used File Content crash recovery).
 *
 * Reloeads the content of a pentry from a local file (used File Content crash recovery).
 *
 * @param path [IN] the full path to the file that will contain the metadata.
 * @param pentry [IN] the input pentry.
 *
 * @return CACHE_INODE_BAD_TYPE if pentry is not related to a REGULAR_FILE
 * @return CACHE_INODE_SUCCESS if operation succeded.
 *
 */
cache_inode_status_t cache_inode_reload_content(char *path, cache_entry_t * pentry)
{
  FILE *stream = NULL;
  int rc;

  char buff[CACHE_INODE_DUMP_LEN+1];

  /* Open the index file */
  if((stream = fopen(path, "r")) == NULL)
    return CACHE_INODE_INVALID_ARGUMENT;

  /* The entry is a file (only file inode are dumped), in state VALID for the gc (not garbageable) */
  pentry->internal_md.type = REGULAR_FILE;
  pentry->internal_md.valid_state = VALID;

  /* BUG: what happens if the fscanf's fail? */
  /* Read the information */
  #define XSTR(s) STR(s)
  #define STR(s) #s
  rc = fscanf(stream, "internal:read_time=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);
  pentry->internal_md.read_time = atoi(buff);

  rc = fscanf(stream, "internal:mod_time=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);
  pentry->internal_md.mod_time = atoi(buff);

  rc = fscanf(stream, "internal:export_id=%" XSTR(CACHE_INODE_DUMP_LEN) "s\n", buff);

  rc = fscanf(stream, "file: FSAL handle=%" XSTR(CACHE_INODE_DUMP_LEN) "s", buff);
  #undef STR
  #undef XSTR

  if(sscanHandle(&(pentry->object.file.handle), buff) < 0)
    {
      /* expected = 2*sizeof(fsal_handle_t) in hexa representation */
      LogCrit(COMPONENT_CACHE_INODE,
              "Error recovering cache content index %s: Invalid handle length. Expected length=%u, Found=%u",
               path, (unsigned int)(2 * sizeof(fsal_handle_t)),
               (unsigned int)strlen(buff));

      fclose(stream);
      return CACHE_INODE_INCONSISTENT_ENTRY;
    }

  /* Close the handle */
  fclose(stream);

  return CACHE_INODE_SUCCESS;
}                               /* cache_inode_reload_content */

/**
 *
 * cache_inode_invalidate_dirent: unassociate a directory entry, 
 * invalidating the containing cache entry.
 *
 * Note that the dirent object linked in each parent entry is
 * necessarily unique, though it shares its name with its linked
 * friends.  This follows from the design of the avl link record.
 * From this it follows that any dirent unlinked here should be
 * reclaimed (or recycled).
 *
 * Removes directory entry association.  Cache entry is locked.
 *
 * @param pentry [INOUT] entry to be managed
 * @param cookie [IN] key of related dirent in pentry sparse avl
 * @param pclient [IN] related pclient
 *
 * @return void
 *
 */
void cache_inode_invalidate_related_dirent(
    cache_entry_t * pentry,
    uint64_t cookie,
    cache_inode_client_t * pclient)
{
    struct avltree_node *dirent_node;
    cache_inode_dir_entry_t dirent_key[1], *dirent;

    dirent_key->cookie = cookie;
    dirent_node = avltree_lookup(&dirent_key->node_c,
				 &pentry->object.dir.cookies);

    if (! dirent_node) {
	LogDebug(COMPONENT_CACHE_INODE,
		 "%s: pentry %p has no sparse node with key%"PRIu64,
		 __func__,
		 pentry,
		 cookie);
	return;
    }

    /* get shared dirent */
    dirent = avltree_container_of(dirent_node, cache_inode_dir_entry_t,
				  node_c);

    /* clear cookie offset avl */
    avltree_remove(&dirent->node_c, &pentry->object.dir.cookies);

    /* clear name avl */
    avltree_remove(&dirent->node_n, &pentry->object.dir.dentries);

    /* reclaim */
    ReleaseToPool(dirent, &pclient->pool_dir_entry);

    /* invalidate pentry */
    pentry->object.dir.has_been_readdir = CACHE_INODE_NO;
    pentry->object.dir.nbactive--; 

    return;
}

/**
 *
 * cache_inode_invalidate_related_dirents: invalidate directory entries
 * related through hard links.
 *
 * Removes directory entry associations.  Dirent is not reclaimed (but
 * may be by the caller).  Cache entry is locked.
 *
 * @param pentry [INOUT] entry to be managed
 * @param pclient [IN] related pclient
 *
 * @return void
 *
 */
void cache_inode_invalidate_related_dirents(  cache_entry_t        * pentry,
                                              cache_inode_client_t * pclient)
{
  cache_inode_parent_entry_t *parent_iter = NULL;
 
  /* Reclaim directory entries */
  for(parent_iter = pentry->parent_list; parent_iter != NULL;
      parent_iter = parent_iter->next_parent)
    {
      if(parent_iter->parent == NULL)
        {
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_gc_invalidate_related_dirent: pentry %p "
		   "has no parent, no dirent to be removed...",
                   pentry);
          continue;
        }

      /* If I reached this point, then parent_iter->parent is not null
       * and is a valid cache_inode pentry */
      P_w(&parent_iter->parent->lock);

      /* Check for type of the parent */
      if(parent_iter->parent->internal_md.type != DIRECTORY)
        {
          V_w(&parent_iter->parent->lock);
          /* Major parent incoherency: parent is not a directory */
          LogDebug(COMPONENT_CACHE_INODE,
                   "cache_inode_gc_invalidate_related_dirent: major "
		   "incoherency. Found an entry whose parent is no directory");
          return;
        }

      /* Invalidate related */
      cache_inode_invalidate_related_dirent(
	  parent_iter->parent, parent_iter->cookie, pclient);

      V_w(&parent_iter->parent->lock);
    }
}                               /* cache_inode_invalidate_related_dirent */

/**
 *
 * cache_inode_release_symlink: release an entry's symlink component, if
 * present
 *
 * releases an allocated symlink component, if any
 *
 * @param pool [INOUT] pool which owns pentry
 * @param pentry [INOUT] entry to be released
 *
 * @return  (void)
 *
 */
void cache_inode_release_symlink(cache_entry_t * pentry,
                                 struct prealloc_pool *pool)
{
    assert(pentry);
    assert(pentry->internal_md.type == SYMBOLIC_LINK);
    if (pentry->object.symlink)
     {
        ReleaseToPool(pentry->object.symlink, pool);
        pentry->object.symlink = NULL;
     }
}

/**
 *
 * cache_inode_release_dirents: release cached dirents associated
 * with an entry.
 *
 * releases an allocated symlink component, if any
 *
 * @param pentry [INOUT] entry to be released
 * @param pclient [IN] related pclient
 * @param which [INOUT] caches to clear (dense, sparse, or both)
 *
 * @return  (void)
 *
 */
void cache_inode_release_dirents( cache_entry_t           * pentry,
				  cache_inode_client_t    * pclient,
				  cache_inode_avl_which_t   which)
{
    struct avltree_node     * dirent_node      = NULL ;
    struct avltree_node     * next_dirent_node = NULL ;
    struct avltree          * tree             = NULL ;
    cache_inode_dir_entry_t * dirent           = NULL ;

    /* wont see this */
    if( pentry->internal_md.type != DIRECTORY )
	return;

    switch( which )
    {
       case CACHE_INODE_AVL_COOKIES:
          /* omit O(N) operation */
          avltree_init(&pentry->object.dir.cookies, ci_avl_dir_ck_cmp, 0 ); /* Last 0 is a flag */
	  break;

       case CACHE_INODE_AVL_NAMES:
	  tree = &pentry->object.dir.dentries;
	  dirent_node = avltree_first(tree);

	  while( dirent_node )
           {
	     next_dirent_node = avltree_next(dirent_node);
             dirent = avltree_container_of( dirent_node,
                                            cache_inode_dir_entry_t,
                                            node_n);
             avltree_remove(dirent_node, tree);
             ReleaseToPool(dirent, &pclient->pool_dir_entry);
	     dirent_node = next_dirent_node;
	   }

        pentry->object.dir.nbactive = 0;
	break;

      case CACHE_INODE_AVL_BOTH:
	cache_inode_release_dirents(pentry, pclient, CACHE_INODE_AVL_COOKIES);
	cache_inode_release_dirents(pentry, pclient, CACHE_INODE_AVL_NAMES);
	/* tree == NULL */
	break;

      default:
	/* tree == NULL */
	break;
     }
} 

/**
 *
 *  cache_inode_file_holds_state : checks if a file entry holds state(s) or not.
 *
 * Checks if a file entry holds state(s) or not.
 *
 * @param pentry [IN] entry to be checked
 *
 * @return TRUE is state(s) are held, FALSE otherwise
 *
 */
inline unsigned int cache_inode_file_holds_state( cache_entry_t * pentry )
{
  unsigned int found_state = FALSE ;

  if( pentry == NULL )
   return FALSE ;

  if( pentry->internal_md.type != REGULAR_FILE )
   return FALSE ;

   /* if locks are held in the file, do not close */
  P(pentry->object.file.lock_list_mutex);
  if(!glist_empty(&pentry->object.file.lock_list))
    {
      found_state = TRUE ;
    }
  V(pentry->object.file.lock_list_mutex);
  
  if( found_state == TRUE ) 
    return found_state ;

  if(!glist_empty(&pentry->object.file.state_list))
    return TRUE ;

  /* if this place is reached, the file holds no state */
  return FALSE ;
} /* cache_inode_file_holds_state */

#ifdef _USE_PROXY
void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr);

void cache_inode_print_srvhandle(char *comment, cache_entry_t * pentry)
{
  proxyfsal_handle_t *pfsal_handle;
  nfs_fh4 nfsfh;
  char tag[30];
  char outstr[1024];

  if(pentry == NULL)
    return;

  /* XXX infelicitous casts  */

  switch (pentry->internal_md.type)
    {
    case REGULAR_FILE:
      strcpy(tag, "file");
      pfsal_handle = (proxyfsal_handle_t *) &(pentry->object.file.handle);
      break;

    case SYMBOLIC_LINK:
      strcpy(tag, "link");
      pfsal_handle = (proxyfsal_handle_t *) &(pentry->object.symlink->handle);
      break;

    case DIRECTORY:
      strcpy(tag, "dir ");
      pfsal_handle = (proxyfsal_handle_t *) &(pentry->object.dir.handle);
      break;

    default:
      return;
      break;
    }

  nfsfh.nfs_fh4_len = pfsal_handle->data.srv_handle_len;
  nfsfh.nfs_fh4_val = pfsal_handle->data.srv_handle_val;

  nfs4_sprint_fhandle(&nfsfh, outstr);

  LogFullDebug(COMPONENT_CACHE_INODE,
               "-->-->-->-->--> External FH (%s) comment=%s = %s",
               tag, comment, outstr);
}                               /* cache_inode_print_srvhandle */
#endif
