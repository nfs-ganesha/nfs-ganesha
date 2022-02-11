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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_lcreate.c
 * \brief   9P version
 *
 * 9p_lcreate.c : _9P_interpretor, request LCREATE
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

int _9p_lcreate(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *flags = NULL;
	u32 *mode = NULL;
	u32 *gid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;

	struct _9p_fid *pfid = NULL;
	struct _9p_qid qid_newfile;
	u32 iounit = _9P_IOUNIT;

	struct fsal_obj_handle *pentry_newfile = NULL;
	char file_name[MAXNAMLEN+1];
	fsal_status_t fsal_status;
	fsal_openflags_t openflags = 0;

	struct fsal_attrlist sattr;
	fsal_verifier_t verifier;
	enum fsal_create_mode createmode = FSAL_UNCHECKED;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, flags, u32);
	_9p_getptr(cursor, mode, u32);
	_9p_getptr(cursor, gid, u32);

	LogDebug(COMPONENT_9P,
		 "TLCREATE: tag=%u fid=%u name=%.*s flags=0%o mode=0%o gid=%u",
		 (u32) *msgtag, *fid, *name_len, name_str, *flags, *mode,
		 *gid);

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

	if (*name_len >= sizeof(file_name)) {
		LogDebug(COMPONENT_9P, "request with name too long (%u)",
			 *name_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}

	_9p_get_fname(file_name, *name_len, name_str);

	_9p_openflags2FSAL(flags, &openflags);
	pfid->state->state_data.fid.share_access =
		_9p_openflags_to_share_access(flags);

	memset(&verifier, 0, sizeof(verifier));

	memset(&sattr, 0, sizeof(sattr));
	sattr.valid_mask = ATTR_MODE | ATTR_GROUP;
	sattr.mode = *mode;
	sattr.group = *gid;

	if (*flags & 0x10) {
		/* Filesize is already 0. */
		sattr.valid_mask |= ATTR_SIZE;
	}

	if (*flags & 0x1000) {
		/* If OEXCL, use FSAL_EXCLUSIVE_9P create mode
		 * so that we can pass the attributes specified
		 * above. Verifier is ignored for this create mode
		 * because we don't have to deal with retry.
		 */
		createmode = FSAL_EXCLUSIVE_9P;
	}

	fsal_status = fsal_open2(pfid->pentry,
				 pfid->state,
				 openflags,
				 createmode,
				 file_name,
				 &sattr,
				 verifier,
				 &pentry_newfile,
				 NULL);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status))
		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status),
				  plenout, preply);

	/* put parent directory entry */
	pfid->pentry->obj_ops->put_ref(pfid->pentry);

	/* Build the qid */
	qid_newfile.type = _9P_QTFILE;
	qid_newfile.version = 0;
	qid_newfile.path = pentry_newfile->fileid;

	/* The fid will represent the new file now - we can't fail anymore */
	pfid->pentry = pentry_newfile;
	pfid->qid = qid_newfile;
	pfid->xattr = NULL;
	pfid->opens = 1;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RLCREATE);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, qid_newfile);
	_9p_setvalue(cursor, iounit, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RLCREATE: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu) iounit=%u pentry=%p",
		 (u32) *msgtag, *fid, *name_len, name_str, qid_newfile.type,
		 qid_newfile.version, (unsigned long long)qid_newfile.path,
		 iounit, pfid->pentry);

	return 1;
}
