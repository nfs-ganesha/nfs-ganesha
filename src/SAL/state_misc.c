// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup SAL State abstraction layer
 * @{
 */

/**
 * @file state_misc.c
 * @brief Misc exported routines for the state abstraction layer
 */

#include "config.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "log.h"
#include "hashtable.h"
#include "fsal.h"
#include "nfs_core.h"
#include "sal_functions.h"

struct glist_head cached_open_owners = GLIST_HEAD_INIT(cached_open_owners);

pthread_mutex_t cached_open_owners_lock = PTHREAD_MUTEX_INITIALIZER;

pool_t *state_owner_pool;	/*< Pool for NFSv4 files's open owner */

#ifdef DEBUG_SAL
struct glist_head state_owners_all = GLIST_HEAD_INIT(state_owners_all);
pthread_mutex_t all_state_owners_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Error conversion routines */
/**
 * @brief Get a string from an error code
 *
 * @param[in] err Error code
 *
 * @return Error string.
 */
const char *state_err_str(state_status_t err)
{
	switch (err) {
	case STATE_SUCCESS:
		return "STATE_SUCCESS";
	case STATE_MALLOC_ERROR:
		return "STATE_MALLOC_ERROR";
	case STATE_POOL_MUTEX_INIT_ERROR:
		return "STATE_POOL_MUTEX_INIT_ERROR";
	case STATE_GET_NEW_LRU_ENTRY:
		return "STATE_GET_NEW_LRU_ENTRY";
	case STATE_INIT_ENTRY_FAILED:
		return "STATE_INIT_ENTRY_FAILED";
	case STATE_FSAL_ERROR:
		return "STATE_FSAL_ERROR";
	case STATE_LRU_ERROR:
		return "STATE_LRU_ERROR";
	case STATE_HASH_SET_ERROR:
		return "STATE_HASH_SET_ERROR";
	case STATE_NOT_A_DIRECTORY:
		return "STATE_NOT_A_DIRECTORY";
	case STATE_INCONSISTENT_ENTRY:
		return "STATE_INCONSISTENT_ENTRY";
	case STATE_BAD_TYPE:
		return "STATE_BAD_TYPE";
	case STATE_ENTRY_EXISTS:
		return "STATE_ENTRY_EXISTS";
	case STATE_DIR_NOT_EMPTY:
		return "STATE_DIR_NOT_EMPTY";
	case STATE_NOT_FOUND:
		return "STATE_NOT_FOUND";
	case STATE_INVALID_ARGUMENT:
		return "STATE_INVALID_ARGUMENT";
	case STATE_INSERT_ERROR:
		return "STATE_INSERT_ERROR";
	case STATE_HASH_TABLE_ERROR:
		return "STATE_HASH_TABLE_ERROR";
	case STATE_FSAL_EACCESS:
		return "STATE_FSAL_EACCESS";
	case STATE_IS_A_DIRECTORY:
		return "STATE_IS_A_DIRECTORY";
	case STATE_FSAL_EPERM:
		return "STATE_FSAL_EPERM";
	case STATE_NO_SPACE_LEFT:
		return "STATE_NO_SPACE_LEFT";
	case STATE_READ_ONLY_FS:
		return "STATE_READ_ONLY_FS";
	case STATE_IO_ERROR:
		return "STATE_IO_ERROR";
	case STATE_ESTALE:
		return "STATE_ESTALE";
	case STATE_FSAL_ERR_SEC:
		return "STATE_FSAL_ERR_SEC";
	case STATE_QUOTA_EXCEEDED:
		return "STATE_QUOTA_EXCEEDED";
	case STATE_ASYNC_POST_ERROR:
		return "STATE_ASYNC_POST_ERROR";
	case STATE_NOT_SUPPORTED:
		return "STATE_NOT_SUPPORTED";
	case STATE_STATE_ERROR:
		return "STATE_STATE_ERROR";
	case STATE_FSAL_DELAY:
		return "STATE_FSAL_DELAY";
	case STATE_NAME_TOO_LONG:
		return "STATE_NAME_TOO_LONG";
	case STATE_LOCK_CONFLICT:
		return "STATE_LOCK_CONFLICT";
	case STATE_LOCKED:
		return "STATE_LOCKED";
	case STATE_LOCK_BLOCKED:
		return "STATE_LOCK_BLOCKED";
	case STATE_LOCK_DEADLOCK:
		return "STATE_LOCK_DEADLOCK";
	case STATE_BAD_COOKIE:
		return "STATE_BAD_COOKIE";
	case STATE_FILE_BIG:
		return "STATE_FILE_BIG";
	case STATE_GRACE_PERIOD:
		return "STATE_GRACE_PERIOD";
	case STATE_CACHE_INODE_ERR:
		return "STATE_CACHE_INODE_ERR";
	case STATE_SIGNAL_ERROR:
		return "STATE_SIGNAL_ERROR";
	case STATE_FILE_OPEN:
		return "STATE_FILE_OPEN";
	case STATE_SHARE_DENIED:
		return "STATE_SHARE_DENIED";
	case STATE_MLINK:
		return "STATE_MLINK";
	case STATE_SERVERFAULT:
		return "STATE_SERVERFAULT";
	case STATE_TOOSMALL:
		return "STATE_TOOSMALL";
	case STATE_XDEV:
		return "STATE_XDEV";
	case STATE_IN_GRACE:
		return "STATE_IN_GRACE";
	case STATE_BADHANDLE:
		return "STATE_BADHANDLE";
	case STATE_BAD_RANGE:
		return "STATE_BAD_RANGE";
	}
	return "unknown";
}

/**
 * @brief converts an FSAL error to the corresponding state error.
 *
 * @param[in] fsal_status Fsal error to be converted
 *
 * @return State status.
 */
state_status_t state_error_convert(fsal_status_t fsal_status)
{
	switch (fsal_status.major) {
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
	case ERR_FSAL_FHEXPIRED:
		return STATE_ESTALE;

	case ERR_FSAL_INVAL:
	case ERR_FSAL_OVERFLOW:
		return STATE_INVALID_ARGUMENT;

	case ERR_FSAL_SEC:
		return STATE_FSAL_ERR_SEC;

	case ERR_FSAL_NOTSUPP:
	case ERR_FSAL_ATTRNOTSUPP:
	case ERR_FSAL_UNION_NOTSUPP:
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
	case ERR_FSAL_BADTYPE:
		return STATE_BAD_TYPE;

	case ERR_FSAL_ISDIR:
		return STATE_IS_A_DIRECTORY;

	case ERR_FSAL_FBIG:
		return STATE_FILE_BIG;

	case ERR_FSAL_FILE_OPEN:
		return STATE_FILE_OPEN;

	case ERR_FSAL_SHARE_DENIED:
		return STATE_SHARE_DENIED;

	case ERR_FSAL_BLOCKED:
		return STATE_LOCK_BLOCKED;

	case ERR_FSAL_IN_GRACE:
		return STATE_IN_GRACE;

	case ERR_FSAL_BADHANDLE:
		return STATE_BADHANDLE;

	case ERR_FSAL_BAD_RANGE:
		return STATE_BAD_RANGE;

	case ERR_FSAL_LOCKED:
		return STATE_LOCKED;

	case ERR_FSAL_TOOSMALL:
		return STATE_TOOSMALL;

	case ERR_FSAL_DQUOT:
	case ERR_FSAL_NAMETOOLONG:
	case ERR_FSAL_STILL_IN_USE:
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
	case ERR_FSAL_TIMEOUT:
	case ERR_FSAL_SERVERFAULT:
	case ERR_FSAL_NO_DATA:
	case ERR_FSAL_NO_ACE:
	case ERR_FSAL_CROSS_JUNCTION:
	case ERR_FSAL_BADNAME:
	case ERR_FSAL_NOXATTR:
	case ERR_FSAL_XATTR2BIG:
		/* These errors should be handled inside state
		 * (or should never be seen by state)
		 */
		LogDebug(COMPONENT_STATE,
			 "Conversion of FSAL error %d,%d to STATE_FSAL_ERROR",
			 fsal_status.major, fsal_status.minor);
		return STATE_FSAL_ERROR;
	}

	/* We should never reach this line, this may produce a warning with
	 * certain compiler */
	LogCrit(COMPONENT_STATE,
		"Default conversion to STATE_FSAL_ERROR for error %d, line %u should never be reached",
		fsal_status.major, __LINE__);

	return STATE_FSAL_ERROR;
}

/**
 * @brief Converts a state status to a nfsv4 status
 *
 * @param[in] error Input state error
 *
 * @return the converted NFSv4 status.
 *
 */
nfsstat4 nfs4_Errno_state(state_status_t error)
{
	nfsstat4 nfserror = NFS4ERR_INVAL;

	switch (error) {
	case STATE_SUCCESS:
		nfserror = NFS4_OK;
		break;

	case STATE_MALLOC_ERROR:
		nfserror = NFS4ERR_RESOURCE;
		break;

	case STATE_POOL_MUTEX_INIT_ERROR:
	case STATE_GET_NEW_LRU_ENTRY:
	case STATE_INIT_ENTRY_FAILED:
		nfserror = NFS4ERR_SERVERFAULT;
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

	case STATE_ESTALE:
		nfserror = NFS4ERR_STALE;
		break;

	case STATE_SHARE_DENIED:
		nfserror = NFS4ERR_SHARE_DENIED;
		break;

	case STATE_LOCKED:
		nfserror = NFS4ERR_LOCKED;
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

	case STATE_SERVERFAULT:
		nfserror = NFS4ERR_SERVERFAULT;
		break;

	case STATE_MLINK:
		nfserror = NFS4ERR_MLINK;
		break;

	case STATE_TOOSMALL:
		nfserror = NFS4ERR_TOOSMALL;
		break;

	case STATE_IN_GRACE:
		nfserror = NFS4ERR_GRACE;
		break;

	case STATE_XDEV:
		nfserror = NFS4ERR_XDEV;
		break;

	case STATE_BADHANDLE:
		nfserror = NFS4ERR_BADHANDLE;
		break;

	case STATE_BAD_RANGE:
		nfserror = NFS4ERR_BAD_RANGE;
		break;

	case STATE_INVALID_ARGUMENT:
	case STATE_CACHE_INODE_ERR:
	case STATE_INCONSISTENT_ENTRY:
	case STATE_HASH_TABLE_ERROR:
	case STATE_ASYNC_POST_ERROR:
	case STATE_SIGNAL_ERROR:
		/* Should not occur */
		nfserror = NFS4ERR_INVAL;
		break;
	}

	return nfserror;
}

/**
 * @brief Converts a state status to an NFSv3 status
 *
 * @param[in] error State error
 *
 * @return Converted NFSv3 status.
 */
nfsstat3 nfs3_Errno_state(state_status_t error)
{
	nfsstat3 nfserror = NFS3ERR_INVAL;

	switch (error) {
	case STATE_SUCCESS:
		nfserror = NFS3_OK;
		break;

	case STATE_MALLOC_ERROR:
	case STATE_POOL_MUTEX_INIT_ERROR:
	case STATE_GET_NEW_LRU_ENTRY:
	case STATE_INIT_ENTRY_FAILED:
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

	case STATE_LOCKED:
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

	case STATE_ESTALE:
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
	case STATE_SHARE_DENIED:
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

	case STATE_MLINK:
		nfserror = NFS3ERR_MLINK;
		break;

	case STATE_SERVERFAULT:
		nfserror = NFS3ERR_SERVERFAULT;
		break;

	case STATE_TOOSMALL:
		nfserror = NFS3ERR_TOOSMALL;
		break;

	case STATE_XDEV:
		nfserror = NFS3ERR_XDEV;
		break;

	case STATE_IN_GRACE:
		nfserror = NFS3ERR_JUKEBOX;
		break;

	case STATE_BADHANDLE:
		nfserror = NFS3ERR_BADHANDLE;
		break;

	case STATE_CACHE_INODE_ERR:
	case STATE_INCONSISTENT_ENTRY:
	case STATE_HASH_TABLE_ERROR:
	case STATE_ASYNC_POST_ERROR:
	case STATE_STATE_ERROR:
	case STATE_LOCK_CONFLICT:
	case STATE_LOCK_BLOCKED:
	case STATE_LOCK_DEADLOCK:
	case STATE_GRACE_PERIOD:
	case STATE_SIGNAL_ERROR:
	case STATE_BAD_RANGE:
		/* Should not occur */
		LogCrit(COMPONENT_NFSPROTO,
			"Unexpected status for conversion = %s",
			state_err_str(error));
		nfserror = NFS3ERR_INVAL;
		break;
	}

	return nfserror;
}

bool state_unlock_err_ok(state_status_t status)
{
	return status == STATE_SUCCESS ||
	       status == STATE_ESTALE;
}

/** String for undefined state owner types */
const char *invalid_state_owner_type = "INVALID STATE OWNER TYPE";

/**
 * @brief Return a string describing a state owner type
 *
 * @param[in] type The state owner type
 *
 * @return The string representation of the type.
 */
const char *state_owner_type_to_str(state_owner_type_t type)
{
	switch (type) {
	case STATE_LOCK_OWNER_UNKNOWN:
		return "STATE_LOCK_OWNER_UNKNOWN";
#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		return "STATE_LOCK_OWNER_NLM";
#endif /* _USE_NLM */
#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		return "STALE_LOCK_OWNER_9P";
#endif
	case STATE_OPEN_OWNER_NFSV4:
		return "STATE_OPEN_OWNER_NFSV4";
	case STATE_LOCK_OWNER_NFSV4:
		return "STATE_LOCK_OWNER_NFSV4";
	case STATE_CLIENTID_OWNER_NFSV4:
		return "STATE_CLIENTID_OWNER_NFSV4";
	}
	return invalid_state_owner_type;
}

/**
 * @brief See if two owners differ
 *
 * @param[in] owner1 An owner
 * @param[in] owner2 Another owner
 *
 * @retval true if owners differ.
 * @retval false if owners are the same.
 */
bool different_owners(state_owner_t *owner1, state_owner_t *owner2)
{
	if (owner1 == NULL || owner2 == NULL)
		return true;

	/* Shortcut if we actually are pointing to the same owner structure */
	if (owner1 == owner2)
		return false;

	if (owner1->so_type != owner2->so_type)
		return true;

	switch (owner1->so_type) {
#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		return compare_nlm_owner(owner1, owner2);
#endif /* _USE_NLM */
#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		return compare_9p_owner(owner1, owner2);
#endif
	case STATE_OPEN_OWNER_NFSV4:
	case STATE_LOCK_OWNER_NFSV4:
	case STATE_CLIENTID_OWNER_NFSV4:
		return compare_nfs4_owner(owner1, owner2);

	case STATE_LOCK_OWNER_UNKNOWN:
		break;
	}

	return true;
}

/**
 * @brief Display a state owner
 *
 * @param[in/out] dspbuf display_buffer describing output string
 * @param[in]     owner  Owner to display
 *
 * @return the bytes remaining in the buffer.
 */
int display_owner(struct display_buffer *dspbuf, state_owner_t *owner)
{
	if (owner == NULL)
		return display_printf(dspbuf, "<NULL>");

	switch (owner->so_type) {
#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		return display_nlm_owner(dspbuf, owner);
#endif /* _USE_NLM */

#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		return display_9p_owner(dspbuf, owner);
#endif

	case STATE_OPEN_OWNER_NFSV4:
	case STATE_LOCK_OWNER_NFSV4:
	case STATE_CLIENTID_OWNER_NFSV4:
		return display_nfs4_owner(dspbuf, owner);

	case STATE_LOCK_OWNER_UNKNOWN:
		return
		    display_printf(dspbuf,
				   "%s powner=%p: refcount=%d",
				   state_owner_type_to_str(owner->so_type),
				   owner,
				   atomic_fetch_int32_t(&owner->so_refcount));
	}

	return display_printf(dspbuf,
			      "%s powner=%p",
			      invalid_state_owner_type, owner);
}

/**
 * @brief Acquire a reference on a state owner
 *
 * @param[in] owner The owner to acquire
 */
void inc_state_owner_ref(state_owner_t *owner)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	int32_t refcount;

	if (isFullDebug(COMPONENT_STATE)) {
		display_owner(&dspbuf, owner);
		str_valid = true;
	}

	refcount = atomic_inc_int32_t(&owner->so_refcount);

	if (str_valid)
		LogFullDebug(COMPONENT_STATE,
			     "Increment refcount now=%" PRId32 " {%s}",
			     refcount, str);
}

/**
 * @brief Free a state owner object
 *
 * @param[in] owner The owner to free
 */
void free_state_owner(state_owner_t *owner)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};

	switch (owner->so_type) {
#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		free_nlm_owner(owner);
		break;
#endif /* _USE_NLM */

#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		break;
#endif

	case STATE_OPEN_OWNER_NFSV4:
	case STATE_LOCK_OWNER_NFSV4:
	case STATE_CLIENTID_OWNER_NFSV4:
		free_nfs4_owner(owner);
		break;

	case STATE_LOCK_OWNER_UNKNOWN:
		display_owner(&dspbuf, owner);

		LogCrit(COMPONENT_STATE, "Unexpected removal of {%s}", str);
		return;
	}

	gsh_free(owner->so_owner_val);

	PTHREAD_MUTEX_destroy(&owner->so_mutex);

#ifdef DEBUG_SAL
	PTHREAD_MUTEX_lock(&all_state_owners_mutex);

	glist_del(&owner->so_all_owners);

	PTHREAD_MUTEX_unlock(&all_state_owners_mutex);
#endif

	pool_free(state_owner_pool, owner);
}

/**
 * @brief Get the hash table associated with a state owner object
 *
 * @param[in] owner The owner to get associated hash table for
 */
hash_table_t *get_state_owner_hash_table(state_owner_t *owner)
{
	switch (owner->so_type) {
#ifdef _USE_NLM
	case STATE_LOCK_OWNER_NLM:
		return ht_nlm_owner;
#endif /* _USE_NLM */

#ifdef _USE_9P
	case STATE_LOCK_OWNER_9P:
		return ht_9p_owner;
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

/**
 * @brief Relinquish a reference on a state owner
 *
 * @param[in] owner The owner to release
 */
void dec_state_owner_ref(state_owner_t *owner)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc old_value;
	int32_t refcount;
	hash_table_t *ht_owner;

	if (isDebug(COMPONENT_STATE)) {
		display_owner(&dspbuf, owner);
		str_valid = true;
	}

	refcount = atomic_dec_int32_t(&owner->so_refcount);

	if (refcount != 0) {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE,
				     "Decrement refcount now=%" PRId32 " {%s}",
				     refcount, str);

		assert(refcount > 0);

		return;
	}

	ht_owner = get_state_owner_hash_table(owner);

	if (ht_owner == NULL) {
		if (!str_valid)
			display_printf(&dspbuf, "Invalid owner %p", owner);

		LogCrit(COMPONENT_STATE, "Unexpected owner {%s}, type {%d}",
			str, owner->so_type);

		return;
	}

	buffkey.addr = owner;
	buffkey.len = sizeof(*owner);

	/* Since the refcount is zero, another thread that needs this
	 * owner might have deleted ours, so expect not to find one or
	 * find someone else's owner!
	 */
	rc = hashtable_getlatch(ht_owner, &buffkey, &old_value, true, &latch);
	switch (rc) {
	case HASHTABLE_SUCCESS:
		/* If ours, delete from hash table */
		if (old_value.addr == owner) {
			hashtable_deletelatched(ht_owner, &buffkey, &latch,
						NULL, NULL);
		}
		break;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		break;

	default:
		if (!str_valid)
			display_printf(&dspbuf, "Invalid owner %p", owner);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return;
	}

	/* Release the latch */
	hashtable_releaselatched(ht_owner, &latch);

	if (str_valid)
		LogFullDebug(COMPONENT_STATE, "Free {%s}", str);

	free_state_owner(owner);
}

/** @brief Remove an NFS 4 open owner from the cached owners list.
 *
 * The caller MUST hold the cached_open_owners_lock, also must NOT hold
 * so_mutex as the so_mutex may get destroyed after this call.
 *
 * If this owner is being revived, the refcount should have already been
 * incremented for the new primary reference. This function will release the
 * refcount that held it in the cache.
 *
 * @param[in] nfs4_owner The owner to release.
 *
 */
void uncache_nfs4_owner(struct state_nfs4_owner_t *nfs4_owner)
{
	state_owner_t *owner = container_of(nfs4_owner,
					    state_owner_t,
					    so_owner.so_nfs4_owner);

	/* This owner is to be removed from the open owner cache:
	 * 1. Remove it from the list.
	 * 2. Make sure this is now a proper list head again.
	 * 3. Indicate it is no longer cached.
	 * 4. Release the reference held on behalf of the cache.
	 */
	if (isFullDebug(COMPONENT_STATE)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};

		display_owner(&dspbuf, owner);

		LogFullDebug(COMPONENT_STATE, "Uncache {%s}", str);
	}

	/* Remove owner from cached_open_owners */
	glist_del(&nfs4_owner->so_cache_entry);

	atomic_store_time_t(&nfs4_owner->so_cache_expire, 0);

	dec_state_owner_ref(owner);
}

static inline
void refresh_nfs4_open_owner(struct state_nfs4_owner_t *nfs4_owner)
{
	time_t cache_expire;

	/* Since this owner is active, reset cache_expire. */
	cache_expire = atomic_fetch_time_t(&nfs4_owner->so_cache_expire);

	if (cache_expire != 0) {
		PTHREAD_MUTEX_lock(&cached_open_owners_lock);

		/* Check again while holding the mutex. */

		if (atomic_fetch_time_t(&nfs4_owner->so_cache_expire) != 0) {
			/* This is a cached open owner, uncache it for use. */
			uncache_nfs4_owner(nfs4_owner);
		}

		PTHREAD_MUTEX_unlock(&cached_open_owners_lock);
	}
}

/**
 * @brief Get a state owner
 *
 * @param[in] care indicates how we care about the owner
 * @param[in] key the owner key we are searching for
 * @param[in] init_owner routine to initialize a new owner
 * @param[in,out] isnew pointer to flag indicating a new owner was created
 *
 * @return the owner found or NULL if no owner was found or created
 */
state_owner_t *get_state_owner(care_t care, state_owner_t *key,
			       state_owner_init_t init_owner, bool_t *isnew)
{
	state_owner_t *owner;
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	struct hash_latch latch;
	hash_error_t rc;
	struct gsh_buffdesc buffkey;
	struct gsh_buffdesc buffval;
	hash_table_t *ht_owner;
	int32_t refcount;

	if (isnew != NULL)
		*isnew = false;

	if (isFullDebug(COMPONENT_STATE)) {
		display_owner(&dspbuf, key);

		LogFullDebug(COMPONENT_STATE, "Find {%s}", str);

		str_valid = true;
	}

	ht_owner = get_state_owner_hash_table(key);

	if (ht_owner == NULL) {
		if (!str_valid)
			display_owner(&dspbuf, key);

		LogCrit(COMPONENT_STATE, "ht=%p Unexpected key {%s}", ht_owner,
			str);
		return NULL;
	}

	buffkey.addr = key;
	buffkey.len = sizeof(*key);
	rc = hashtable_getlatch(ht_owner, &buffkey, &buffval, true, &latch);

	switch (rc) {
	case HASHTABLE_SUCCESS:
		owner = buffval.addr;
		refcount = atomic_inc_int32_t(&owner->so_refcount);
		if (refcount == 1) {
			/* This owner is in the process of getting freed.
			 * Delete from the hash table and pretend as
			 * though we didn't find it!
			 */
			(void)atomic_dec_int32_t(&owner->so_refcount);
			hashtable_deletelatched(ht_owner, &buffkey, &latch,
						NULL, NULL);
			goto not_found;
		}

		/* Return the found NSM Client */
		if (isFullDebug(COMPONENT_STATE)) {
			display_owner(&dspbuf, owner);
			str_valid = true;
		}

		hashtable_releaselatched(ht_owner, &latch);

		/* Refresh an nfs4 open owner if needed. */
		if (owner->so_type == STATE_OPEN_OWNER_NFSV4) {
			refresh_nfs4_open_owner(&owner->so_owner.so_nfs4_owner);
		}

		if (isFullDebug(COMPONENT_STATE)) {
			if (!str_valid)
				display_owner(&dspbuf, owner);

			LogFullDebug(COMPONENT_STATE,
				     "Found {%s} refcount now=%" PRId32,
				     str, refcount);
		}
		return owner;

	case HASHTABLE_ERROR_NO_SUCH_KEY:
		goto not_found;

	default:
		if (!str_valid)
			display_owner(&dspbuf, key);

		LogCrit(COMPONENT_STATE, "Error %s, could not find {%s}",
			hash_table_err_to_str(rc), str);

		return NULL;
	}

not_found:
	/* Not found, but we don't care, return NULL */
	if (care == CARE_NOT) {
		if (str_valid)
			LogFullDebug(COMPONENT_STATE, "Ignoring {%s}", str);

		hashtable_releaselatched(ht_owner, &latch);

		return NULL;
	}

	owner = pool_alloc(state_owner_pool);

	/* Copy everything over */
	memcpy(owner, key, sizeof(*key));

	PTHREAD_MUTEX_init(&owner->so_mutex, NULL);

#ifdef DEBUG_SAL
	PTHREAD_MUTEX_lock(&all_state_owners_mutex);

	glist_add_tail(&state_owners_all, &owner->so_all_owners);

	PTHREAD_MUTEX_unlock(&all_state_owners_mutex);
#endif

	/* Do any owner type specific initialization */
	if (init_owner != NULL)
		init_owner(owner);


	if (key->so_owner_len != 0) {
		owner->so_owner_val = gsh_malloc(key->so_owner_len);

		memcpy(owner->so_owner_val,
		       key->so_owner_val,
		       key->so_owner_len);
	}

	glist_init(&owner->so_lock_list);

	owner->so_refcount = 1;

	if (isFullDebug(COMPONENT_STATE)) {
		display_reset_buffer(&dspbuf);
		display_owner(&dspbuf, owner);
		LogFullDebug(COMPONENT_STATE, "New {%s}", str);
		str_valid = true;
	} else {
		/* If we had the key, we don't want it anymore */
		str_valid = false;
	}

	buffkey.addr = owner;
	buffkey.len = sizeof(*owner);
	buffval.addr = owner;
	buffval.len = sizeof(*owner);

	rc = hashtable_setlatched(ht_owner, &buffval, &buffval, &latch, false,
				  NULL, NULL);

	/* An error occurred, return NULL */
	if (rc != HASHTABLE_SUCCESS) {
		if (!str_valid)
			display_owner(&dspbuf, owner);

		LogCrit(COMPONENT_STATE, "Error %s, inserting {%s}",
			hash_table_err_to_str(rc), str);

		free_state_owner(owner);

		return NULL;
	}

	if (isnew != NULL)
		*isnew = true;

	return owner;
}

/**
 * @brief Acquire a reference on a state owner that might be getting freed.
 *
 * @param[in] owner The owner to acquire
 *
 * inc_state_owner_ref() can be called to hold a reference provided
 * someone already has a valid refcount. In other words, one can't call
 * inc_state_owner_ref on a owner whose refcount has possibly gone to
 * zero.
 *
 * If we don't know for sure if the owner refcount is positive, this
 * function must be called to hold a reference.  This function places a
 * reference if the owner has a positive refcount already.
 *
 * @return True if we are able to hold a reference, False otherwise.
 */
bool hold_state_owner(state_owner_t *owner)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};
	bool str_valid = false;
	hash_table_t *ht_owner;
	struct gsh_buffdesc buffkey;
	struct hash_latch latch;
	hash_error_t rc;
	int32_t refcount;

	/* We need to increment and possibly decrement the refcount
	 * atomically. This "increment" to check for the owner refcount
	 * condition is also done in get_state_owner. Those are done
	 * under the hash lock, so we do the same here.
	 */
	ht_owner = get_state_owner_hash_table(owner);
	if (ht_owner == NULL) {
		if (!str_valid)
			display_owner(&dspbuf, owner);
		LogCrit(COMPONENT_STATE,
			"ht=%p Unexpected key {%s}", ht_owner, str);
		return false;
	}

	buffkey.addr = owner;
	buffkey.len = sizeof(*owner);

	/* We don't care if this owner is in the hashtable or not. We
	 * just need the partition lock in exclusive mode!
	 */
	rc = hashtable_acquire_latch(ht_owner, &buffkey, &latch);
	switch (rc) {
	case HASHTABLE_SUCCESS:
		refcount = atomic_inc_int32_t(&owner->so_refcount);
		if (refcount == 1) {
			/* This owner is in the process of getting freed */
			(void)atomic_dec_int32_t(&owner->so_refcount);
			hashtable_releaselatched(ht_owner, &latch);
			return false;
		}

		hashtable_releaselatched(ht_owner, &latch);
		return true;

	default:
		assert(0);
		return false;
	}
}

/**
 * @brief Release all state on a file
 *
 * This function may not be called in any context which could hold
 * entry->st_lock.  It will now be reliably called in cleanup
 * processing.
 *
 * @param[in,out] obj File to be wiped
 */
void state_wipe_file(struct fsal_obj_handle *obj)
{
	/*
	 * currently, only REGULAR files can have state; byte range locks and
	 * stateid (for v4).  In the future, 4.1, directories could have
	 * delegations, which is state.  At that point, we may need to modify
	 * this routine to clear state on directories.
	 */
	if (obj->type != REGULAR_FILE)
		return;

	STATELOCK_lock(obj);

	state_lock_wipe(obj->state_hdl);
#ifdef _USE_NLM
	state_share_wipe(obj->state_hdl);
#endif /* _USE_NLM */
	state_nfs4_state_wipe(obj->state_hdl);

	STATELOCK_unlock(obj);

#ifdef DEBUG_SAL
	dump_all_states();
#endif
}

#ifdef DEBUG_SAL
void dump_all_owners(void)
{
	if (!isFullDebug(COMPONENT_STATE))
		return;

	PTHREAD_MUTEX_lock(&all_state_owners_mutex);

	if (!glist_empty(&state_owners_all)) {
		char str[LOG_BUFF_LEN] = "\0";
		struct display_buffer dspbuf = {sizeof(str), str, str};
		struct glist_head *glist;

		LogFullDebug(COMPONENT_STATE,
			     " ---------------------- State Owner List ----------------------");

		glist_for_each(glist, &state_owners_all) {
			display_reset_buffer(&dspbuf);
			display_owner(&dspbuf, glist_entry(glist,
							   state_owner_t,
							   so_all_owners));
			LogFullDebug(COMPONENT_STATE, "{%s}", str);
		}

		LogFullDebug(COMPONENT_STATE, " ----------------------");
	} else
		LogFullDebug(COMPONENT_STATE, "All state owners released");

	PTHREAD_MUTEX_unlock(&all_state_owners_mutex);
}
#endif

/**
 * @brief Release all the state belonging to an export.
 *
 * @param[in]  exp   The export to release state for.
 *
 */

void state_release_export(struct gsh_export *export)
{
	struct req_op_context op_context;

	/* Get a ref to the export and initialize op_context */
	get_gsh_export_ref(export);
	init_op_context_simple(&op_context, export, export->fsal_export);

	state_export_unlock_all();
	state_export_release_nfs4_state();
#ifdef _USE_NLM
	state_export_unshare_all();
#endif /* _USE_NLM */
	release_op_context();
}

/** @} */
