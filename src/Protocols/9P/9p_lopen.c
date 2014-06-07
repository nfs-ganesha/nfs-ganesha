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
 * \file    9p_lopen.c
 * \brief   9P version
 *
 * 9p_lopen.c : _9P_interpretor, request LOPEN
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
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "fsal.h"
#include "9p.h"

int _9p_lopen(struct _9p_request_data *req9p, void *worker_data,
	      u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *flags = NULL;

	cache_inode_status_t cache_status;
	fsal_openflags_t openflags = 0;

	struct _9p_fid *pfid = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, flags, u32);

	LogDebug(COMPONENT_9P, "TLOPEN: tag=%u fid=%u flags=0x%x",
		 (u32) *msgtag, *fid, *flags);

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

	_9p_openflags2FSAL(flags, &openflags);
	op_ctx = &pfid->op_context;
	if (pfid->pentry->type == REGULAR_FILE) {
		/** @todo: Maybe other types (FIFO, SOCKET,...) require
		 * to be opened too */
		if (!atomic_postinc_uint32_t(&pfid->opens)) {
			cache_status = cache_inode_inc_pin_ref(pfid->pentry);
			if (cache_status != CACHE_INODE_SUCCESS)
				return _9p_rerror(req9p, worker_data, msgtag,
						  _9p_tools_errno(cache_status),
						  plenout, preply);
		}

		cache_status =
		    cache_inode_open(pfid->pentry, openflags, 0);
		if (cache_status != CACHE_INODE_SUCCESS)
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);

	}

	/* iounit = 0 by default */
	pfid->specdata.iounit = _9P_IOUNIT;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RLOPEN);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, pfid->qid);
	_9p_setptr(cursor, &pfid->specdata.iounit, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RLOPEN: tag=%u fid=%u qid=(type=%u,version=%u,path=%llu) iounit=%u",
		 *msgtag, *fid, (u32) pfid->qid.type, pfid->qid.version,
		 (unsigned long long)pfid->qid.path, pfid->specdata.iounit);

	return 1;
}
