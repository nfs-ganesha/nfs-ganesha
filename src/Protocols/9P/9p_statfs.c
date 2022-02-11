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
 * \file    9p_statfs.c
 * \brief   9P version
 *
 * 9p_statfs.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"

int _9p_statfs(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;

	struct _9p_fid *pfid = NULL;

	u32 type = 0x01021997;	/* V9FS_MAGIC */
	u32 bsize = 1;		/* fsal_statfs and
				 * FSAL already care for blocksize */
	u64 *blocks = NULL;
	u64 *bfree = NULL;
	u64 *bavail = NULL;
	u64 *files = NULL;
	u64 *ffree = NULL;
	u64 fsid = 0LL;

	u32 namelen = MAXNAMLEN;

	fsal_dynamicfsinfo_t dynamicinfo;
	fsal_status_t fsal_status;
	struct fsal_attrlist attrs;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);

	LogDebug(COMPONENT_9P, "TSTATFS: tag=%u fid=%u", (u32) *msgtag, *fid);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pfid = req9p->pconn->fids[*fid];
	if (pfid == NULL)
		return _9p_rerror(req9p, msgtag, EINVAL, plenout, preply);
	_9p_init_opctx(pfid, req9p);

	/* Get the obj's attributes */
	fsal_prepare_attrs(&attrs, ATTRS_NFS3);

	fsal_status = pfid->pentry->obj_ops->getattrs(pfid->pentry, &attrs);

	if (FSAL_IS_ERROR(fsal_status)) {
		/* Done with the attrs */
		fsal_release_attrs(&attrs);

		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status), plenout,
				  preply);
	}

	/* Get the FS's stats */
	fsal_status = fsal_statfs(pfid->pentry, &dynamicinfo);
	if (FSAL_IS_ERROR(fsal_status)) {
		/* Done with the attrs */
		fsal_release_attrs(&attrs);

		return _9p_rerror(req9p, msgtag,
				  _9p_tools_errno(fsal_status), plenout,
				  preply);
	}

	blocks = (u64 *) &dynamicinfo.total_bytes;
	bfree = (u64 *) &dynamicinfo.free_bytes;
	bavail = (u64 *) &dynamicinfo.avail_bytes;
	files = (u64 *) &dynamicinfo.total_files;
	ffree = (u64 *) &dynamicinfo.free_files;
	fsid = (u64) attrs.rawdev.major;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RSTATFS);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, type, u32);
	_9p_setvalue(cursor, bsize, u32);
	_9p_setptr(cursor, blocks, u64);
	_9p_setptr(cursor, bfree, u64);
	_9p_setptr(cursor, bavail, u64);
	_9p_setptr(cursor, files, u64);
	_9p_setptr(cursor, ffree, u64);
	_9p_setvalue(cursor, fsid, u64);
	_9p_setvalue(cursor, namelen, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "RSTATFS: tag=%u fid=%u", (u32) *msgtag, *fid);

	return 1;
}
