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
cache_entry_t *
cache_inode_create(cache_entry_t * pentry_parent,
                   fsal_name_t * pname,
                   cache_inode_file_type_t type,
                   cache_inode_policy_t policy,
                   fsal_accessmode_t mode,
                   cache_inode_create_arg_t * pcreate_arg,
                   fsal_attrib_list_t * pattr,
                   hash_table_t * ht,
                   cache_inode_client_t * pclient,
                   fsal_op_context_t * pcontext,
                   cache_inode_status_t * pstatus)
{
    cache_entry_t *pentry = NULL;
    cache_inode_dir_entry_t *new_dir_entry;
    fsal_status_t fsal_status;
#ifdef _USE_MFSL
    mfsl_object_t object_handle;
    fsal_attrib_list_t parent_attributes;
#else
    fsal_handle_t object_handle;
#endif
    fsal_attrib_list_t object_attributes;
    fsal_handle_t dir_handle;
    cache_inode_fsal_data_t fsal_data;
    cache_inode_status_t status;
    struct cache_inode_dir__ *pdir = NULL;
#ifdef _USE_PNFS

#ifdef _USE_PNFS_SPNFS_LIKE  /** @todo : do the thing in a cleaner way here */
    pnfs_file_t pnfs_file ;
    pnfs_file.ds_file.allocated = FALSE ;
#endif
#endif

    memset( ( char *)&fsal_data, 0, sizeof( fsal_data ) ) ;
    memset( ( char *)&object_handle, 0, sizeof( object_handle ) ) ;

    fsal_accessflags_t access_mask = 0;

    /* Set the return default to CACHE_INODE_SUCCESS */
    *pstatus = CACHE_INODE_SUCCESS;

    /* stats */
    pclient->stat.nb_call_total += 1;
    inc_func_call(pclient, CACHE_INODE_CREATE);
    /*
     * Check if the required type is correct, with this
     * function, we manage file, dir and symlink
     */
    if(type != REGULAR_FILE && type != DIRECTORY && type != SYMBOLIC_LINK &&
       type != SOCKET_FILE && type != FIFO_FILE && type != CHARACTER_FILE &&
       type != BLOCK_FILE)
        {
            *pstatus = CACHE_INODE_BAD_TYPE;

            /* stats */
            inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);
            return NULL;
        }
    /*
     * Check if caller is allowed to perform the operation
     */
    access_mask = FSAL_MODE_MASK_SET(FSAL_W_OK) |
                  FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                                     FSAL_ACE_PERM_ADD_SUBDIRECTORY);
    status = cache_inode_access(pentry_parent,
                                access_mask, ht,
                                pclient, pcontext, &status);
    if (status != CACHE_INODE_SUCCESS)
        {
            *pstatus = status;

            /* stats */
            inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);

            /* pentry is a directory */
            return NULL;
        }
    /*
     * Check if an entry of the same name exists
     */
    pentry = cache_inode_lookup( pentry_parent,
                                 pname, 
                                 policy,
                                 &object_attributes,
                                 ht, 
                                 pclient, 
                                 pcontext, 
                                 pstatus);
    if (pentry != NULL)
        {
            *pstatus = CACHE_INODE_ENTRY_EXISTS;

            if(pentry->internal_md.type != type)
                {
                    /*
                     * Incompatible types, returns NULL
                     */
                    /* stats */
                    inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);
                    return NULL;
                }
            else
                {
                    /* stats */
                    inc_func_success(pclient, CACHE_INODE_CREATE);
                    /*
                     * redondant creation, returned the
                     * previously created entry
                     */
                    return pentry;
                }
        }
    /*
     * At this point, the entry was not found, this means
     * that it doesn't exist in FSAL, we can create it
     */
    /* Get the lock for the parent */
    P_w(&pentry_parent->lock);
    pdir = &pentry_parent->object.dir;   
    dir_handle = pdir->handle;
    object_attributes.asked_attributes = pclient->attrmask;
    switch (type)
        {
        case REGULAR_FILE:
#ifdef _USE_MFSL
            cache_inode_get_attributes(pentry_parent, &parent_attributes);
            fsal_status = MFSL_create(&pentry_parent->mobject,
                                      pname, pcontext,
                                      &pclient->mfsl_context,
                                      mode, &object_handle,
                                      &object_attributes, &parent_attributes,
#ifdef _USE_PNFS_SPNFS_LIKE   /** @todo : do the thing in a cleaner way here */
	                              &pnfs_file ) ;			      
#else
                                      NULL);
#endif /* _USE_PNFS */
#else
            fsal_status = FSAL_create(&dir_handle,
                                      pname, pcontext, mode,
                                      &object_handle, &object_attributes);
#endif
            break;

        case DIRECTORY:
#ifdef _USE_MFSL
            cache_inode_get_attributes(pentry_parent, &parent_attributes);
            fsal_status = MFSL_mkdir(&pentry_parent->mobject,
                                     pname, pcontext,
                                     &pclient->mfsl_context,
                                     mode, &object_handle,
                                     &object_attributes, &parent_attributes, NULL);
#else
            fsal_status = FSAL_mkdir(&dir_handle,
                                     pname, pcontext, mode,
                                     &object_handle, &object_attributes);
#endif
            break;

        case SYMBOLIC_LINK:
#ifdef _USE_MFSL
            cache_inode_get_attributes(pentry_parent, &object_attributes);
            fsal_status = MFSL_symlink(&pentry_parent->mobject,
                                       pname, &pcreate_arg->link_content,
                                       pcontext, &pclient->mfsl_context,
                                       mode, &object_handle, &object_attributes, NULL);
#else
            fsal_status = FSAL_symlink(&dir_handle,
                                       pname, &pcreate_arg->link_content,
                                       pcontext, mode, &object_handle,
                                       &object_attributes);
#endif
            break;

        case SOCKET_FILE:
#ifdef _USE_MFSL
            fsal_status = MFSL_mknode(&pentry_parent->mobject, pname,
                                      pcontext, &pclient->mfsl_context, mode,
                                      FSAL_TYPE_SOCK, NULL, /* no dev_t needed for socket file */
                                      &object_handle, &object_attributes, NULL);
#else
            fsal_status = FSAL_mknode(&dir_handle, pname, pcontext,
                                      mode, FSAL_TYPE_SOCK, NULL, /* no dev_t needed for socket file */
                                      &object_handle, &object_attributes);
#endif
            break;

        case FIFO_FILE:
#ifdef _USE_MFSL
            fsal_status = MFSL_mknode(&pentry_parent->mobject, pname,
                                      pcontext, &pclient->mfsl_context,
                                      mode, FSAL_TYPE_FIFO, NULL, /* no dev_t needed for FIFO file */
                                      &object_handle, &object_attributes, NULL);
#else
            fsal_status = FSAL_mknode(&dir_handle, pname, pcontext,
                                      mode, FSAL_TYPE_FIFO, NULL, /* no dev_t needed for FIFO file */
                                      &object_handle, &object_attributes);
#endif
            break;

        case BLOCK_FILE:
#ifdef _USE_MFSL
            fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                      pname, pcontext,
                                      &pclient->mfsl_context,
                                      mode, FSAL_TYPE_BLK,
                                      &pcreate_arg->dev_spec,
                                      &object_handle, &object_attributes, NULL);
#else
            fsal_status = FSAL_mknode(&dir_handle,
                                      pname, pcontext,
                                      mode, FSAL_TYPE_BLK,
                                      &pcreate_arg->dev_spec,
                                      &object_handle, &object_attributes);
#endif
            break;

        case CHARACTER_FILE:
#ifdef _USE_MFSL
            fsal_status = MFSL_mknode(&pentry_parent->mobject,
                                      pname, pcontext,
                                      &pclient->mfsl_context,
                                      mode, FSAL_TYPE_CHR,
                                      &pcreate_arg->dev_spec,
                                      &object_handle, &object_attributes, NULL);
#else
            fsal_status = FSAL_mknode(&dir_handle,
                                      pname, pcontext,
                                      mode, FSAL_TYPE_CHR,
                                      &pcreate_arg->dev_spec,
                                      &object_handle, &object_attributes);
#endif
            break;

        default:
            /* we should never go there */
            *pstatus = CACHE_INODE_INCONSISTENT_ENTRY;
            V_w(&pentry_parent->lock);

            /* stats */
            inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);

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
                    LogEvent(COMPONENT_CACHE_INODE,
                             "cache_inode_create: Stale FSAL File Handle detected for pentry = %p",
                             pentry_parent);

                    cache_inode_kill_entry(pentry_parent, NO_LOCK, ht,
                                           pclient, &kill_status);
                    if(kill_status != CACHE_INODE_SUCCESS)
                        LogCrit(COMPONENT_CACHE_INODE,
                                "cache_inode_create: Could not kill entry %p, status = %u",
                                pentry_parent, kill_status);
                    *pstatus = CACHE_INODE_FSAL_ESTALE;
                }
            /* stats */
            inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);

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

            pentry = cache_inode_new_entry( &fsal_data, 
                                            &object_attributes,
                                            type,
                                            policy,
                                            pcreate_arg, NULL,
                                            ht, 
                                            pclient, 
                                            pcontext,
                                            TRUE, /* This is a creation and not a population */
                                            pstatus);
            if (pentry == NULL)
                {
                    *pstatus = CACHE_INODE_INSERT_ERROR;
                    V_w(&pentry_parent->lock);

                    /* stats */
                    inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);
                    return NULL;
                }
#ifdef _USE_MFSL
            /* Copy the MFSL object to the cache */
            memcpy((char *)&(pentry->mobject),
                   (char *)&object_handle, sizeof(mfsl_object_t));
#endif

            /* Add this entry to the directory */
            status = cache_inode_add_cached_dirent(pentry_parent,
                                                   pname, pentry,
                                                   ht,
						   &new_dir_entry,
                                                   pclient,
						   pcontext,
                                                   pstatus);
            if (status != CACHE_INODE_SUCCESS)
                {
                    V_w(&pentry_parent->lock);

                    /* stats */
                    inc_func_err_unrecover(pclient, CACHE_INODE_CREATE);
                    return NULL;
                }
        }

#ifdef _USE_PNFS_SPNFS_LIKE /** @todo : do the thing in a cleaner way here */
       if((type == REGULAR_FILE) &&
       (pcreate_arg != NULL) &&
       (pcreate_arg->use_pnfs == TRUE))
          memcpy( (char *)&pentry->object.file.pnfs_file, (char *)&pnfs_file, sizeof( pnfs_file_t ) ) ;
#endif
       /* Update the parent cached attributes */
       cache_inode_set_time_current( &pdir->attributes.mtime ) ;
       pdir->attributes.ctime = pdir->attributes.mtime;
       /*
        * if the created object is a directory, it contains a link
        * to its parent : '..'. Thus the numlink attr must be increased.
        */
       if(type == DIRECTORY)
           {
               pdir->attributes.numlinks++;
           }
       /* Get the attributes in return */
       *pattr = object_attributes;

       /* valid the parent */
       *pstatus = cache_inode_valid(pentry_parent,
                                    CACHE_INODE_OP_SET,
                                    pclient);
       /* release the lock for the parent */
       V_w(&pentry_parent->lock);

       /* stat */
       if(*pstatus != CACHE_INODE_SUCCESS)
           inc_func_err_retryable(pclient, CACHE_INODE_CREATE);
       else
           inc_func_success(pclient, CACHE_INODE_CREATE);

       return pentry;
}

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

cache_entry_t *
cache_inode_create_open(cache_entry_t * pentry_parent,
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
}
