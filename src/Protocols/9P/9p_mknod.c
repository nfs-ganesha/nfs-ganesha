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
 * \file    9p_mknod.c
 * \brief   9P version
 *
 * 9p_mknod.c : _9P_interpretor, request MKNOD
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "nfs_exports.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"

int _9p_mknod(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *mode = NULL;
	u32 *gid = NULL;
	u32 *major = NULL;
	u32 *minor = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;

	struct _9p_fid *pfid = NULL;
	struct _9p_qid qid_newobj;

	struct fsal_obj_handle *pentry_newobj = NULL;
	char obj_name[MAXNAMLEN+1];
	uint64_t fileid = 0LL;
	fsal_status_t fsal_status;
	object_file_type_t nodetype;
	struct fsal_attrlist object_attributes;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, mode, u32);
	_9p_getptr(cursor, major, u32);
	_9p_getptr(cursor, minor, u32);
	_9p_getptr(cursor, gid, u32);

	LogDebug(COMPONENT_9P,
		 "TMKNOD: tag=%u fid=%u name=%.*s mode=0%o major=%u minor=%u gid=%u",
		 (u32) *msgtag, *fid, *name_len, name_str, *mode, *major,
		 *minor, *gid);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pfid = req9p->pconn->fids[*fid];

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	_9p_init_opctx(pfid, req9p);

	if ((op_ctx->export_perms.options & EXPORT_OPTION_WRITE_ACCESS) == 0)
		return _9p_rerror(req9p, msgtag, EROFS, plenout, preply);

	if (*name_len >= sizeof(obj_name)) {
		LogDebug(COMPONENT_9P, "request with name too long (%u)",
			 *name_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}

	_9p_get_fname(obj_name, *name_len, name_str);

	/* Set the nodetype */
	if (S_ISDIR(*mode))
		nodetype = CHARACTER_FILE;
	else if (S_ISBLK(*mode))
		nodetype = BLOCK_FILE;
	else if (S_ISFIFO(*mode))
		nodetype = FIFO_FILE;
	else if (S_ISSOCK(*mode))
		nodetype = SOCKET_FILE;
	else			/* bad type */
		return _9p_rerror(req9p, msgtag, EINVAL, plenout, preply);

	fsal_prepare_attrs(&object_attributes, ATTR_RAWDEV | ATTR_MODE);

	object_attributes.rawdev.major = *major;
	object_attributes.rawdev.minor = *minor;
	object_attributes.valid_mask |= ATTR_RAWDEV;
	object_attributes.mode = *mode;

	/* Create the directory */
	/**  @todo  BUGAZOMEU the gid parameter is not used yet */
	fsal_status = fsal_create(pfid->pentry, obj_name, nodetype,
				  &object_attributes, NULL, &pentry_newobj,
				  NULL);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&object_attributes);

	if (FSAL_IS_ERROR(fsal_status))
		return _9p_rerror(req9p, msgtag, _9p_tools_errno(fsal_status),
				  plenout, preply);

	/* we don't keep a reference to the entry */
	pentry_newobj->obj_ops->put_ref(pentry_newobj);

	/* Build the qid */
	qid_newobj.type = _9P_QTTMP;
	/** @todo BUGAZOMEU For wanting of something better */
	qid_newobj.version = 0;
	qid_newobj.path = fileid;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RMKNOD);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, qid_newobj);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "TMKNOD: tag=%u fid=%u name=%.*s major=%u minor=%u qid=(type=%u,version=%u,path=%llu)",
		 (u32) *msgtag, *fid, *name_len, name_str, *major, *minor,
		 qid_newobj.type, qid_newobj.version,
		 (unsigned long long)qid_newobj.path);

	return 1;
}
