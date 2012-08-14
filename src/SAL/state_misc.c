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

#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "sal_functions.h"

pool_t *state_owner_pool; /*< Pool for NFSv4 files's open owner */

#ifdef _DEBUG_MEMLEAKS
struct glist_head state_owners_all;
pthread_mutex_t all_state_owners_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

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
      case STATE_SIGNAL_ERROR:          return "STATE_SIGNAL_ERROR";
      case STATE_KILLED:                return "STATE_KILLED";
      case STATE_FILE_OPEN:             return "STATE_FILE_OPEN";
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
      case CACHE_INODE_DELAY:                 return STATE_FSAL_DELAY;
      case CACHE_INODE_NAME_TOO_LONG:         return STATE_NAME_TOO_LONG;
      case CACHE_INODE_BAD_COOKIE:            return STATE_BAD_COOKIE;
      case CACHE_INODE_FILE_BIG:              return STATE_FILE_BIG;
      case CACHE_INODE_KILLED:                return STATE_KILLED;
      case CACHE_INODE_FILE_OPEN:             return STATE_FILE_OPEN;
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
      LogCrit(COMPONENT_STATE,
              "Conversion of ERR_FSAL_NOT_OPENED to STATE_FSAL_ERROR");
      return STATE_FSAL_ERROR;

    case ERR_FSAL_SYMLINK:
    case ERR_FSAL_ISDIR:
    case ERR_FSAL_BADTYPE:
      return STATE_BAD_TYPE;

    case ERR_FSAL_FBIG:
      return STATE_FILE_BIG;

    case ERR_FSAL_FILE_OPEN:
      return STATE_FILE_OPEN;

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

    case STATE_FILE_OPEN:
      nfserror = NFS4ERR_FILE_OPEN;
      break;

     case STATE_NAME_TOO_LONG:
      nfserror = NFS4ERR_NAMETOOLONG;
      break;

    case STATE_KILLED:
    case STATE_DEAD_ENTRY:
    case STATE_FSAL_ESTALE:
      nfserror = NFS4ERR_STALE;
      break;

    case STATE_STATE_CONFLICT:
      nfserror = NFS4ERR_SHARE_DENIED;
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

    case STATE_INVALID_ARGUMENT:
    case STATE_CACHE_INODE_ERR:
    case STATE_INCONSISTENT_ENTRY:
    case STATE_HASH_TABLE_ERROR:
    case STATE_CACHE_CONTENT_ERROR:
    case STATE_ASYNC_POST_ERROR:
    case STATE_SIGNAL_ERROR:
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
    case STATE_FILE_OPEN:
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

    case STATE_KILLED:
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
    case STATE_SIGNAL_ERROR:
        /* Should not occur */
        LogCrit(COMPONENT_NFSPROTO,
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

    case STATE_KILLED:
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
    case STATE_SIGNAL_ERROR:
    case STATE_FILE_OPEN:
        /* Should not occur */
        LogCrit(COMPONENT_NFSPROTO,
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
      case STATE_LOCK_OWNER_UNKNOWN:     return "STATE_LOCK_OWNER_UNKNOWN";
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:         return "STATE_LOCK_OWNER_NLM";
#endif
#ifdef _USE_9P
      case STATE_LOCK_OWNER_9P:          return "STALE_LOCK_OWNER_9P";
#endif
      case STATE_OPEN_OWNER_NFSV4:       return "STATE_OPEN_OWNER_NFSV4";
      case STATE_LOCK_OWNER_NFSV4:       return "STATE_LOCK_OWNER_NFSV4";
      case STATE_CLIENTID_OWNER_NFSV4:   return "STATE_CLIENTID_OWNER_NFSV4";
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
#ifdef _USE_9P
      case STATE_LOCK_OWNER_9P:
        if(powner2->so_type != STATE_LOCK_OWNER_9P)
           return 1;
        return compare_9p_owner(powner1, powner2);
#endif
      case STATE_OPEN_OWNER_NFSV4:
      case STATE_LOCK_OWNER_NFSV4:
      case STATE_CLIENTID_OWNER_NFSV4:
        if(powner1->so_type != powner2->so_type)
          return 1;
        return compare_nfs4_owner(powner1, powner2);

      case STATE_LOCK_OWNER_UNKNOWN:
        break;
    }

  return 1;
}

int DisplayOwner(state_owner_t *powner, char *buf)
{
  if(powner == NULL)
    return sprintf(buf, "<NULL>");

  switch(powner->so_type)
    {
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:
        return display_nlm_owner(powner, buf);
#endif
#ifdef _USE_9P
      case STATE_LOCK_OWNER_9P:
        return display_9p_owner(powner, buf);
#endif

      case STATE_OPEN_OWNER_NFSV4:
      case STATE_LOCK_OWNER_NFSV4:
      case STATE_CLIENTID_OWNER_NFSV4:
        return display_nfs4_owner(powner, buf);

      case STATE_LOCK_OWNER_UNKNOWN:
        return sprintf(buf,
                       "%s powner=%p: refcount=%d",
                       state_owner_type_to_str(powner->so_type),
                       powner,
                       atomic_fetch_int32_t(&powner->so_refcount));
    }

  return sprintf(buf, "%s", invalid_state_owner_type);
}

void inc_state_owner_ref(state_owner_t * powner)
{
  char    str[HASHTABLE_DISPLAY_STRLEN];
  int32_t refcount;

  if(isDebug(COMPONENT_STATE))
    DisplayOwner(powner, str);

  refcount = atomic_inc_int32_t(&powner->so_refcount);

  LogFullDebug(COMPONENT_STATE,
               "Increment refcount now=%"PRId32" {%s}",
               refcount, str);
}

void free_state_owner(state_owner_t * powner)
{
  char str[HASHTABLE_DISPLAY_STRLEN];

  switch(powner->so_type)
    {
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:
        free_nlm_owner(powner);
        break;
#endif

#ifdef _USE_9P
      case STATE_LOCK_OWNER_9P:
        break;
#endif

      case STATE_OPEN_OWNER_NFSV4:
      case STATE_LOCK_OWNER_NFSV4:
      case STATE_CLIENTID_OWNER_NFSV4:
        free_nfs4_owner(powner);
        break;

      case STATE_LOCK_OWNER_UNKNOWN:
        DisplayOwner(powner, str);

        LogCrit(COMPONENT_STATE,
                "Unexpected removal of {%s}",
                str);
        return;
    }

  if(powner->so_owner_val != NULL)
    gsh_free(powner->so_owner_val);

  pthread_mutex_destroy(&powner->so_mutex);

#ifdef _DEBUG_MEMLEAKS
  P(all_state_owners_mutex);

  glist_del(&powner->sle_all_owners);

  V(all_state_owners_mutex);
#endif

  pool_free(state_owner_pool, powner);
}

hash_table_t * get_state_owner_hash_table(state_owner_t * powner)
{
  switch(powner->so_type)
    {
#ifdef _USE_NLM
      case STATE_LOCK_OWNER_NLM:
        return ht_nlm_owner;
#endif

#ifdef _USE_9P
      case STATE_LOCK_OWNER_9P:
        returnht_9p_owner;
#endif

      case STATE_OPEN_OWNER_NFSV4:
      case STATE_LOCK_OWNER_NFSV4:
      case STATE_CLIENTID_OWNER_NFSV4:
        return ht_nfs4_owner;

      case STATE_LOCK_OWNER_UNKNOWN:
        break;
    }

  return NULL;
}

void dec_state_owner_ref(state_owner_t * powner)
{
  char                str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch   latch;
  hash_error_t        rc;
  hash_buffer_t       buffkey;
  hash_buffer_t       old_value;
  hash_buffer_t       old_key;
  int32_t             refcount;
  hash_table_t      * ht_owner;

  if(isDebug(COMPONENT_STATE))
    DisplayOwner(powner, str);

  refcount = atomic_dec_int32_t(&powner->so_refcount);

  if(refcount != 0)
    {
      LogFullDebug(COMPONENT_STATE,
                   "Decrement refcount now=%"PRId32" {%s}",
                   refcount, str);

      assert(refcount > 0);

      return;
    }

  ht_owner = get_state_owner_hash_table(powner);

  if(ht_owner == NULL)
    {
      DisplayOwner(powner, str);

      LogCrit(COMPONENT_STATE,
              "Unexpected owner {%s}",
              str);

      assert(ht_owner);

      return;
    }

  buffkey.pdata = powner;
  buffkey.len   = sizeof(*powner);

  /* Get the hash table entry and hold latch */
  rc = HashTable_GetLatch(ht_owner,
                          &buffkey,
                          &old_value,
                          TRUE,
                          &latch);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_owner, &latch);

      DisplayOwner(powner, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  refcount = atomic_fetch_int32_t(&powner->so_refcount);

  if(refcount > 0)
    {
      LogDebug(COMPONENT_STATE,
               "Did not release {%s} refcount now=%"PRId32,
               str, refcount);

      HashTable_ReleaseLatched(ht_owner, &latch);

      return;
    }

  /* use the key to delete the entry */
  rc = HashTable_DeleteLatched(ht_owner,
                               &buffkey,
                               &latch,
                               &old_key,
                               &old_value);

  if(rc != HASHTABLE_SUCCESS)
    {
      if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
        HashTable_ReleaseLatched(ht_owner, &latch);

      DisplayOwner(powner, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not remove {%s}",
              hash_table_err_to_str(rc), str);

      return;
    }

  LogFullDebug(COMPONENT_STATE,
               "Free {%s}",
               str);

  free_state_owner(powner);
}

state_owner_t *get_state_owner(care_t               care,
                               state_owner_t      * pkey,
                               state_owner_init_t   init_owner,
                               bool_t             * isnew)
{
  state_owner_t      * powner;
  char                 str[HASHTABLE_DISPLAY_STRLEN];
  struct hash_latch    latch;
  hash_error_t         rc;
  hash_buffer_t        buffkey;
  hash_buffer_t        buffval;
  hash_table_t       * ht_owner;

  if(isnew != NULL)
    *isnew = FALSE;

  if(isFullDebug(COMPONENT_STATE))
    {
      DisplayOwner(pkey, str);

      LogFullDebug(COMPONENT_STATE,
                   "Find {%s}", str);
    }

  ht_owner = get_state_owner_hash_table(pkey);

  if(ht_owner == NULL)
    {
      DisplayOwner(pkey, str);

      LogCrit(COMPONENT_STATE,
              "ht=%p Unexpected key {%s}",
              ht_owner, str);
      return NULL;
    }

  buffkey.pdata = pkey;
  buffkey.len   = sizeof(*pkey);

  rc = HashTable_GetLatch(ht_owner,
                          &buffkey,
                          &buffval,
                          TRUE,
                          &latch);

  /* If we found it, return it */
  if(rc == HASHTABLE_SUCCESS)
    {
      powner = buffval.pdata;

      /* Return the found NSM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          DisplayOwner(powner, str);
          LogFullDebug(COMPONENT_STATE,
                       "Found {%s}",
                       str);
        }

      /* Increment refcount under hash latch.
       * This prevents dec ref from removing this entry from hash if a race
       * occurs.
       */
      inc_state_owner_ref(powner);

      HashTable_ReleaseLatched(ht_owner, &latch);

      return powner;
    }

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    {
      DisplayOwner(pkey, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, could not find {%s}",
              hash_table_err_to_str(rc), str);

      return NULL;
    }

  /* Not found, but we don't care, return NULL */
  if(care == CARE_NOT)
    {
      /* Return the found NSM Client */
      if(isFullDebug(COMPONENT_STATE))
        {
          DisplayOwner(pkey, str);
          LogFullDebug(COMPONENT_STATE,
                       "Ignoring {%s}",
                       str);
        }

      HashTable_ReleaseLatched(ht_owner, &latch);

      return NULL;
    }

  powner = pool_alloc(state_owner_pool, NULL);

  if(powner == NULL)
    {
      DisplayOwner(pkey, str);
      LogCrit(COMPONENT_STATE,
              "No memory for {%s}",
              str);

      return NULL;
    }

  /* Copy everything over */
  memcpy(powner, pkey, sizeof(*pkey));

  if(pthread_mutex_init(&powner->so_mutex, NULL) == -1)
    {
      /* Mutex initialization failed, free the created owner */
      DisplayOwner(pkey, str);
      LogCrit(COMPONENT_STATE,
              "Could not init mutex for {%s}",
              str);

      gsh_free(powner);
      return NULL;
    }

#ifdef _DEBUG_MEMLEAKS
  P(all_state_owners_mutex);

  glist_add_tail(&state_owners_all, &powner->sle_all_owners);

  V(all_state_owners_mutex);
#endif

  /* Do any owner type specific initialization */
  if(init_owner != NULL)
    init_owner(powner);

  powner->so_owner_val = gsh_malloc(pkey->so_owner_len);

  if(powner->so_owner_val == NULL)
    {
      /* Discard the created owner */
      DisplayOwner(pkey, str);
      LogCrit(COMPONENT_STATE,
              "No memory for {%s}",
              str);

      free_state_owner(powner);
      return NULL;
    }

  memcpy(powner->so_owner_val, pkey->so_owner_val, pkey->so_owner_len);

  init_glist(&powner->so_lock_list);

  powner->so_refcount = 1;

  if(isFullDebug(COMPONENT_STATE))
    {
      DisplayOwner(powner, str);
      LogFullDebug(COMPONENT_STATE,
                   "New {%s}", str);
    }

  buffkey.pdata = powner;
  buffkey.len   = sizeof(*powner);
  buffval.pdata = powner;
  buffval.len   = sizeof(*powner);

  rc = HashTable_SetLatched(ht_owner,
                            &buffval,
                            &buffval,
                            &latch,
                            FALSE,
                            NULL,
                            NULL);

  /* An error occurred, return NULL */
  if(rc != HASHTABLE_SUCCESS)
    {
      DisplayOwner(powner, str);

      LogCrit(COMPONENT_STATE,
              "Error %s, inserting {%s}",
              hash_table_err_to_str(rc), str);

      free_state_owner(powner);

      return NULL;
    }

  if(isnew != NULL)
    *isnew = TRUE;

  return powner;
}

void state_wipe_file(cache_entry_t        * pentry)
{
  bool_t had_lock = FALSE;

  /*
   * currently, only REGULAR files can have state; byte range locks and
   * stateid (for v4).  In the future, 4.1, directories could have
   * delegations, which is state.  At that point, we may need to modify
   * this routine to clear state on directories.
   */
  if (pentry->type != REGULAR_FILE)
    return;

  /* The state lock may have been acquired by the caller. */
  if (pthread_rwlock_trywrlock(&pentry->state_lock))
    {
      /* This thread already has some kind of lock, but we don't know
         if it's a write lock. */
      had_lock = TRUE;
      pthread_rwlock_unlock(&pentry->state_lock);
    }

  pthread_rwlock_wrlock(&pentry->state_lock);

  state_lock_wipe(pentry);

#ifdef _USE_NLM
  state_share_wipe(pentry);
#endif

  state_nfs4_state_wipe(pentry);

  if (!had_lock)
    {
      pthread_rwlock_unlock(&pentry->state_lock);
    }

#ifdef _DEBUG_MEMLEAKS
  dump_all_states();
#endif
}

int DisplayOpaqueValue(char * value, int len, char * str)
{
  unsigned int   i = 0;
  char         * strtmp = str;

  if(value == NULL || len == 0)
    return sprintf(str, "(NULL)");

  strtmp += sprintf(strtmp, "(%d:", len);

  assert(len > 0);

  if(len < 0 || len > 1024)
    len = 1024;

  for(i = 0; i < len; i++)
    if(!isprint(value[i]))
      break;

  if(i == len)
    {
      memcpy(strtmp, value, len);
      strtmp += len;
      *strtmp = '\0';
    }
  else
    {
      strtmp += sprintf(strtmp, "0x");
      for(i = 0; i < len; i++)
        strtmp += sprintf(strtmp, "%02x", (unsigned char)value[i]);
    }

  strtmp += sprintf(strtmp, ")");

  return strtmp - str;
}

#ifdef _DEBUG_MEMLEAKS
void dump_all_owners(void)
{
  if(!isDebug(COMPONENT_STATE))
    return;

  P(all_state_owners_mutex);

  if(!glist_empty(&state_owners_all))
    {
      char                str[HASHTABLE_DISPLAY_STRLEN];
      struct glist_head * glist;

      LogDebug(COMPONENT_STATE,
               " ---------------------- State Owner List ----------------------");

      glist_for_each(glist, &state_owners_all)
        {
          DisplayOwner(glist_entry(glist, state_owner_t, sle_all_owners), str);
          LogDebug(COMPONENT_STATE,
                   "{%s}", str);
        }

      LogDebug(COMPONENT_STATE,
               " ---------------------- --------------- ----------------------");
    }
  else
    LogDebug(COMPONENT_STATE, "All state owners released");

  V(all_state_owners_mutex);
}
#endif
