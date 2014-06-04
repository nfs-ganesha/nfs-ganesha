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
 * \file    9p_unlinkat.c
 * \brief   9P version
 *
 * 9p_unlinkat.c : _9P_interpretor, request ATTACH
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
#include "fsal.h"
#include "9p.h"

int _9p_unlinkat(struct _9p_request_data *req9p, void *worker_data,
		 u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *dfid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;
	__attribute__ ((unused)) u32 *flags = NULL;

	struct _9p_fid *pdfid = NULL;

	cache_inode_status_t cache_status;
	char name[MAXNAMLEN];

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, dfid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, flags, u32);

	LogDebug(COMPONENT_9P, "TUNLINKAT: tag=%u dfid=%u name=%.*s",
		 (u32) *msgtag, *dfid, *name_len, name_str);

	if (*dfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pdfid = req9p->pconn->fids[*dfid];

	/* Check that it is a valid fid */
	if (pdfid == NULL || pdfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *dfid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	op_ctx = &pdfid->op_context;

	/* Let's do the job */
	snprintf(name, MAXNAMLEN, "%.*s", *name_len, name_str);

	cache_status = cache_inode_remove(pdfid->pentry, name);
	if (cache_status != CACHE_INODE_SUCCESS)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RUNLINKAT);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "TUNLINKAT: tag=%u dfid=%u name=%.*s",
		 (u32) *msgtag, *dfid, *name_len, name_str);

	return 1;
}
