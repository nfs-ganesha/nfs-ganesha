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
 * \file    9p_setattr.c
 * \brief   9P version
 *
 * 9p_setattr.c : _9P_interpretor, request SETATTR
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

int _9p_setattr(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *valid = NULL;
	u32 *mode = NULL;
	u32 *uid = NULL;
	u32 *gid = NULL;
	u64 *size = NULL;
	u64 *atime_sec = NULL;
	u64 *atime_nsec = NULL;
	u64 *mtime_sec = NULL;
	u64 *mtime_nsec = NULL;

	struct _9p_fid *pfid = NULL;

	struct fsal_attrlist fsalattr;
	fsal_status_t fsal_status;

	struct timeval t;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);

	_9p_getptr(cursor, valid, u32);
	_9p_getptr(cursor, mode, u32);
	_9p_getptr(cursor, uid, u32);
	_9p_getptr(cursor, gid, u32);
	_9p_getptr(cursor, size, u64);
	_9p_getptr(cursor, atime_sec, u64);
	_9p_getptr(cursor, atime_nsec, u64);
	_9p_getptr(cursor, mtime_sec, u64);
	_9p_getptr(cursor, mtime_nsec, u64);

	LogDebug(COMPONENT_9P,
		 "TSETATTR: tag=%u fid=%u valid=0x%x mode=0%o uid=%u gid=%u size=%"
		 PRIu64 " atime=(%llu|%llu) mtime=(%llu|%llu)", (u32) *msgtag,
		 *fid, *valid, *mode, *uid, *gid, *size,
		 (unsigned long long)*atime_sec,
		 (unsigned long long)*atime_nsec,
		 (unsigned long long)*mtime_sec,
		 (unsigned long long)*mtime_nsec);

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

	/* If a "time" change is required, but not with the "_set" suffix,
	 * use gettimeofday */
	if (*valid &
	    (_9P_SETATTR_ATIME | _9P_SETATTR_CTIME | _9P_SETATTR_MTIME)) {
		if (gettimeofday(&t, NULL) == -1) {
			LogMajor(COMPONENT_9P,
				 "TSETATTR: tag=%u fid=%u ERROR !! gettimeofday returned -1 with errno=%u",
				 (u32) *msgtag, *fid, errno);
			return _9p_rerror(req9p, msgtag, errno,
					  plenout, preply);
		}
	}

	/* Let's do the job */
	memset((char *)&fsalattr, 0, sizeof(fsalattr));

	if (*valid & _9P_SETATTR_MODE) {
		fsalattr.valid_mask |= ATTR_MODE;
		fsalattr.mode = *mode;
	}

	if (*valid & _9P_SETATTR_UID) {
		fsalattr.valid_mask |= ATTR_OWNER;
		fsalattr.owner = *uid;
	}

	if (*valid & _9P_SETATTR_GID) {
		fsalattr.valid_mask |= ATTR_GROUP;
		fsalattr.group = *gid;
	}

	if (*valid & _9P_SETATTR_SIZE) {
		fsalattr.valid_mask |= ATTR_SIZE;
		fsalattr.filesize = *size;
	}

	if (*valid & _9P_SETATTR_ATIME) {
		fsalattr.valid_mask |= ATTR_ATIME;
		fsalattr.atime.tv_sec = t.tv_sec;
		fsalattr.atime.tv_nsec = t.tv_usec * 1000;
	}

	if (*valid & _9P_SETATTR_MTIME) {
		fsalattr.valid_mask |= ATTR_MTIME;
		fsalattr.mtime.tv_sec = t.tv_sec;
		fsalattr.mtime.tv_nsec = t.tv_usec * 1000;
	}

	if (*valid & _9P_SETATTR_CTIME) {
		fsalattr.valid_mask |= ATTR_CTIME;
		fsalattr.ctime.tv_sec = t.tv_sec;
		fsalattr.ctime.tv_nsec = t.tv_usec * 1000;
	}

	if (*valid & _9P_SETATTR_ATIME_SET) {
		fsalattr.valid_mask |= ATTR_ATIME;
		fsalattr.atime.tv_sec = *atime_sec;
		fsalattr.atime.tv_nsec = *atime_nsec;
	}

	if (*valid & _9P_SETATTR_MTIME_SET) {
		fsalattr.valid_mask |= ATTR_MTIME;
		fsalattr.mtime.tv_sec = *mtime_sec;
		fsalattr.mtime.tv_nsec = *mtime_nsec;
	}

	/* Now set the attr */
	fsal_status = fsal_setattr(pfid->pentry, false, pfid->state, &fsalattr);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&fsalattr);

	if (FSAL_IS_ERROR(fsal_status))
		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status), plenout,
				  preply);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RSETATTR);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RSETATTR: tag=%u fid=%u valid=0x%x mode=0%o uid=%u gid=%u size=%"
		 PRIu64 " atime=(%llu|%llu) mtime=(%llu|%llu)", (u32) *msgtag,
		 *fid, *valid, *mode, *uid, *gid, *size,
		 (unsigned long long)*atime_sec,
		 (unsigned long long)*atime_nsec,
		 (unsigned long long)*mtime_sec,
		 (unsigned long long)*mtime_nsec);

	/* _9p_stat_update(*pmsgtype, TRUE, &pwkrdata->stats._9p_stat_req); */
	return 1;
}
