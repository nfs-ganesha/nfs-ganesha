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
 * \file    9p_xattrcreate.c
 * \brief   9P version
 *
 * 9p_xattrcreate.c : _9P_interpretor, request XATTRCREATE
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "fsal.h"
#include "9p.h"

int _9p_xattrcreate(struct _9p_request_data *req9p, void *worker_data,
		    u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	int create;

	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u64 *size;
	u32 *flag;
	u16 *name_len;
	char *name_str;

	struct _9p_fid *pfid = NULL;

	fsal_status_t fsal_status;
	char name[MAXNAMLEN];

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, size, u64);
	_9p_getptr(cursor, flag, u32);

	LogDebug(COMPONENT_9P,
		 "TXATTRCREATE: tag=%u fid=%u name=%.*s size=%llu flag=%u",
		 (u32) *msgtag, *fid, *name_len, name_str,
		 (unsigned long long)*size, *flag);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	snprintf(name, MAXNAMLEN, "%.*s", *name_len, name_str);

	/* set op_ctx, it will be useful if FSAL is later called */
	op_ctx = &pfid->op_context;

	if (*size == 0LL) {
		/* Size == 0 : this is in fact a call to removexattr */
		LogDebug(COMPONENT_9P,
			 "TXATTRCREATE: tag=%u fid=%u : will remove xattr %s",
			 (u32) *msgtag, *fid, name);

		fsal_status =
		    pfid->pentry->obj_handle->ops->remove_extattr_by_name(
			pfid->pentry->obj_handle,
			name);

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno
					  (cache_inode_error_convert
					   (fsal_status)), plenout, preply);
	} else if (!strncmp(name, "system.posix_acl_access", MAXNAMLEN)) {
		/* /!\  POSIX_ACL RELATED HOOK
		 * Setting a POSIX ACL (using setfacl for example) means
		 * settings a xattr named system.posix_acl_access BUT this
		 * attribute is to be used and should not be created
		 * (it exists already since acl feature is on) */
		fsal_status.major = ERR_FSAL_NO_ERROR;
		fsal_status.minor = 0;

		/* Create the xattr at the FSAL level and cache result */
		pfid->specdata.xattr.xattr_content =
		     gsh_malloc(XATTR_BUFFERSIZE);
		if (pfid->specdata.xattr.xattr_content == NULL)
			return _9p_rerror(req9p, worker_data, msgtag, ENOMEM,
					  plenout, preply);
		/* Special Value */
		pfid->specdata.xattr.xattr_id = ACL_ACCESS_XATTR_ID;
	} else {
		/* Size != 0 , this is a creation/replacement of xattr */

		/* Create the xattr at the FSAL level and cache result */
		pfid->specdata.xattr.xattr_content =
		     gsh_malloc(XATTR_BUFFERSIZE);
		if (pfid->specdata.xattr.xattr_content == NULL)
			return _9p_rerror(req9p, worker_data, msgtag, ENOMEM,
					  plenout, preply);

		/* try to create if flag doesn't have REPLACE bit */
		if ((*flag & XATTR_REPLACE) == 0)
			create = TRUE;
		else
			create = FALSE;

		fsal_status =
		    pfid->pentry->obj_handle->ops->setextattr_value(
			pfid->pentry->obj_handle,
			name,
			pfid->specdata.xattr.xattr_content,
			*size, create);

		/* Try again with create = false if flag was set to 0
		 * and create failed because attribute already exists */
		if (FSAL_IS_ERROR(fsal_status)
		    && fsal_status.major == ERR_FSAL_EXIST && (*flag == 0)) {
			fsal_status =
			    pfid->pentry->obj_handle->ops->
			    setextattr_value(pfid->pentry->obj_handle,
					     name,
					     pfid->specdata.xattr.xattr_content,
					     *size, FALSE);
		}

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno
					  (cache_inode_error_convert
					   (fsal_status)), plenout, preply);

		fsal_status =
		    pfid->pentry->obj_handle->ops->getextattr_id_by_name(
			pfid->pentry->obj_handle,
			name,
			&pfid->specdata.xattr.xattr_id);

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno
					  (cache_inode_error_convert
					   (fsal_status)), plenout, preply);
	}

	/* Remember the size of the xattr to be written,
	 * in order to check at TCLUNK */
	pfid->specdata.xattr.xattr_size = *size;
	pfid->specdata.xattr.xattr_offset = 0LL;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RXATTRCREATE);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RXATTRCREATE: tag=%u fid=%u name=%.*s size=%llu flag=%u",
		 (u32) *msgtag, *fid, *name_len, name_str,
		 (unsigned long long)*size, *flag);

	return 1;
}				/* _9p_xattrcreate */
