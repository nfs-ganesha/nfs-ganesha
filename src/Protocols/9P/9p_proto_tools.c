// SPDX-License-Identifier: LGPL-3.0-or-later
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
#include "fsal_convert.h"

/**
 * @brief Allocate a new struct _9p_user_cred, with refcounter set to 1.
 *
 * @return NULL if the allocation failed, else the new structure.
 */
static struct _9p_user_cred *new_9p_user_creds(void)
{
	struct _9p_user_cred *result =
		gsh_calloc(1, sizeof(struct _9p_user_cred));

	result->refcount = 1;

	return result;
}

/**
 * @brief Get a new reference of user credential into the op_ctx.
 *
 * This function increments the refcount of the argument. Any reference returned
 * by this function must be released.
 *
 * @param[in,out] pfid The _9p_fid containing the wanted credentials.
 */
static void set_op_ctx_creds_from_fid(struct _9p_fid *pfid)
{
	(void) atomic_inc_int64_t(&pfid->ucred->refcount);
	op_ctx->proto_private = pfid->ucred;
	op_ctx->creds = pfid->ucred->creds;
}

void get_9p_user_cred_ref(struct _9p_user_cred *creds)
{
	(void) atomic_inc_int64_t(&creds->refcount);
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
 * @brief Release a reference obtained with set_op_ctx_creds_from_fid.
 *
 * This function decrements the refcounter of the containing _9p_user_cred
 * structure. If this counter reaches 0, the structure is freed.
 */
static void release_op_ctx_creds_ref_to_fid_creds(void)
{
	struct _9p_user_cred *cred_9p = op_ctx->proto_private;

	if (cred_9p == NULL)
		return;

	memset(&op_ctx->creds, 0, sizeof(op_ctx->creds));
	op_ctx->proto_private = NULL;
	release_9p_user_cred_ref(cred_9p);
}

int _9p_init(void)
{
	return 0;
}				/* _9p_init */

void _9p_init_opctx(struct _9p_fid *pfid, struct _9p_request_data *req9p)
{
	if (pfid->fid_export != NULL) {
		/* export affectation (require refcount handling). */
		if (op_ctx->ctx_export != pfid->fid_export) {
			if (op_ctx->ctx_export != NULL) {
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

			get_gsh_export_ref(pfid->fid_export);
			set_op_context_export(pfid->fid_export);
		}
	}

	set_op_ctx_creds_from_fid(pfid);
}

void _9p_release_opctx(void)
{
	if (op_ctx->ctx_export != NULL) {
		clear_op_context_export();
	}

	release_op_ctx_creds_ref_to_fid_creds();
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

	release_op_ctx_creds_ref_to_fid_creds();
	set_op_ctx_creds_from_fid(pfid);

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

	release_op_ctx_creds_ref_to_fid_creds();
	set_op_ctx_creds_from_fid(pfid);

	op_ctx->req_type = _9P_REQUEST;

	return 0;
}				/* _9p_tools_get_fsal_cred */

int _9p_tools_errno(fsal_status_t fsal_status)
{
	int rc = 0;

	switch (fsal_status.major) {
	case ERR_FSAL_NO_ERROR:
		rc = 0;
		break;

	case ERR_FSAL_NOMEM:
		rc = ENOMEM;
		break;

	case ERR_FSAL_NOTDIR:
		rc = ENOTDIR;
		break;

	case ERR_FSAL_EXIST:
		rc = EEXIST;
		break;

	case ERR_FSAL_NOTEMPTY:
		rc = ENOTEMPTY;
		break;

	case ERR_FSAL_NOENT:
		rc = ENOENT;
		break;

	case ERR_FSAL_ISDIR:
		rc = EISDIR;
		break;

	case ERR_FSAL_PERM:
	case ERR_FSAL_SEC:
		rc = EPERM;
		break;

	case ERR_FSAL_INVAL:
	case ERR_FSAL_NAMETOOLONG:
	case ERR_FSAL_NOT_OPENED:
	case ERR_FSAL_BADTYPE:
	case ERR_FSAL_SYMLINK:
		rc = EINVAL;
		break;

	case ERR_FSAL_NOSPC:
		rc = ENOSPC;
		break;

	case ERR_FSAL_ROFS:
		rc = EROFS;
		break;

	case ERR_FSAL_STALE:
	case ERR_FSAL_FHEXPIRED:
		rc = ESTALE;
		break;

	case ERR_FSAL_DQUOT:
	case ERR_FSAL_NO_QUOTA:
		rc = EDQUOT;
		break;

	case ERR_FSAL_IO:
	case ERR_FSAL_NXIO:
		rc = EIO;
		break;

	case ERR_FSAL_NOTSUPP:
	case ERR_FSAL_ATTRNOTSUPP:
		rc = ENOTSUP;
		break;

	case ERR_FSAL_ACCESS:
		rc = EACCES;
		break;

	case ERR_FSAL_DELAY:
		rc = EAGAIN;
		break;

	case ERR_FSAL_NO_DATA:
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
		if ((pfid->pentry->type == REGULAR_FILE) && pfid->opens) {
			/* We need to close the state before freeing the state.
			 */
			(void) pfid->pentry->obj_ops->close2(
						pfid->pentry,
						pfid->state);
		}

		pfid->state->state_exp->exp_ops.free_state(
				pfid->state->state_exp, pfid->state);
	}

	if (pfid->pentry != NULL)
		pfid->pentry->obj_ops->put_ref(pfid->pentry);

	if (pfid->ppentry != NULL)
		pfid->ppentry->obj_ops->put_ref(pfid->ppentry);

	if (pfid->fid_export != NULL)
		put_gsh_export(pfid->fid_export);

	if (pfid->ucred != NULL)
		release_9p_user_cred_ref(pfid->ucred);

	gsh_free(pfid->xattr);
	gsh_free(pfid);
}

int _9p_tools_clunk(struct _9p_fid *pfid)
{
	fsal_status_t fsal_status;

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
	if (pfid->xattr != NULL &&
	    pfid->xattr->xattr_write == _9P_XATTR_DID_WRITE) {
		/* Check size give at TXATTRCREATE with
		 * the one resulting from the writes */
		if (pfid->xattr->xattr_size != pfid->xattr->xattr_offset) {
			free_fid(pfid);
			return EINVAL;
		}

		fsal_status =
		    pfid->pentry->obj_ops->setextattr_value(
				pfid->pentry,
				pfid->xattr->xattr_name,
				pfid->xattr->xattr_content,
				pfid->xattr->xattr_size,
				false);
		if (FSAL_IS_ERROR(fsal_status)) {
			free_fid(pfid);
			return _9p_tools_errno(fsal_status);
		}
	}

	/* If object is an opened file, close it */
	if ((pfid->pentry->type == REGULAR_FILE) && pfid->opens) {
		pfid->opens = 0;	/* dead */

		LogDebug(COMPONENT_9P,
			 "Calling close on %s entry %p",
			 object_file_type_to_str(pfid->pentry->type),
			 pfid->pentry);

		fsal_status = pfid->pentry->obj_ops->close2(pfid->pentry,
							   pfid->state);

		if (FSAL_IS_ERROR(fsal_status)) {
			free_fid(pfid);
			return _9p_tools_errno(fsal_status);
		}
	}

	free_fid(pfid);
	return 0;
}

void _9p_cleanup_fids(struct _9p_conn *conn)
{
	int i;
	struct req_op_context op_context;

	/* Initialize op context.
	 * Note we only need it if there is a non-null fid,
	 * might be worth optimizing for huge clusters
	 */
	init_op_context(&op_context, NULL, NULL, NULL, 0, 0, _9P_REQUEST);

	for (i = 0; i < _9P_FID_PER_CONN; i++) {
		if (conn->fids[i]) {
			_9p_init_opctx(conn->fids[i], NULL);
			_9p_tools_clunk(conn->fids[i]);
			_9p_release_opctx();
			conn->fids[i] = NULL;	/* poison the entry */
		}
	}

	release_op_context();
}
