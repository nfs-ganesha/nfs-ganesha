#ifndef _ERR_CACHE_INODE_H
#define _ERR_CACHE_INODE_H

#include "log_functions.h"
#include "cache_inode.h"

/**
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */

/**/

/**
 * \file    err_cache_inode.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:22 $
 * \version $Revision: 1.6 $
 * \brief   Cache inode error definitions.
 * 
 * err_cache_inode.h : Cache inode error definitions.
 *
 *
 */
static  family_error_t __attribute(( __unused__ ))  tab_errctx_cache_inode[] =
{
#define ERR_CACHE_INODE_ACCESS   CACHE_INODE_ACCESS    
  {ERR_CACHE_INODE_ACCESS, "ERR_CACHE_INODE_ACCESS", "cache_inode_access failed"},
#define ERR_CACHE_INODE_GETATTR  CACHE_INODE_GETATTR
  {ERR_CACHE_INODE_GETATTR, "ERR_CACHE_INODE_GETATTR", "cache_inode_getattr failed"},
#define ERR_CACHE_INODE_MKDIR     CACHE_INODE_MKDIR 
  {ERR_CACHE_INODE_MKDIR, "ERR_CACHE_INODE_MKDIR", "cache_inode_mkdir failed"},
#define ERR_CACHE_INODE_REMOVE CACHE_INODE_REMOVE
  {ERR_CACHE_INODE_REMOVE, "ERR_CACHE_INODE_REMOVE", "cache_inode_remove failed"},
#define ERR_CACHE_INODE_STATFS  CACHE_INODE_STATFS
  {ERR_CACHE_INODE_STATFS, "ERR_CACHE_INODE_STATFS", "cache_inode_statfs failed"},
#define ERR_CACHE_INODE_LINK       CACHE_INODE_LINK  
  {ERR_CACHE_INODE_LINK, "ERR_CACHE_INODE_LINK", "cache_inode_link failed"}, 
#define ERR_CACHE_INODE_READDIR  CACHE_INODE_READDIR 
  {ERR_CACHE_INODE_READDIR, "ERR_CACHE_INODE_READDIR", "cache_inode_readdir failed"}, 
#define ERR_CACHE_INODE_RENAME   CACHE_INODE_RENAME             
  {ERR_CACHE_INODE_RENAME, "ERR_CACHE_INODE_RENAME", "cache_inode_rename failed"}, 
#define ERR_CACHE_INODE_SYMLINK   CACHE_INODE_SYMLINK
  {ERR_CACHE_INODE_SYMLINK, "ERR_CACHE_INODE_SYMLINK", "cache_inode_symlink failed"},
#define ERR_CACHE_INODE_CREATE   CACHE_INODE_CREATE  
  {ERR_CACHE_INODE_CREATE, "ERR_CACHE_INODE_CREATE", "cache_inode_create failed"},
#define ERR_CACHE_INODE_LOOKUP    CACHE_INODE_LOOKUP    
  {ERR_CACHE_INODE_LOOKUP, "ERR_CACHE_INODE_LOOKUP", "cache_inode_lookup failed"},
#define ERR_CACHE_INODE_READLINK   CACHE_INODE_READLINK
  {ERR_CACHE_INODE_READLINK, "ERR_CACHE_INODE_READLINK", "cache_inode_readlink failed"},
#define ERR_CACHE_INODE_TRUNCATE  CACHE_INODE_TRUNCATE
  {ERR_CACHE_INODE_TRUNCATE, "ERR_CACHE_INODE_TRUNCATE", "cache_inode_truncate failed"},
#define ERR_CACHE_INODE_GET   CACHE_INODE_GET
  {ERR_CACHE_INODE_GET, "ERR_CACHE_INODE_GET", "cache_inode_get failed"},
#define ERR_CACHE_INODE_RELEASE   CACHE_INODE_RELEASE
  {ERR_CACHE_INODE_RELEASE, "ERR_CACHE_INODE_RELEASE", "cache_inode_release failed"},
#define ERR_CACHE_INODE_SETATTR   CACHE_INODE_SETATTR 
  {ERR_CACHE_INODE_SETATTR, "ERR_CACHE_INODE_SETATTR", "cache_inode_setattr failed"},
#define ERR_CACHE_INODE_NEW_ENTRY CACHE_INODE_NEW_ENTRY 
  {ERR_CACHE_INODE_NEW_ENTRY, "ERR_CACHE_INODE_NEW_ENTRY ", "cache_inode_new_entry failed"},
#define ERR_CACHE_INODE_READ_DATA CACHE_INODE_READ_DATA 
  {ERR_CACHE_INODE_READ_DATA, "ERR_CACHE_INODE_READ_DATA ", "cache_inode_read failed"},
#define ERR_CACHE_INODE_WRITE_DATA   CACHE_INODE_WRITE_DATA  
  {ERR_CACHE_INODE_WRITE_DATA, "ERR_CACHE_INODE_WRITE_DATA", "cache_inode_write failed"},
#define ERR_CACHE_INODE_ADD_DATA_CACHE  CACHE_INODE_ADD_DATA_CACHE
  {ERR_CACHE_INODE_ADD_DATA_CACHE, "ERR_CACHE_INODE_ADD_DATA_CACHE", "cache_inode_add_data_cache failed"},
#define ERR_CACHE_INODE_RELEASE_DATA_CACHE CACHE_INODE_RELEASE_DATA_CACHE
  {ERR_CACHE_INODE_RELEASE_DATA_CACHE, "ERR_CACHE_INODE_RELEASE_DATA_CACHE", "cache_inode_release_data_cache failed"},
#define ERR_CACHE_INODE_RENEW_ENTRY  CACHE_INODE_RENEW_ENTRY      
  {ERR_CACHE_INODE_RENEW_ENTRY, "ERR_CACHE_INODE_RENEW_ENTRY", "cache_inode_renew_entry failed"},
#define ERR_CACHE_INODE_ASYNC_POST_ERROR CACHE_INODE_ASYNC_POST_ERROR
  {ERR_CACHE_INODE_ASYNC_POST_ERROR, "ERR_CACHE_INODE_ASYNC_POST_ERROR", "cache_inode_post_async_op failed" },
#define ERR_CACHE_INODE_STATE_ERROR CACHE_INODE_STATE_ERROR
  {ERR_CACHE_INODE_STATE_ERROR, "ERR_CACHE_INODE_STATE_ERROR", "operation failed in state management" } ,
#define ERR_CACHE_INODE_FSAL_DELAY CACHE_INODE_FSAL_DELAY
  {ERR_CACHE_INODE_FSAL_DELAY, "ERR_CACHE_INODE_FSAL_DELAY", "FSAL operation was delayed" } ,

  {ERR_NULL, "ERR_NULL", ""}
};


static  family_error_t __attribute__(( __unused__ )) tab_errstatus_cache_inode[] =
{
#define ERR_CACHE_INODE_NO_ERROR CACHE_INODE_SUCCESS
#define ERR_CACHE_INODE_SUCCESS ERR_CACHE_INODE_NO_ERROR 
  {ERR_CACHE_INODE_NO_ERROR, "ERR_CACHE_INODE_NO_ERROR", "No error"},
#define ERR_CACHE_INODE_MALLOC_ERROR  CACHE_INODE_MALLOC_ERROR
  {ERR_CACHE_INODE_MALLOC_ERROR, "ERR_CACHE_INODE_MALLOC_ERROR", "memory allocation error"},
#define ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR  CACHE_INODE_POOL_MUTEX_INIT_ERROR
  {ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR, "ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR", "Pool of mutexes could not be initialised"},
#define ERR_CACHE_INODE_GET_NEW_LRU_ENTRY  CACHE_INODE_GET_NEW_LRU_ENTRY
  {ERR_CACHE_INODE_GET_NEW_LRU_ENTRY, "ERR_CACHE_INODE_GET_NEW_LRU_ENTRY", "Can't get a new LRU entry"}, 
#define ERR_CACHE_INODE_UNAPPROPRIATED_KEY  CACHE_INODE_UNAPPROPRIATED_KEY
  {ERR_CACHE_INODE_UNAPPROPRIATED_KEY, "ERR_CACHE_INODE_UNAPPROPRIATED_KEY", "Bad hash table key"}, 
#define ERR_CACHE_INODE_FSAL_ERROR CACHE_INODE_FSAL_ERROR 
  {ERR_CACHE_INODE_FSAL_ERROR, "ERR_CACHE_INODE_FSAL_ERROR", "FSAL error occured"}, 
#define ERR_CACHE_INODE_LRU_ERROR CACHE_INODE_LRU_ERROR
  {ERR_CACHE_INODE_LRU_ERROR, "ERR_CACHE_INODE_LRU_ERROR", "Unexpected LRU error"},
#define ERR_CACHE_INODE_HASH_SET_ERROR CACHE_INODE_HASH_SET_ERROR
  {ERR_CACHE_INODE_HASH_SET_ERROR, "ERR_CACHE_INODE_HASH_SET_ERROR", "Hashtable entry could not be set"}, 
#define ERR_CACHE_INODE_NOT_A_DIRECTORY CACHE_INODE_NOT_A_DIRECTORY
  {ERR_CACHE_INODE_NOT_A_DIRECTORY, "ERR_CACHE_INODE_NOT_A_DIRECTORY", "Entry is not a directory"}, 
#define ERR_CACHE_INODE_INCONSISTENT_ENTRY CACHE_INODE_INCONSISTENT_ENTRY
  {ERR_CACHE_INODE_INCONSISTENT_ENTRY, "ERR_CACHE_INODE_INCONSISTENT_ENTRY", "Entry is inconsistent"},
#define ERR_CACHE_INODE_BAD_TYPE CACHE_INODE_BAD_TYPE
  {ERR_CACHE_INODE_BAD_TYPE, "ERR_CACHE_INODE_BAD_TYPE", "Entry has not the correct type for this operation"},
#define ERR_CACHE_INODE_ENTRY_EXISTS CACHE_INODE_ENTRY_EXISTS 
  {ERR_CACHE_INODE_ENTRY_EXISTS, "ERR_CACHE_INODE_ENTRY_EXISTS", "Such an entry already exists"},
#define ERR_CACHE_INODE_DIR_NOT_EMPTY CACHE_INODE_DIR_NOT_EMPTY
  {ERR_CACHE_INODE_DIR_NOT_EMPTY, "ERR_CACHE_INODE_DIR_NOT_EMPTY", "Directory is not empty"},
#define ERR_CACHE_INODE_NOT_FOUND CACHE_INODE_NOT_FOUND
  {ERR_CACHE_INODE_NOT_FOUND, "ERR_CACHE_INODE_NOT_FOUND", "No such entry"},
#define ERR_CACHE_INODE_INVALID_ARGUMENT CACHE_INODE_INVALID_ARGUMENT 
  {ERR_CACHE_INODE_INVALID_ARGUMENT, "ERR_CACHE_INODE_INVALID_ARGUMENT", "Invalid argument"},
#define ERR_CACHE_INODE_INSERT_ERROR CACHE_INODE_INSERT_ERROR
  {ERR_CACHE_INODE_INSERT_ERROR, "ERR_CACHE_INODE_INSERT_ERROR", "Can't insert the entry"},
#define ERR_CACHE_INODE_HASH_TABLE_ERROR CACHE_INODE_HASH_TABLE_ERROR
  {ERR_CACHE_INODE_HASH_TABLE_ERROR, "ERR_CACHE_INODE_HASH_TABLE_ERROR", "Unexpected hash table error"}, 
#define ERR_CACHE_INODE_FSAL_EACCESS CACHE_INODE_FSAL_EACCESS
  {ERR_CACHE_INODE_FSAL_EACCESS, "ERR_CACHE_INODE_FSAL_EACCESS", "Permission denied"},
#define ERR_CACHE_INODE_IS_A_DIRECTORY CACHE_INODE_IS_A_DIRECTORY   
  {ERR_CACHE_INODE_IS_A_DIRECTORY, "ERR_CACHE_INODE_IS_A_DIRECTORY", "Entry is a directory"},
#define ERR_CACHE_INODE_CACHE_CONTENT_ERROR CACHE_INODE_CACHE_CONTENT_ERROR
  {ERR_CACHE_INODE_CACHE_CONTENT_ERROR, "ERR_CACHE_INODE_CACHE_CONTENT_ERROR", "Unexpected cache content error"},
#define ERR_CACHE_INODE_NO_PERMISSION CACHE_INODE_FSAL_EPERM
  {ERR_CACHE_INODE_NO_PERMISSION, "ERR_CACHE_INODE_NO_PERMISSION", "Permission denied"},
#define ERR_CACHE_INODE_NO_SPACE_LEFT  CACHE_INODE_NO_SPACE_LEFT   
  {ERR_CACHE_INODE_NO_SPACE_LEFT, "ERR_CACHE_INODE_NO_SPACE_LEFT", "No space left on device"},
#define ERR_CACHE_INODE_CACHE_CONTENT_EXISTS CACHE_INODE_CACHE_CONTENT_EXISTS
  {ERR_CACHE_INODE_CACHE_CONTENT_EXISTS, "ERR_CACHE_INODE_CACHE_CONTENT_EXISTS", "Cache content entry already exists"},
#define ERR_CACHE_INODE_CACHE_CONTENT_EMPTY CACHE_INODE_CACHE_CONTENT_EMPTY
  {ERR_CACHE_INODE_CACHE_CONTENT_EMPTY, "ERR_CACHE_INODE_CACHE_CONTENT_EMPTY", "No cache content entry found"},
#define ERR_CACHE_INODE_READ_ONLY_FS CACHE_INODE_READ_ONLY_FS
  {ERR_CACHE_INODE_READ_ONLY_FS, "ERR_CACHE_INODE_READ_ONLY_FS", "Read Only File System"}, 
#define ERR_CACHE_INODE_IO_ERROR CACHE_INODE_IO_ERROR
   {ERR_CACHE_INODE_IO_ERROR, "ERR_CACHE_INODE_IO_ERROR", "I/O Error on a underlying layer"},
#define ERR_CACHE_INODE_STALE_HANDLE CACHE_INODE_FSAL_ESTALE
   {ERR_CACHE_INODE_STALE_HANDLE, "ERR_CACHE_INODE_STALE_HANDLE", "Stale Filesystem Handle"},
#define ERR_CACHE_INODE_FSAL_ERR_SEC CACHE_INODE_FSAL_ERR_SEC
   {ERR_CACHE_INODE_FSAL_ERR_SEC, "ERR_CACHE_INODE_FSAL_ERR_SEC", "Security error from FSAL"}, 
#define ERR_CACHE_INODE_STATE_CONFLICT CACHE_INODE_STATE_CONFLICT
   {ERR_CACHE_INODE_STATE_CONFLICT, "ERR_CACHE_INODE_STATE_CONFLICT", "Conflicting file states"},
#define ERR_CACHE_INODE_QUOTA_EXCEEDED CACHE_INODE_QUOTA_EXCEEDED
   {ERR_CACHE_INODE_QUOTA_EXCEEDED, "ERR_CACHE_INODE_QUOTA_EXCEEDED", "Quota exceeded"},
#define ERR_CACHE_INODE_DEAD_ENTRY CACHE_INODE_DEAD_ENTRY
   {ERR_CACHE_INODE_DEAD_ENTRY, "ERR_CACHE_INODE_DEAD_ENTRY", "Trying to access a dead entry"},
#define ERR_CACHE_INODE_NOT_SUPPORTED CACHE_INODE_NOT_SUPPORTED
   {ERR_CACHE_INODE_NOT_SUPPORTED, "ERR_CACHE_INODE_NOT_SUPPORTED", "Not supported operation in FSAL"},
   {ERR_NULL, "ERR_NULL", ""}
};


#endif /* _ERR_CACHE_INODE_H */
