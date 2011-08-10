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

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#include "log_macros.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "sal_functions.h"

const char *state_err_str(state_status_t err)
{
  switch(err)
    {
      case STATE_SUCCESS:               return "STATE_SUCCESS";
      case STATE_MALLOC_ERROR:          return "STATE_MALLOC_ERROR";
      case STATE_POOL_MUTEX_INIT_ERROR: return "STATE_POOL_MUTEX_INIT_ERROR";
      case STATE_GET_NEW_LRU_ENTRY:     return "STATE_GET_NEW_LRU_ENTRY";
      case STATE_UNAPPROPRIATED_KEY:    return "STATE_UNAPPROPRIATED_KEY";
      case STATE_INIT_ENTRY_FAILED:     return "STATE_INIT_ENTRY_FAILED";
      case STATE_FSAL_ERROR:            return "STATE_FSAL_ERROR";
      case STATE_LRU_ERROR:             return "STATE_LRU_ERROR";
      case STATE_HASH_SET_ERROR:        return "STATE_HASH_SET_ERROR";
      case STATE_NOT_A_DIRECTORY:       return "STATE_NOT_A_DIRECTORY";
      case STATE_INCONSISTENT_ENTRY:    return "STATE_INCONSISTENT_ENTRY";
      case STATE_BAD_TYPE:              return "STATE_BAD_TYPE";
      case STATE_ENTRY_EXISTS:          return "STATE_ENTRY_EXISTS";
      case STATE_DIR_NOT_EMPTY:         return "STATE_DIR_NOT_EMPTY";
      case STATE_NOT_FOUND:             return "STATE_NOT_FOUND";
      case STATE_INVALID_ARGUMENT:      return "STATE_INVALID_ARGUMENT";
      case STATE_INSERT_ERROR:          return "STATE_INSERT_ERROR";
      case STATE_HASH_TABLE_ERROR:      return "STATE_HASH_TABLE_ERROR";
      case STATE_FSAL_EACCESS:          return "STATE_FSAL_EACCESS";
      case STATE_IS_A_DIRECTORY:        return "STATE_IS_A_DIRECTORY";
      case STATE_FSAL_EPERM:            return "STATE_FSAL_EPERM";
      case STATE_NO_SPACE_LEFT:         return "STATE_NO_SPACE_LEFT";
      case STATE_CACHE_CONTENT_ERROR:   return "STATE_CACHE_CONTENT_ERROR";
      case STATE_CACHE_CONTENT_EXISTS:  return "STATE_CACHE_CONTENT_EXISTS";
      case STATE_CACHE_CONTENT_EMPTY:   return "STATE_CACHE_CONTENT_EMPTY";
      case STATE_READ_ONLY_FS:          return "STATE_READ_ONLY_FS";
      case STATE_IO_ERROR:              return "STATE_IO_ERROR";
      case STATE_FSAL_ESTALE:           return "STATE_FSAL_ESTALE";
      case STATE_FSAL_ERR_SEC:          return "STATE_FSAL_ERR_SEC";
      case STATE_STATE_CONFLICT:        return "STATE_STATE_CONFLICT";
      case STATE_QUOTA_EXCEEDED:        return "STATE_QUOTA_EXCEEDED";
      case STATE_DEAD_ENTRY:            return "STATE_DEAD_ENTRY";
      case STATE_ASYNC_POST_ERROR:      return "STATE_ASYNC_POST_ERROR";
      case STATE_NOT_SUPPORTED:         return "STATE_NOT_SUPPORTED";
      case STATE_STATE_ERROR:           return "STATE_STATE_ERROR";
      case STATE_FSAL_DELAY:            return "STATE_FSAL_DELAY";
      case STATE_NAME_TOO_LONG:         return "STATE_NAME_TOO_LONG";
      case STATE_LOCK_CONFLICT:         return "STATE_LOCK_CONFLICT";
      case STATE_LOCK_BLOCKED:          return "STATE_LOCK_BLOCKED";
      case STATE_LOCK_DEADLOCK:         return "STATE_LOCK_DEADLOCK";
      case STATE_BAD_COOKIE:            return "STATE_BAD_COOKIE";
      case STATE_FILE_BIG:              return "STATE_FILE_BIG";
      case STATE_GRACE_PERIOD:          return "STATE_GRACE_PERIOD";
      case STATE_CACHE_INODE_ERR:       return "STATE_CACHE_INODE_ERR";
    }
  return "unknown";
}

state_status_t cache_inode_status_to_state_status(cache_inode_status_t status)
{
  switch(status)
    {
      case CACHE_INODE_SUCCESS:               return STATE_SUCCESS;
      case CACHE_INODE_MALLOC_ERROR:          return STATE_MALLOC_ERROR;
      case CACHE_INODE_POOL_MUTEX_INIT_ERROR: return STATE_POOL_MUTEX_INIT_ERROR;
      case CACHE_INODE_GET_NEW_LRU_ENTRY:     return STATE_GET_NEW_LRU_ENTRY;
      case CACHE_INODE_UNAPPROPRIATED_KEY:    return STATE_UNAPPROPRIATED_KEY;
      case CACHE_INODE_INIT_ENTRY_FAILED:     return STATE_INIT_ENTRY_FAILED;
      case CACHE_INODE_FSAL_ERROR:            return STATE_FSAL_ERROR;
      case CACHE_INODE_LRU_ERROR:             return STATE_LRU_ERROR;
      case CACHE_INODE_HASH_SET_ERROR:        return STATE_HASH_SET_ERROR;
      case CACHE_INODE_NOT_A_DIRECTORY:       return STATE_NOT_A_DIRECTORY;
      case CACHE_INODE_INCONSISTENT_ENTRY:    return STATE_INCONSISTENT_ENTRY;
      case CACHE_INODE_BAD_TYPE:              return STATE_BAD_TYPE;
      case CACHE_INODE_ENTRY_EXISTS:          return STATE_ENTRY_EXISTS;
      case CACHE_INODE_DIR_NOT_EMPTY:         return STATE_DIR_NOT_EMPTY;
      case CACHE_INODE_NOT_FOUND:             return STATE_NOT_FOUND;
      case CACHE_INODE_INVALID_ARGUMENT:      return STATE_INVALID_ARGUMENT;
      case CACHE_INODE_INSERT_ERROR:          return STATE_INSERT_ERROR;
      case CACHE_INODE_HASH_TABLE_ERROR:      return STATE_HASH_TABLE_ERROR;
      case CACHE_INODE_FSAL_EACCESS:          return STATE_FSAL_EACCESS;
      case CACHE_INODE_IS_A_DIRECTORY:        return STATE_IS_A_DIRECTORY;
      case CACHE_INODE_FSAL_EPERM:            return STATE_FSAL_EPERM;
      case CACHE_INODE_NO_SPACE_LEFT:         return STATE_NO_SPACE_LEFT;
      case CACHE_INODE_CACHE_CONTENT_ERROR:   return STATE_CACHE_CONTENT_ERROR;
      case CACHE_INODE_CACHE_CONTENT_EXISTS:  return STATE_CACHE_CONTENT_EXISTS;
      case CACHE_INODE_CACHE_CONTENT_EMPTY:   return STATE_CACHE_CONTENT_EMPTY;
      case CACHE_INODE_READ_ONLY_FS:          return STATE_READ_ONLY_FS;
      case CACHE_INODE_IO_ERROR:              return STATE_IO_ERROR;
      case CACHE_INODE_FSAL_ESTALE:           return STATE_FSAL_ESTALE;
      case CACHE_INODE_FSAL_ERR_SEC:          return STATE_FSAL_ERR_SEC;
      case CACHE_INODE_STATE_CONFLICT:        return STATE_STATE_CONFLICT;
      case CACHE_INODE_QUOTA_EXCEEDED:        return STATE_QUOTA_EXCEEDED;
      case CACHE_INODE_DEAD_ENTRY:            return STATE_DEAD_ENTRY;
      case CACHE_INODE_ASYNC_POST_ERROR:      return STATE_ASYNC_POST_ERROR;
      case CACHE_INODE_NOT_SUPPORTED:         return STATE_NOT_SUPPORTED;
      case CACHE_INODE_STATE_ERROR:           return STATE_STATE_ERROR;
      case CACHE_INODE_FSAL_DELAY:            return STATE_FSAL_DELAY;
      case CACHE_INODE_NAME_TOO_LONG:         return STATE_NAME_TOO_LONG;
      case CACHE_INODE_LOCK_CONFLICT:         return STATE_LOCK_CONFLICT;
      case CACHE_INODE_LOCK_BLOCKED:          return STATE_LOCK_BLOCKED;
      case CACHE_INODE_LOCK_DEADLOCK:         return STATE_LOCK_DEADLOCK;
      case CACHE_INODE_BAD_COOKIE:            return STATE_BAD_COOKIE;
      case CACHE_INODE_FILE_BIG:              return STATE_FILE_BIG;
      case CACHE_INODE_GRACE_PERIOD:          return STATE_GRACE_PERIOD;
    }
  return STATE_CACHE_INODE_ERR;
}

/**
 *
 * state_error_convert: converts an FSAL error to the corresponding state error.
 *
 * Converts an FSAL error to the corresponding state error.
 *
 * @param fsal_status [IN] fsal error to be converted.
 *
 * @return the result of the conversion.
 *
 */
state_status_t state_error_convert(fsal_status_t fsal_status)
{
  switch (fsal_status.major)
    {
    case ERR_FSAL_NO_ERROR:
      return STATE_SUCCESS;

    case ERR_FSAL_NOENT:
      return STATE_NOT_FOUND;

    case ERR_FSAL_EXIST:
      return STATE_ENTRY_EXISTS;

    case ERR_FSAL_ACCESS:
      return STATE_FSAL_EACCESS;

    case ERR_FSAL_PERM:
      return STATE_FSAL_EPERM;

    case ERR_FSAL_NOSPC:
      return STATE_NO_SPACE_LEFT;

    case ERR_FSAL_NOTEMPTY:
      return STATE_DIR_NOT_EMPTY;

    case ERR_FSAL_ROFS:
      return STATE_READ_ONLY_FS;

    case ERR_FSAL_NOTDIR:
      return STATE_NOT_A_DIRECTORY;

    case ERR_FSAL_IO:
    case ERR_FSAL_NXIO:
      return STATE_IO_ERROR;

    case ERR_FSAL_STALE:
    case ERR_FSAL_BADHANDLE:
    case ERR_FSAL_FHEXPIRED:
      return STATE_FSAL_ESTALE;

    case ERR_FSAL_INVAL:
    case ERR_FSAL_OVERFLOW:
      return STATE_INVALID_ARGUMENT;

    case ERR_FSAL_DQUOT:
      return STATE_QUOTA_EXCEEDED;

    case ERR_FSAL_SEC:
      return STATE_FSAL_ERR_SEC;

    case ERR_FSAL_NOTSUPP:
    case ERR_FSAL_ATTRNOTSUPP:
      return STATE_NOT_SUPPORTED;

    case ERR_FSAL_DELAY:
      return STATE_FSAL_DELAY;

    case ERR_FSAL_NAMETOOLONG:
      return STATE_NAME_TOO_LONG;

    case ERR_FSAL_NOMEM:
      return STATE_MALLOC_ERROR;

    case ERR_FSAL_DEADLOCK:
      return STATE_LOCK_DEADLOCK;

    case ERR_FSAL_BADCOOKIE:
      return STATE_BAD_COOKIE;

    case ERR_FSAL_NOT_OPENED:
      LogDebug(COMPONENT_STATE,
               "Conversion of ERR_FSAL_NOT_OPENED to STATE_FSAL_ERROR");
      return STATE_FSAL_ERROR;

    case ERR_FSAL_SYMLINK:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_BADTYPE:
      return STATE_BAD_TYPE;

    case ERR_FSAL_FBIG:
      return STATE_FILE_BIG;

    case ERR_FSAL_BLOCKED:
      return STATE_LOCK_BLOCKED;

    case ERR_FSAL_INTERRUPT:
    case ERR_FSAL_FAULT:
    case ERR_FSAL_NOT_INIT:
    case ERR_FSAL_ALREADY_INIT:
    case ERR_FSAL_BAD_INIT:
    case ERR_FSAL_NO_QUOTA:
    case ERR_FSAL_XDEV:
    case ERR_FSAL_MLINK:
    case ERR_FSAL_TOOSMALL:
    case ERR_FSAL_SERVERFAULT:
      /* These errors should be handled inside Cache Inode (or should never be seen by Cache Inode) */
      LogDebug(COMPONENT_STATE,
               "Conversion of FSAL error %d,%d to STATE_FSAL_ERROR",
               fsal_status.major, fsal_status.minor);
      return STATE_FSAL_ERROR;
    }

  /* We should never reach this line, this may produce a warning with certain compiler */
  LogCrit(COMPONENT_STATE,
          "state_error_convert: default conversion to STATE_FSAL_ERROR for error %d, line %u should never be reached",
           fsal_status.major, __LINE__);
  return STATE_FSAL_ERROR;
}                               /* state_error_convert */
