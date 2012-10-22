#ifndef _ERR_CACHE_INODE_H
#define _ERR_CACHE_INODE_H

#include "log.h"
#include "cache_inode.h"

/**
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

static family_error_t __attribute__ ((__unused__)) tab_errstatus_cache_inode[] =
{
#define ERR_CACHE_INODE_NO_ERROR CACHE_INODE_SUCCESS
#define ERR_CACHE_INODE_SUCCESS ERR_CACHE_INODE_NO_ERROR
  {
  ERR_CACHE_INODE_NO_ERROR, "ERR_CACHE_INODE_NO_ERROR", "No error"},
#define ERR_CACHE_INODE_MALLOC_ERROR  CACHE_INODE_MALLOC_ERROR
  {
  ERR_CACHE_INODE_MALLOC_ERROR, "ERR_CACHE_INODE_MALLOC_ERROR",
        "memory allocation error"},
#define ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR  CACHE_INODE_POOL_MUTEX_INIT_ERROR
  {
  ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR, "ERR_CACHE_INODE_POOL_MUTEX_INIT_ERROR",
        "Pool of mutexes could not be initialised"},
#define ERR_CACHE_INODE_GET_NEW_LRU_ENTRY  CACHE_INODE_GET_NEW_LRU_ENTRY
  {
  ERR_CACHE_INODE_GET_NEW_LRU_ENTRY, "ERR_CACHE_INODE_GET_NEW_LRU_ENTRY",
        "Can't get a new LRU entry"},
#define ERR_CACHE_INODE_UNAPPROPRIATED_KEY  CACHE_INODE_UNAPPROPRIATED_KEY
  {
  ERR_CACHE_INODE_UNAPPROPRIATED_KEY, "ERR_CACHE_INODE_UNAPPROPRIATED_KEY",
        "Bad hash table key"},
#define ERR_CACHE_INODE_FSAL_ERROR CACHE_INODE_FSAL_ERROR
  {
  ERR_CACHE_INODE_FSAL_ERROR, "ERR_CACHE_INODE_FSAL_ERROR", "FSAL error occured"},
#define ERR_CACHE_INODE_LRU_ERROR CACHE_INODE_LRU_ERROR
  {
  ERR_CACHE_INODE_LRU_ERROR, "ERR_CACHE_INODE_LRU_ERROR", "Unexpected LRU error"},
#define ERR_CACHE_INODE_HASH_SET_ERROR CACHE_INODE_HASH_SET_ERROR
  {
  ERR_CACHE_INODE_HASH_SET_ERROR, "ERR_CACHE_INODE_HASH_SET_ERROR",
        "Hashtable entry could not be set"},
#define ERR_CACHE_INODE_NOT_A_DIRECTORY CACHE_INODE_NOT_A_DIRECTORY
  {
  ERR_CACHE_INODE_NOT_A_DIRECTORY, "ERR_CACHE_INODE_NOT_A_DIRECTORY",
        "Entry is not a directory"},
#define ERR_CACHE_INODE_INCONSISTENT_ENTRY CACHE_INODE_INCONSISTENT_ENTRY
  {
  ERR_CACHE_INODE_INCONSISTENT_ENTRY, "ERR_CACHE_INODE_INCONSISTENT_ENTRY",
        "Entry is inconsistent"},
#define ERR_CACHE_INODE_BAD_TYPE CACHE_INODE_BAD_TYPE
  {
  ERR_CACHE_INODE_BAD_TYPE, "ERR_CACHE_INODE_BAD_TYPE",
        "Entry has not the correct type for this operation"},
#define ERR_CACHE_INODE_ENTRY_EXISTS CACHE_INODE_ENTRY_EXISTS
  {
  ERR_CACHE_INODE_ENTRY_EXISTS, "ERR_CACHE_INODE_ENTRY_EXISTS",
        "Such an entry already exists"},
#define ERR_CACHE_INODE_DIR_NOT_EMPTY CACHE_INODE_DIR_NOT_EMPTY
  {
  ERR_CACHE_INODE_DIR_NOT_EMPTY, "ERR_CACHE_INODE_DIR_NOT_EMPTY",
        "Directory is not empty"},
#define ERR_CACHE_INODE_NOT_FOUND CACHE_INODE_NOT_FOUND
  {
  ERR_CACHE_INODE_NOT_FOUND, "ERR_CACHE_INODE_NOT_FOUND", "No such entry"},
#define ERR_CACHE_INODE_INVALID_ARGUMENT CACHE_INODE_INVALID_ARGUMENT
  {
  ERR_CACHE_INODE_INVALID_ARGUMENT, "ERR_CACHE_INODE_INVALID_ARGUMENT",
        "Invalid argument"},
#define ERR_CACHE_INODE_INSERT_ERROR CACHE_INODE_INSERT_ERROR
  {
  ERR_CACHE_INODE_INSERT_ERROR, "ERR_CACHE_INODE_INSERT_ERROR", "Can't insert the entry"},
#define ERR_CACHE_INODE_HASH_TABLE_ERROR CACHE_INODE_HASH_TABLE_ERROR
  {
  ERR_CACHE_INODE_HASH_TABLE_ERROR, "ERR_CACHE_INODE_HASH_TABLE_ERROR",
        "Unexpected hash table error"},
#define ERR_CACHE_INODE_FSAL_EACCESS CACHE_INODE_FSAL_EACCESS
  {
  ERR_CACHE_INODE_FSAL_EACCESS, "ERR_CACHE_INODE_FSAL_EACCESS", "Permission denied"},
#define ERR_CACHE_INODE_IS_A_DIRECTORY CACHE_INODE_IS_A_DIRECTORY
  {
  ERR_CACHE_INODE_IS_A_DIRECTORY, "ERR_CACHE_INODE_IS_A_DIRECTORY",
        "Entry is a directory"},
#define ERR_CACHE_INODE_CACHE_CONTENT_ERROR CACHE_INODE_CACHE_CONTENT_ERROR
  {
  ERR_CACHE_INODE_CACHE_CONTENT_ERROR, "ERR_CACHE_INODE_CACHE_CONTENT_ERROR",
        "Unexpected cache content error"},
#define ERR_CACHE_INODE_NO_PERMISSION CACHE_INODE_FSAL_EPERM
  {
  ERR_CACHE_INODE_NO_PERMISSION, "ERR_CACHE_INODE_NO_PERMISSION", "Permission denied"},
#define ERR_CACHE_INODE_NO_SPACE_LEFT  CACHE_INODE_NO_SPACE_LEFT
  {
  ERR_CACHE_INODE_NO_SPACE_LEFT, "ERR_CACHE_INODE_NO_SPACE_LEFT",
        "No space left on device"},
#define ERR_CACHE_INODE_CACHE_CONTENT_EXISTS CACHE_INODE_CACHE_CONTENT_EXISTS
  {
  ERR_CACHE_INODE_CACHE_CONTENT_EXISTS, "ERR_CACHE_INODE_CACHE_CONTENT_EXISTS",
        "Cache content entry already exists"},
#define ERR_CACHE_INODE_CACHE_CONTENT_EMPTY CACHE_INODE_CACHE_CONTENT_EMPTY
  {
  ERR_CACHE_INODE_CACHE_CONTENT_EMPTY, "ERR_CACHE_INODE_CACHE_CONTENT_EMPTY",
        "No cache content entry found"},
#define ERR_CACHE_INODE_READ_ONLY_FS CACHE_INODE_READ_ONLY_FS
  {
  ERR_CACHE_INODE_READ_ONLY_FS, "ERR_CACHE_INODE_READ_ONLY_FS", "Read Only File System"},
#define ERR_CACHE_INODE_IO_ERROR CACHE_INODE_IO_ERROR
  {
  ERR_CACHE_INODE_IO_ERROR, "ERR_CACHE_INODE_IO_ERROR",
        "I/O Error on a underlying layer"},
#define ERR_CACHE_INODE_STALE_HANDLE CACHE_INODE_FSAL_ESTALE
  {
  ERR_CACHE_INODE_STALE_HANDLE, "ERR_CACHE_INODE_STALE_HANDLE",
        "Stale Filesystem Handle"},
#define ERR_CACHE_INODE_FSAL_ERR_SEC CACHE_INODE_FSAL_ERR_SEC
  {
  ERR_CACHE_INODE_FSAL_ERR_SEC, "ERR_CACHE_INODE_FSAL_ERR_SEC",
        "Security error from FSAL"},
#define ERR_CACHE_INODE_STATE_CONFLICT CACHE_INODE_STATE_CONFLICT
  {
  ERR_CACHE_INODE_STATE_CONFLICT, "ERR_CACHE_INODE_STATE_CONFLICT",
        "Conflicting file states"},
#define ERR_CACHE_INODE_QUOTA_EXCEEDED CACHE_INODE_QUOTA_EXCEEDED
  {
  ERR_CACHE_INODE_QUOTA_EXCEEDED, "ERR_CACHE_INODE_QUOTA_EXCEEDED", "Quota exceeded"},
#define ERR_CACHE_INODE_DEAD_ENTRY CACHE_INODE_DEAD_ENTRY
  {
  ERR_CACHE_INODE_DEAD_ENTRY, "ERR_CACHE_INODE_DEAD_ENTRY",
        "Trying to access a dead entry"},
#define ERR_CACHE_INODE_NOT_SUPPORTED CACHE_INODE_NOT_SUPPORTED
  {
  ERR_CACHE_INODE_NOT_SUPPORTED, "ERR_CACHE_INODE_NOT_SUPPORTED",
        "Not supported operation in FSAL"},
#define ERR_CACHE_INODE_MLINK CACHE_INODE_MLINK
  {ERR_CACHE_INODE_MLINK, "ERR_CACHE_INODE_MLINK",
   "Too many hard links."
  },
#define ERR_CACHE_INODE_SERVERFAULT CACHE_INODE_SERVERFAULT
  {ERR_CACHE_INODE_SERVERFAULT, "ERR_CACHE_INODE_SERVERFAULT",
   "The FSAL layer returned an error that can't be recovered."
  },
#define ERR_CACHE_INODE_TOOSMALL CACHE_INODE_TOOSMALL
  {ERR_CACHE_INODE_TOOSMALL, "ERR_CACHE_INODE_TOOSMALL",
   "Buffer or request is too small."
  },
#define ERR_CACHE_INODE_XDEV CACHE_INODE_XDEV
  {ERR_CACHE_INODE_XDEV, "ERR_CACHE_INODE_XDEV",
   "Attempt to do an operation between different fsids."
  },
  {
  ERR_NULL, "ERR_NULL", ""}
};

#endif                          /* _ERR_CACHE_INODE_H */
