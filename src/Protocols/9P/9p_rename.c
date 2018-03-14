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
 * \file    9p_rename.c
 * \brief   9P version
 *
 * 9p_rename.c : _9P_interpretor, request ATTACH
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

int _9p_rename(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;

	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *dfid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;	/* for unused-but-set-variable */
	struct _9p_fid *pfid = NULL;
	struct _9p_fid *pdfid = NULL;

	char newname[MAXNAMLEN+1];
	fsal_status_t fsal_status;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, dfid, u32);
	_9p_getstr(cursor, name_len, name_str);

	LogDebug(COMPONENT_9P, "TRENAME: tag=%u fid=%u dfid=%u name=%.*s",
		 (u32) *msgtag, *fid, *dfid, *name_len, name_str);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pfid = req9p->pconn->fids[*fid];

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	_9p_init_opctx(pfid, req9p);

	if ((op_ctx->export_perms->options &
				 EXPORT_OPTION_WRITE_ACCESS) == 0)
		return _9p_rerror(req9p, msgtag, EROFS, plenout, preply);

	if (*dfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pdfid = req9p->pconn->fids[*dfid];

	/* Check that it is a valid fid */
	if (pdfid == NULL || pdfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *dfid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	/* Check that pfid and pdfid are in the same export. */
	if (pfid->fid_export != NULL && pdfid->fid_export != NULL &&
	    pfid->fid_export->export_id != pdfid->fid_export->export_id) {
		LogDebug(COMPONENT_9P,
			 "request on fid=%u and dfid=%u crosses exports",
			 *fid, *dfid);
		return _9p_rerror(req9p, msgtag, EXDEV, plenout, preply);
	}

	if (*name_len >= sizeof(newname)) {
		LogDebug(COMPONENT_9P, "request with name too long (%u)",
			 *name_len);
		return _9p_rerror(req9p, msgtag, ENAMETOOLONG, plenout,
				  preply);
	}
	snprintf(newname, sizeof(newname), "%.*s", *name_len, name_str);

	fsal_status = fsal_rename(pfid->ppentry, pfid->name, pdfid->pentry,
				  newname);
	if (FSAL_IS_ERROR(fsal_status))
		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status), plenout,
				  preply);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RRENAME);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "RRENAMEAT: tag=%u fid=%u dfid=%u newname=%.*s",
		 (u32) *msgtag, *fid, *dfid, *name_len, name_str);

	/*  _9p_stat_update(*pmsgtype, TRUE, &pwkrdata->stats._9p_stat_req); */
	return 1;
}
