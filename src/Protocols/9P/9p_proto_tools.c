/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_proto_tools.c
 * \brief   9P version
 *
 * 9p_proto_tools.c : _9P_interpretor, protocol's service functions
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"
#include "idmapper.h"
#include "uid2grp.h"
#include "export_mgr.h"

/**
 * @brief Allocate a new struct _9p_user_cred, with refcounter set to 1.
 *
 * @return NULL if the allocation failed, else the new structure.
 */
static struct _9p_user_cred *new_9p_user_creds()
{
	struct _9p_user_cred *result =
		gsh_calloc(1, sizeof(struct _9p_user_cred));

	result->refcount = 1;

	return result;
}

/**
 * @brief Get a new reference of user credential.
 *
 * This function increments the refcount of the argument. Any reference returned
 * by this function must be released.
 *
 * @param[in,out] creds The 9P credential containing the wanted credentials.
 * @return A reference to the credentials.
 */
static struct user_cred *get_user_cred_ref(struct _9p_user_cred *creds)
{
	atomic_inc_int64_t(&creds->refcount);
	return &creds->creds;
}

void get_9p_user_cred_ref(struct _9p_user_cred *creds)
{
	atomic_inc_int64_t(&creds->refcount);
}

void release_9p_user_cred_ref(struct _9p_user_cred *creds)
{
	int64_t refcount = atomic_dec_int64_t(&creds->refcount);

	if (refcount != 0) {
		assert(refcount > 0);
		return;
	}

	gsh_free(creds);
}

/**
 * @brief Release a reference obtained with get_user_cred_ref.
 *
 * This function decrements the refcounter of the containing _9p_user_cred
 * structure. If this counter reaches 0, the structure is freed.
 *
 * @param creds The reference that is released.
 */
static void release_user_cred_ref(struct user_cred *creds)
{
	struct _9p_user_cred *cred_9p =
		container_of(creds, struct _9p_user_cred, creds);

	release_9p_user_cred_ref(cred_9p);
}

int _9p_init(void)
{
	return 0;
}				/* _9p_init */

void _9p_init_opctx(struct _9p_fid *pfid, struct _9p_request_data *req9p)
{

	struct gsh_export *export = pfid->export;

	if (export != NULL) {
		/* export affectation (require refcount handling). */
		if (op_ctx->export != export) {
			if (op_ctx->export != NULL) {
				LogCrit(COMPONENT_9P,
					"Op_ctx was already initialized, or was not allocated/cleaned up properly.");
				/* This tells there's an error in the code.
				 * Use an assert because :
				 * - if compiled in debug mode, the program will
				 * crash and tell the developer he has something
				 * to fix here.
				 * - if compiled for production, we'll try to
				 * recover. */
				assert(false);
			}

			get_gsh_export_ref(export);
			op_ctx->export = export;
		}

		op_ctx->fsal_export = export->fsal_export;
	}

	if (req9p != NULL)
		op_ctx->export_perms = &req9p->pconn->export_perms;

	op_ctx->creds = get_user_cred_ref(pfid->ucred);
}

void _9p_release_opctx(void)
{
	struct gsh_export *export = op_ctx->export;

	if (export != NULL) {
		op_ctx->export = NULL;
		put_gsh_export(export);
	}

	struct user_cred *creds = op_ctx->creds;

	if (creds != NULL) {
		op_ctx->creds = NULL;
		release_user_cred_ref(creds);
	}
}

int _9p_tools_get_req_context_by_uid(u32 uid, struct _9p_fid *pfid)
{
	struct group_data *grpdata;

	if (!uid2grp(uid, &grpdata))
		return -ENOENT;

	pfid->ucred = new_9p_user_creds();

	pfid->gdata = grpdata;
	pfid->ucred->creds.caller_uid = grpdata->uid;
	pfid->ucred->creds.caller_gid = grpdata->gid;
	pfid->ucred->creds.caller_glen = grpdata->nbgroups;
	pfid->ucred->creds.caller_garray = grpdata->groups;

	if (op_ctx->creds != NULL)
		release_user_cred_ref(op_ctx->creds);
	op_ctx->creds = get_user_cred_ref(pfid->ucred);

	op_ctx->req_type = _9P_REQUEST;

	return 0;
}				/* _9p_tools_get_fsal_cred */

int _9p_tools_get_req_context_by_name(int uname_len, char *uname_str,
				      struct _9p_fid *pfid)
{
	struct gsh_buffdesc name = {
		.addr = uname_str,
		.len = uname_len
	};
	struct group_data *grpdata;

	if (!name2grp(&name, &grpdata))
		return -ENOENT;

	pfid->ucred = new_9p_user_creds();

	pfid->gdata = grpdata;
	pfid->ucred->creds.caller_uid = grpdata->uid;
	pfid->ucred->creds.caller_gid = grpdata->gid;
	pfid->ucred->creds.caller_glen = grpdata->nbgroups;
	pfid->ucred->creds.caller_garray = grpdata->groups;

	if (op_ctx->creds != NULL)
		release_user_cred_ref(op_ctx->creds);
	op_ctx->creds = get_user_cred_ref(pfid->ucred);

	op_ctx->req_type = _9P_REQUEST;

	return 0;
}				/* _9p_tools_get_fsal_cred */

int _9p_tools_errno(cache_inode_status_t cache_status)
{
	int rc = 0;

	switch (cache_status) {
	case CACHE_INODE_SUCCESS:
		rc = 0;
		break;

	case CACHE_INODE_MALLOC_ERROR:
		rc = ENOMEM;
		break;

	case CACHE_INODE_NOT_A_DIRECTORY:
		rc = ENOTDIR;
		break;

	case CACHE_INODE_ENTRY_EXISTS:
		rc = EEXIST;
		break;

	case CACHE_INODE_DIR_NOT_EMPTY:
		rc = ENOTEMPTY;
		break;

	case CACHE_INODE_NOT_FOUND:
		rc = ENOENT;
		break;

	case CACHE_INODE_IS_A_DIRECTORY:
		rc = EISDIR;
		break;

	case CACHE_INODE_FSAL_EPERM:
	case CACHE_INODE_FSAL_ERR_SEC:
		rc = EPERM;
		break;

	case CACHE_INODE_INVALID_ARGUMENT:
	case CACHE_INODE_NAME_TOO_LONG:
	case CACHE_INODE_INCONSISTENT_ENTRY:
	case CACHE_INODE_FSAL_ERROR:
	case CACHE_INODE_BAD_TYPE:
	case CACHE_INODE_STATE_ERROR:
	case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
	case CACHE_INODE_INIT_ENTRY_FAILED:
		rc = EINVAL;
		break;

	case CACHE_INODE_NO_SPACE_LEFT:
		rc = ENOSPC;
		break;

	case CACHE_INODE_READ_ONLY_FS:
		rc = EROFS;
		break;

	case CACHE_INODE_ESTALE:
		rc = ESTALE;
		break;

	case CACHE_INODE_QUOTA_EXCEEDED:
		rc = EDQUOT;
		break;

	case CACHE_INODE_IO_ERROR:
	case CACHE_INODE_ASYNC_POST_ERROR:
	case CACHE_INODE_GET_NEW_LRU_ENTRY:
	case CACHE_INODE_LRU_ERROR:
	case CACHE_INODE_HASH_SET_ERROR:
	case CACHE_INODE_INSERT_ERROR:
	case CACHE_INODE_HASH_TABLE_ERROR:
		rc = EIO;
		break;

	case CACHE_INODE_NOT_SUPPORTED:
		rc = ENOTSUP;
		break;

	case CACHE_INODE_FSAL_EACCESS:
		rc = EACCES;
		break;

	case CACHE_INODE_DELAY:
		rc = EAGAIN;
		break;

	case CACHE_INODE_NO_DATA:
		rc = ENODATA;
		break;

	default:
		rc = EIO;
		break;
	}

	return rc;
}				/* _9p_tools_errno */

void _9p_openflags2FSAL(u32 *inflags, fsal_openflags_t *outflags)
{
	if (inflags == NULL || outflags == NULL)
		return;

	if (*inflags & O_WRONLY)
		*outflags |= FSAL_O_WRITE;
	if (*inflags & O_RDWR)
		*outflags |= FSAL_O_RDWR;
	/* Exception: O_RDONLY is 0, it can't be tested with a logical and */
	/* We consider that non( has O_WRONLY or has O_RDWR ) is RD_ONLY */
	if (!(*inflags & (O_WRONLY | O_RDWR)))
		*outflags = FSAL_O_READ;
}				/* _9p_openflags2FSAL */

void free_fid(struct _9p_fid *pfid)
{
	if (pfid->state != NULL) {
		if (pfid->pentry->obj_handle->fsal->m_ops.support_ex()) {
			/* We need to close the state before freeing the state.
			 */
			(void) pfid->pentry->obj_handle->obj_ops.close2(
						pfid->pentry->obj_handle,
						pfid->state);
		}

		pfid->state->state_exp->exp_ops.free_state(pfid->state);
	}

	if (pfid->pentry != NULL)
		cache_inode_put(pfid->pentry);

	if (pfid->export != NULL)
		put_gsh_export(pfid->export);

	if (pfid->ucred != NULL)
		release_9p_user_cred_ref(pfid->ucred);

	gsh_free(pfid->specdata.xattr.xattr_content);
	gsh_free(pfid);
}

int _9p_tools_clunk(struct _9p_fid *pfid)
{
	fsal_status_t fsal_status;
	cache_inode_status_t cache_status;

	/* pentry may be null in the case of an aborted TATTACH
	 * this would happens when trying to mount a non-existing
	 * or non-authorized directory */
	if (pfid->pentry == NULL) {
		LogEvent(COMPONENT_9P,
			 "Trying to clunk a fid with NULL pentry. Bad mount ?");
		return 0;
	}

	/* unref the related group list */
	uid2grp_unref(pfid->gdata);

	/* If the fid is related to a xattr, free the related memory */
	if (pfid->specdata.xattr.xattr_content != NULL &&
	    pfid->specdata.xattr.xattr_write == true) {
		/* Check size give at TXATTRCREATE with
		 * the one resulting from the writes */
		if (pfid->specdata.xattr.xattr_size !=
		    pfid->specdata.xattr.xattr_offset) {
			free_fid(pfid);
			return EINVAL;
		}

		/* Do we handle system.posix_acl_access */
		if (pfid->specdata.xattr.xattr_id == ACL_ACCESS_XATTR_ID) {
			fsal_status =
			    pfid->pentry->obj_handle->obj_ops.setextattr_value(
					pfid->pentry->obj_handle,
					"system.posix_acl_access",
					pfid->specdata.xattr.xattr_content,
					pfid->specdata.xattr.xattr_size,
					false);
		} else {
			/* Write the xattr content */
			fsal_status = pfid->pentry->obj_handle->obj_ops.
				setextattr_value_by_id(pfid->pentry->obj_handle,
					pfid->specdata.xattr.xattr_id,
					pfid->specdata.xattr.xattr_content,
					pfid->specdata.xattr.xattr_size);
			if (FSAL_IS_ERROR(fsal_status)) {
				free_fid(pfid);
				return _9p_tools_errno(
					   cache_inode_error_convert(
					       fsal_status));
			}
		}
	}

	/* If object is an opened file, close it */
	if ((pfid->pentry->type == REGULAR_FILE) && pfid->opens) {
		cache_inode_dec_pin_ref(pfid->pentry, false);
		pfid->opens = 0;	/* dead */

		if (pfid->pentry->obj_handle->fsal->m_ops.support_ex()) {
			fsal_status =
			    pfid->pentry->obj_handle->obj_ops.close2(
						pfid->pentry->obj_handle,
						pfid->state);

			cache_status = cache_inode_error_convert(fsal_status);

			if (cache_status != CACHE_INODE_SUCCESS) {
				free_fid(pfid);
				return _9p_tools_errno(cache_status);
			}
		} else {
			/* Under this flag, pin ref is still checked */
			cache_status =
			    cache_inode_close(pfid->pentry,
					      CACHE_INODE_FLAG_REALLYCLOSE);
			if (cache_status != CACHE_INODE_SUCCESS) {
				free_fid(pfid);
				return _9p_tools_errno(cache_status);
			}
			cache_status =
			    cache_inode_refresh_attrs_locked(pfid->pentry);
			if (cache_status != CACHE_INODE_SUCCESS
			    && cache_status != CACHE_INODE_ESTALE) {
				free_fid(pfid);
				return _9p_tools_errno(cache_status);
			}
		}
	}

	free_fid(pfid);
	return 0;
}

void _9p_cleanup_fids(struct _9p_conn *conn)
{
	int i;

	/* Allocate op_ctx, is should always be NULL here
	 * Note we only need it if there is a non-null fid,
	 * might be worth optimizing for huge clusters
	 */
	op_ctx = gsh_calloc(1, sizeof(struct req_op_context));

	for (i = 0; i < _9P_FID_PER_CONN; i++) {
		if (conn->fids[i]) {
			_9p_init_opctx(conn->fids[i], NULL);
			_9p_tools_clunk(conn->fids[i]);
			_9p_release_opctx();
			conn->fids[i] = NULL;	/* poison the entry */
		}
	}

	gsh_free(op_ctx);
	op_ctx = NULL;
}
