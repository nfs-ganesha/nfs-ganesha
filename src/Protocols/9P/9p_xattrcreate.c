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
#include "os/xattr.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"

int _9p_xattrcreate(struct _9p_request_data *req9p, u32 *plenout, char *preply)
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

	fsal_status_t fsal_status = { .major = ERR_FSAL_NO_ERROR, .minor = 0 };
	char name[MAXNAMLEN+1];

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
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	if (*size > _9P_XATTR_MAX_SIZE)
		return _9p_rerror(req9p, msgtag, ENOSPC, plenout, preply);

	pfid = req9p->pconn->fids[*fid];

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	/* set op_ctx, it will be useful if FSAL is later called */
	_9p_init_opctx(pfid, req9p);

	if ((op_ctx->export_perms->options &
				 EXPORT_OPTION_WRITE_ACCESS) == 0)
		return _9p_rerror(req9p, msgtag, EROFS, plenout, preply);

	if (*name_len >= sizeof(name)) {
		LogDebug(COMPONENT_9P, "request with name too long (%u)",
			 *name_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}
	snprintf(name, sizeof(name), "%.*s", *name_len, name_str);

	if (*size == 0LL) {
		/* Size == 0 : this is in fact a call to removexattr */
		LogDebug(COMPONENT_9P,
			 "TXATTRCREATE: tag=%u fid=%u : will remove xattr %s",
			 (u32) *msgtag, *fid, name);

		fsal_status =
		    pfid->pentry->obj_ops->remove_extattr_by_name(pfid->pentry,
								 name);

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, msgtag,
					  _9p_tools_errno(fsal_status), plenout,
					  preply);
	} else {
		/* Size != 0 , this is a creation/replacement of xattr */

		/* Create the xattr at the FSAL level and cache result */
		pfid->xattr = gsh_malloc(sizeof(*pfid->xattr) + *size);
		pfid->xattr->xattr_size = *size;
		pfid->xattr->xattr_offset = 0LL;
		pfid->xattr->xattr_write = _9P_XATTR_CAN_WRITE;


		strncpy(pfid->xattr->xattr_name, name, MAXNAMLEN);

		/* /!\  POSIX_ACL RELATED HOOK
		 * Setting a POSIX ACL (using setfacl for example) means
		 * settings a xattr named system.posix_acl_access BUT this
		 * attribute is to be used and should not be created
		 * (it exists already since acl feature is on) */
		if (!strcmp(name, "system.posix_acl_access"))
			goto skip_create;

		/* try to create if flag doesn't have REPLACE bit */
		if ((*flag & XATTR_REPLACE) == 0)
			create = true;
		else
			create = false;

		fsal_status =
		    pfid->pentry->obj_ops->setextattr_value(pfid->pentry, name,
				pfid->xattr->xattr_content,
				*size, create);

		/* Try again with create = false if flag was set to 0
		 * and create failed because attribute already exists */
		if (FSAL_IS_ERROR(fsal_status)
		    && fsal_status.major == ERR_FSAL_EXIST && (*flag == 0)) {
			fsal_status = pfid->pentry->obj_ops->setextattr_value(
						pfid->pentry, name,
						pfid->xattr->xattr_content,
						*size, false);
		}

		if (FSAL_IS_ERROR(fsal_status)) {
			gsh_free(pfid->xattr);
			pfid->xattr = NULL;
			return _9p_rerror(req9p, msgtag,
					  _9p_tools_errno(fsal_status), plenout,
					  preply);
		}
	}

skip_create:
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
