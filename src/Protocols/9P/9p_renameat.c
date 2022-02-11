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
 * \file    9p_renameat.c
 * \brief   9P version
 *
 * 9p_renameat.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "nfs_exports.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"

int _9p_renameat(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *oldfid = NULL;
	u16 *oldname_len = NULL;
	char *oldname_str = NULL;
	u32 *newfid = NULL;
	u16 *newname_len = NULL;
	char *newname_str = NULL;

	struct _9p_fid *poldfid = NULL;
	struct _9p_fid *pnewfid = NULL;

	fsal_status_t fsal_status;

	char oldname[MAXNAMLEN+1];
	char newname[MAXNAMLEN+1];

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, oldfid, u32);
	_9p_getstr(cursor, oldname_len, oldname_str);
	_9p_getptr(cursor, newfid, u32);
	_9p_getstr(cursor, newname_len, newname_str);

	LogDebug(COMPONENT_9P,
		 "TRENAMEAT: tag=%u oldfid=%u oldname=%.*s newfid=%u newname=%.*s",
		 (u32) *msgtag, *oldfid, *oldname_len, oldname_str, *newfid,
		 *newname_len, newname_str);

	if (*oldfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	poldfid = req9p->pconn->fids[*oldfid];

	/* Check that it is a valid fid */
	if (poldfid == NULL || poldfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *oldfid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	_9p_init_opctx(poldfid, req9p);

	if (*newfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pnewfid = req9p->pconn->fids[*newfid];

	/* Check that it is a valid fid */
	if (pnewfid == NULL || pnewfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *newfid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	/* Check that poldfid and pnewfid are in the same export. */
	if (poldfid->fid_export != NULL && pnewfid->fid_export != NULL &&
	    poldfid->fid_export->export_id != pnewfid->fid_export->export_id) {
		LogDebug(COMPONENT_9P,
			 "request on oldfid=%u and newfid=%u crosses exports",
			 *oldfid, *newfid);
		return _9p_rerror(req9p, msgtag, EXDEV, plenout, preply);
	}

	if ((op_ctx->export_perms.options & EXPORT_OPTION_WRITE_ACCESS) == 0)
		return _9p_rerror(req9p, msgtag, EROFS, plenout, preply);

	/* Let's do the job */
	if (*oldname_len >= sizeof(oldname)) {
		LogDebug(COMPONENT_9P, "request with names too long (%u or %u)",
			 *oldname_len, *newname_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}

	_9p_get_fname(oldname, *oldname_len, oldname_str);

	if (*newname_len >= sizeof(newname)) {
		LogDebug(COMPONENT_9P, "request with names too long (%u or %u)",
			 *oldname_len, *newname_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}

	_9p_get_fname(newname, *newname_len, newname_str);

	fsal_status = fsal_rename(poldfid->pentry, oldname, pnewfid->pentry,
				  newname);
	if (FSAL_IS_ERROR(fsal_status))
		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status), plenout,
				  preply);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RRENAMEAT);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RRENAMEAT: tag=%u oldfid=%u oldname=%.*s newfid=%u newname=%.*s",
		 (u32) *msgtag, *oldfid, *oldname_len, oldname_str, *newfid,
		 *newname_len, newname_str);

	return 1;
}
