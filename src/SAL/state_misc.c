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
 * \File    state_misc.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:14:51 $
 * \version $Revision: 1.63 $
 * \brief   Some routines for management of the state abstraction layer, shared by other calls.
 *
 * state_misc.c : Some routines for management of the state abstraction layer, shared by other calls.
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
      case CACHE_INODE_BAD_COOKIE:            return STATE_BAD_COOKIE;
      case CACHE_INODE_FILE_BIG:              return STATE_FILE_BIG;
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

    case ERR_FSAL_DELAY:
    case ERR_FSAL_ACCESS:
      /* EDELAY and EACCESS are documented by fcntl as
       * indicating lock conflict
       */
      return STATE_LOCK_CONFLICT;

    case ERR_FSAL_PERM:
      return STATE_FSAL_EPERM;

    case ERR_FSAL_NOSPC:
      return STATE_NO_SPACE_LEFT;

    case ERR_FSAL_ROFS:
      return STATE_READ_ONLY_FS;

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

    case ERR_FSAL_SEC:
      return STATE_FSAL_ERR_SEC;

    case ERR_FSAL_NOTSUPP:
    case ERR_FSAL_ATTRNOTSUPP:
      return STATE_NOT_SUPPORTED;

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

    case ERR_FSAL_DQUOT:
    case ERR_FSAL_NAMETOOLONG:
    case ERR_FSAL_EXIST:
    case ERR_FSAL_NOTEMPTY:
    case ERR_FSAL_NOTDIR:
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
      /* These errors should be handled inside state (or should never be seen by state) */
      LogDebug(COMPONENT_STATE,
               "Conversion of FSAL error %d,%d to STATE_FSAL_ERROR",
               fsal_status.major, fsal_status.minor);
      return STATE_FSAL_ERROR;
    }

  /* We should never reach this line, this may produce a warning with certain compiler */
  LogCrit(COMPONENT_STATE,
          "Default conversion to STATE_FSAL_ERROR for error %d, line %u should never be reached",
           fsal_status.major, __LINE__);
  return STATE_FSAL_ERROR;
}                               /* state_error_convert */

/* Error conversion routines */
/**
 *
 * nfs4_Errno: Converts a state status to a nfsv4 status.
 *
 * @param error  [IN] Input state error.
 *
 * @return the converted NFSv4 status.
 *
 */
nfsstat4 nfs4_Errno_state(state_status_t error)
{
  nfsstat4 nfserror= NFS4ERR_INVAL;

  switch (error)
    {
    case STATE_SUCCESS:
      nfserror = NFS4_OK;
      break;

    case STATE_MALLOC_ERROR:
      nfserror = NFS4ERR_RESOURCE;
      break;

    case STATE_POOL_MUTEX_INIT_ERROR:
    case STATE_GET_NEW_LRU_ENTRY:
    case STATE_INIT_ENTRY_FAILED:
    case STATE_CACHE_CONTENT_EXISTS:
    case STATE_CACHE_CONTENT_EMPTY:
      nfserror = NFS4ERR_SERVERFAULT;
      break;

    case STATE_UNAPPROPRIATED_KEY:
      nfserror = NFS4ERR_BADHANDLE;
      break;

    case STATE_BAD_TYPE:
      nfserror = NFS4ERR_INVAL;
      break;

    case STATE_INVALID_ARGUMENT:
      nfserror = NFS4ERR_PERM;
      break;

    case STATE_NOT_A_DIRECTORY:
      nfserror = NFS4ERR_NOTDIR;
      break;

    case STATE_ENTRY_EXISTS:
      nfserror = NFS4ERR_EXIST;
      break;

    case STATE_DIR_NOT_EMPTY:
      nfserror = NFS4ERR_NOTEMPTY;
      break;

    case STATE_NOT_FOUND:
      nfserror = NFS4ERR_NOENT;
      break;

    case STATE_FSAL_ERROR:
    case STATE_INSERT_ERROR:
    case STATE_LRU_ERROR:
    case STATE_HASH_SET_ERROR:
      nfserror = NFS4ERR_IO;
      break;

    case STATE_FSAL_EACCESS:
      nfserror = NFS4ERR_ACCESS;
      break;

    case STATE_FSAL_EPERM:
    case STATE_FSAL_ERR_SEC:
      nfserror = NFS4ERR_PERM;
      break;

    case STATE_NO_SPACE_LEFT:
      nfserror = NFS4ERR_NOSPC;
      break;

    case STATE_IS_A_DIRECTORY:
      nfserror = NFS4ERR_ISDIR;
      break;

    case STATE_READ_ONLY_FS:
      nfserror = NFS4ERR_ROFS;
      break;

    case STATE_IO_ERROR:
      nfserror = NFS4ERR_IO;
      break;

     case STATE_NAME_TOO_LONG:
      nfserror = NFS4ERR_NAMETOOLONG;
      break;

    case STATE_DEAD_ENTRY:
    case STATE_FSAL_ESTALE:
      nfserror = NFS4ERR_STALE;
      break;

    case STATE_STATE_CONFLICT:
      nfserror = NFS4ERR_PERM;
      break;

    case STATE_QUOTA_EXCEEDED:
      nfserror = NFS4ERR_DQUOT;
      break;

    case STATE_NOT_SUPPORTED:
      nfserror = NFS4ERR_NOTSUPP;
      break;

    case STATE_FSAL_DELAY:
      nfserror = NFS4ERR_DELAY;
      break;

    case STATE_FILE_BIG:
      nfserror = NFS4ERR_FBIG;
      break;

    case STATE_LOCK_DEADLOCK:
      nfserror = NFS4ERR_DEADLOCK;
      break;

    case STATE_LOCK_BLOCKED:
    case STATE_LOCK_CONFLICT:
      nfserror = NFS4ERR_DENIED;
      break;

    case STATE_STATE_ERROR:
      nfserror = NFS4ERR_BAD_STATEID;
      break;

    case STATE_BAD_COOKIE:
      nfserror = NFS4ERR_BAD_COOKIE;
      break;

    case STATE_GRACE_PERIOD:
      nfserror = NFS4ERR_GRACE;
      break;

    case STATE_CACHE_INODE_ERR:
    case STATE_INCONSISTENT_ENTRY:
    case STATE_HASH_TABLE_ERROR:
    case STATE_CACHE_CONTENT_ERROR:
    case STATE_ASYNC_POST_ERROR:
      /* Should not occur */
      nfserror = NFS4ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs4_Errno_state */

/**
 *
 * nfs3_Errno_state: Converts a state status to a nfsv3 status.
 *
 * @param error  [IN] Input state error.
 *
 * @return the converted NFSv3 status.
 *
 */
nfsstat3 nfs3_Errno_state(state_status_t error)
{
  nfsstat3 nfserror= NFS3ERR_INVAL;

  switch (error)
    {
    case STATE_SUCCESS:
      nfserror = NFS3_OK;
      break;

    case STATE_MALLOC_ERROR:
    case STATE_POOL_MUTEX_INIT_ERROR:
    case STATE_GET_NEW_LRU_ENTRY:
    case STATE_UNAPPROPRIATED_KEY:
    case STATE_INIT_ENTRY_FAILED:
    case STATE_CACHE_CONTENT_EXISTS:
    case STATE_CACHE_CONTENT_EMPTY:
    case STATE_INSERT_ERROR:
    case STATE_LRU_ERROR:
    case STATE_HASH_SET_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFS3ERR_IO but was set non-retryable",
              error);
      nfserror = NFS3ERR_IO;
      break;

    case STATE_INVALID_ARGUMENT:
      nfserror = NFS3ERR_INVAL;
      break;

    case STATE_FSAL_ERROR:
    case STATE_CACHE_CONTENT_ERROR:
                                         /** @todo: Check if this works by making stress tests */
      LogCrit(COMPONENT_NFSPROTO,
              "Error STATE_FSAL_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case STATE_NOT_A_DIRECTORY:
      nfserror = NFS3ERR_NOTDIR;
      break;

    case STATE_ENTRY_EXISTS:
      nfserror = NFS3ERR_EXIST;
      break;

    case STATE_DIR_NOT_EMPTY:
      nfserror = NFS3ERR_NOTEMPTY;
      break;

    case STATE_NOT_FOUND:
      nfserror = NFS3ERR_NOENT;
      break;

    case STATE_FSAL_EACCESS:
      nfserror = NFS3ERR_ACCES;
      break;

    case STATE_FSAL_EPERM:
    case STATE_FSAL_ERR_SEC:
      nfserror = NFS3ERR_PERM;
      break;

    case STATE_NO_SPACE_LEFT:
      nfserror = NFS3ERR_NOSPC;
      break;

    case STATE_IS_A_DIRECTORY:
      nfserror = NFS3ERR_ISDIR;
      break;

    case STATE_READ_ONLY_FS:
      nfserror = NFS3ERR_ROFS;
      break;

    case STATE_DEAD_ENTRY:
    case STATE_FSAL_ESTALE:
      nfserror = NFS3ERR_STALE;
      break;

    case STATE_QUOTA_EXCEEDED:
      nfserror = NFS3ERR_DQUOT;
      break;

    case STATE_BAD_TYPE:
      nfserror = NFS3ERR_BADTYPE;
      break;

    case STATE_NOT_SUPPORTED:
      nfserror = NFS3ERR_NOTSUPP;
      break;

    case STATE_FSAL_DELAY:
      nfserror = NFS3ERR_JUKEBOX;
      break;

    case STATE_IO_ERROR:
        LogCrit(COMPONENT_NFSPROTO,
                "Error STATE_IO_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case STATE_NAME_TOO_LONG:
      nfserror = NFS3ERR_NAMETOOLONG;
      break;

    case STATE_FILE_BIG:
      nfserror = NFS3ERR_FBIG;
      break;

    case STATE_BAD_COOKIE:
      nfserror = NFS3ERR_BAD_COOKIE;
      break;

    case STATE_CACHE_INODE_ERR:
    case STATE_INCONSISTENT_ENTRY:
    case STATE_HASH_TABLE_ERROR:
    case STATE_STATE_CONFLICT:
    case STATE_ASYNC_POST_ERROR:
    case STATE_STATE_ERROR:
    case STATE_LOCK_CONFLICT:
    case STATE_LOCK_BLOCKED:
    case STATE_LOCK_DEADLOCK:
    case STATE_GRACE_PERIOD:
        /* Should not occur */
        LogDebug(COMPONENT_NFSPROTO,
                 "Unexpected status for conversion = %s",
                 state_err_str(error));
      nfserror = NFS3ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs3_Errno_state */

/**
 *
 * nfs2_Errno_state: Converts a state status to a nfsv2 status.
 *
 * @param error  [IN] Input state error.
 *
 * @return the converted NFSv2 status.
 *
 */
nfsstat2 nfs2_Errno_state(state_status_t error)
{
  nfsstat2 nfserror= NFSERR_IO;

  switch (error)
    {
    case STATE_SUCCESS:
      nfserror = NFS_OK;
      break;

    case STATE_MALLOC_ERROR:
    case STATE_POOL_MUTEX_INIT_ERROR:
    case STATE_GET_NEW_LRU_ENTRY:
    case STATE_UNAPPROPRIATED_KEY:
    case STATE_INIT_ENTRY_FAILED:
    case STATE_BAD_TYPE:
    case STATE_CACHE_CONTENT_EXISTS:
    case STATE_CACHE_CONTENT_EMPTY:
    case STATE_INSERT_ERROR:
    case STATE_LRU_ERROR:
    case STATE_HASH_SET_ERROR:
    case STATE_INVALID_ARGUMENT:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFSERR_IO but was set non-retryable",
              error);
      nfserror = NFSERR_IO;
      break;

    case STATE_NOT_A_DIRECTORY:
      nfserror = NFSERR_NOTDIR;
      break;

    case STATE_ENTRY_EXISTS:
      nfserror = NFSERR_EXIST;
      break;

    case STATE_FSAL_ERROR:
    case STATE_CACHE_CONTENT_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error STATE_FSAL_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case STATE_DIR_NOT_EMPTY:
      nfserror = NFSERR_NOTEMPTY;
      break;

    case STATE_NOT_FOUND:
      nfserror = NFSERR_NOENT;
      break;

    case STATE_FSAL_EACCESS:
      nfserror = NFSERR_ACCES;
      break;

    case STATE_NO_SPACE_LEFT:
      nfserror = NFSERR_NOSPC;
      break;

    case STATE_FSAL_EPERM:
    case STATE_FSAL_ERR_SEC:
      nfserror = NFSERR_PERM;
      break;

    case STATE_IS_A_DIRECTORY:
      nfserror = NFSERR_ISDIR;
      break;

    case STATE_READ_ONLY_FS:
      nfserror = NFSERR_ROFS;
      break;

    case STATE_DEAD_ENTRY:
    case STATE_FSAL_ESTALE:
      nfserror = NFSERR_STALE;
      break;

    case STATE_QUOTA_EXCEEDED:
      nfserror = NFSERR_DQUOT;
      break;

    case STATE_IO_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error STATE_IO_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case STATE_NAME_TOO_LONG:
      nfserror = NFSERR_NAMETOOLONG;
      break;

    case STATE_CACHE_INODE_ERR:
    case STATE_INCONSISTENT_ENTRY:
    case STATE_HASH_TABLE_ERROR:
    case STATE_STATE_CONFLICT:
    case STATE_ASYNC_POST_ERROR:
    case STATE_STATE_ERROR:
    case STATE_LOCK_CONFLICT:
    case STATE_LOCK_BLOCKED:
    case STATE_LOCK_DEADLOCK:
    case STATE_NOT_SUPPORTED:
    case STATE_FSAL_DELAY:
    case STATE_BAD_COOKIE:
    case STATE_FILE_BIG:
    case STATE_GRACE_PERIOD:
        /* Should not occur */
        LogDebug(COMPONENT_NFSPROTO,
                 "Unexpected conversion for status = %s",
                 state_err_str(error));
      nfserror = NFSERR_IO;
      break;
    }

  return nfserror;
}                               /* nfs2_Errno_state */

const char * invalid_state_owner_type = "INVALID STATE OWNER TYPE";

const char * state_owner_type_to_str(state_owner_type_t type)
{
  switch(type)
    {
      case STATE_LOCK_OWNER_UNKNOWN: return "STATE_LOCK_OWNER_UNKNOWN";
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:     return "STATE_LOCK_OWNER_NLM";
#endif
      case STATE_OPEN_OWNER_NFSV4:   return "STATE_OPEN_OWNER_NFSV4";
      case STATE_LOCK_OWNER_NFSV4:   return "STATE_LOCK_OWNER_NFSV4";
    }
  return invalid_state_owner_type;
}

int different_owners(state_owner_t *powner1, state_owner_t *powner2)
{
  if(powner1 == NULL || powner2 == NULL)
    return 1;

  /* Shortcut in case we actually are pointing to the same owner structure */
  if(powner1 == powner2)
    return 0;

  if(powner1->so_type != powner2->so_type)
    return 1;

  switch(powner1->so_type)
    {
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:
         if(powner2->so_type != STATE_LOCK_OWNER_NLM)
           return 1;
        return compare_nlm_owner(powner1, powner2);
#endif
      case STATE_OPEN_OWNER_NFSV4:
      case STATE_LOCK_OWNER_NFSV4:
         if(powner2->so_type != STATE_OPEN_OWNER_NFSV4 &&
            powner2->so_type != STATE_LOCK_OWNER_NFSV4)
           return 1;
        return compare_nfs4_owner(powner1, powner2);

      case STATE_LOCK_OWNER_UNKNOWN:
        break;
    }

  return 1;
}

int DisplayOwner(state_owner_t *powner, char *buf)
{
  if(powner != NULL)
    switch(powner->so_type)
      {
#ifdef _USE_NLM
        case STATE_LOCK_OWNER_NLM:
          return display_nlm_owner(powner, buf);
#endif

        case STATE_OPEN_OWNER_NFSV4:
        case STATE_LOCK_OWNER_NFSV4:
          return display_nfs4_owner(powner, buf);

        case STATE_LOCK_OWNER_UNKNOWN:
          return sprintf(buf,
                         "%s powner=%p: refcount=%d",
                         state_owner_type_to_str(powner->so_type), powner, powner->so_refcount);
    }

  return sprintf(buf, "%s", invalid_state_owner_type);
}

int Hash_dec_state_owner_ref(hash_buffer_t *buffval)
{
  int rc;
  state_owner_t *powner = (state_owner_t *)(buffval->pdata);

  P(powner->so_mutex);

  powner->so_refcount--;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount for {%s}",
                   str);
    }

  rc = powner->so_refcount;

  V(powner->so_mutex);

  return rc;
}

void Hash_inc_state_owner_ref(hash_buffer_t *buffval)
{
  state_owner_t *powner = (state_owner_t *)(buffval->pdata);

  P(powner->so_mutex);
  powner->so_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount for {%s}",
                   str);
    }

  V(powner->so_mutex);
}

void inc_state_owner_ref_locked(state_owner_t *powner)
{
  powner->so_refcount++;

  if(isFullDebug(COMPONENT_STATE))
    {
      char str[HASHTABLE_DISPLAY_STRLEN];

      DisplayOwner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "Increment refcount for {%s}",
                   str);
    }

  V(powner->so_mutex);
}

void inc_state_owner_ref(state_owner_t *powner)
{
  P(powner->so_mutex);

  inc_state_owner_ref_locked(powner);
}

void dec_state_owner_ref_locked(state_owner_t        * powner,
                                cache_inode_client_t * pclient)
{
  bool_t remove = FALSE;
  char   str[HASHTABLE_DISPLAY_STRLEN];

  if(isDebug(COMPONENT_STATE))
    DisplayOwner(powner, str);

  if(powner->so_refcount > 1)
    {
      powner->so_refcount--;

      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount for {%s}",
                   str);
    }
  else
    {
      LogFullDebug(COMPONENT_STATE,
                   "Refcount for {%s} is 1",
                   str);
      remove = TRUE;
    }

  V(powner->so_mutex);

  if(remove)
    {
      switch(powner->so_type)
        {
#ifdef _USE_NLM
          case STATE_LOCK_OWNER_NLM:
            remove_nlm_owner(pclient, powner, str);
            break;
#endif

          case STATE_OPEN_OWNER_NFSV4:
          case STATE_LOCK_OWNER_NFSV4:
            remove_nfs4_owner(pclient, powner, str);
            break;

          case STATE_LOCK_OWNER_UNKNOWN:
            LogDebug(COMPONENT_STATE,
                     "Unexpected removal of powner=%p: %s",
                     powner, str);
            break;
        }
    }
}

void dec_state_owner_ref(state_owner_t        * powner,
                         cache_inode_client_t * pclient)
{
  P(powner->so_mutex);

  dec_state_owner_ref_locked(powner, pclient);
}

