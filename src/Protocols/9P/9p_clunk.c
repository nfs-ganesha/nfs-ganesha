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
 * \file    9p_clunk.c
 * \brief   9P version
 *
 * 9p_clunk.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "9p.h"

int _9p_clunk(struct _9p_request_data *req9p, void *worker_data,
	      u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	int rc;

	struct _9p_fid *pfid = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);

	LogDebug(COMPONENT_9P, "TCLUNK: tag=%u fid=%u", (u32) *msgtag, *fid);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "clunk request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	rc = _9p_tools_clunk(pfid);
	req9p->pconn->fids[*fid] = NULL;

	if (rc) {
		return _9p_rerror(req9p, worker_data, msgtag, rc,
				  plenout, preply);
	}

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RCLUNK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "RCLUNK: tag=%u fid=%u", (u32) *msgtag, *fid);

	return 1;
}
