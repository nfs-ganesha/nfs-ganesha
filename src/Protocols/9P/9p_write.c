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
 * \file    9p_write.c
 * \brief   9P version
 *
 * 9p_write.c : _9P_interpretor, request WRITE
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
#include "fsal.h"
#include "9p.h"
#include "server_stats.h"
#include "client_mgr.h"

int _9p_write(struct _9p_request_data *req9p, void *worker_data,
	      u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u64 *offset = NULL;
	u32 *count = NULL;

	u32 outcount = 0;

	struct _9p_fid *pfid = NULL;

	size_t size;
	size_t written_size = 0;
	bool eof_met;
	cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
	/* bool sync = true; */
	bool sync = false;

	char *databuffer = NULL;

	/* fsal_status_t fsal_status; */

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, offset, u64);
	_9p_getptr(cursor, count, u32);

	databuffer = cursor;

	LogDebug(COMPONENT_9P, "TWRITE: tag=%u fid=%u offset=%llu count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];

	/* Make sure the requested amount of data respects negotiated msize */
	if (*count + _9P_ROOM_TWRITE > req9p->pconn->msize)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	op_ctx = &pfid->op_context;

	/* Do the job */
	size = *count;

	if (pfid->specdata.xattr.xattr_content != NULL) {
		memcpy(pfid->specdata.xattr.xattr_content + (*offset),
		       databuffer, size);
		pfid->specdata.xattr.xattr_offset += size;
		pfid->specdata.xattr.xattr_write = TRUE;

		/* ADD CODE TO DETECT GAP */
#if 0
		fsal_status =
		    pfid->pentry->obj_handle->ops->setextattr_value_by_id(
			pfid->pentry->obj_handle,
			&pfid->op_context,
			pfid->specdata.xattr.xattr_id,
			xattrval, size + 1);

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno
					  (cache_inode_error_convert
					   (fsal_status)), plenout, preply);
#endif

		outcount = *count;
	} else {
		cache_status =
		    cache_inode_rdwr(pfid->pentry, CACHE_INODE_WRITE, *offset,
				     size, &written_size, databuffer, &eof_met,
				     &sync);

		/* Get the handle, for stats */
		struct gsh_client *client = req9p->pconn->client;

		if (client == NULL) {
			LogDebug(COMPONENT_9P,
				 "Cannot get client block for 9P request");
		} else {
			pfid->op_context.client = client;

			server_stats_io_done(size,
					     written_size,
					     (cache_status ==
					      CACHE_INODE_SUCCESS) ?
					      true : false,
					     true);
		}

		if (cache_status != CACHE_INODE_SUCCESS)
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);

		outcount = (u32) written_size;

	}

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RWRITE);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, outcount, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RWRITE: tag=%u fid=%u offset=%llu input count=%u output count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count,
		 outcount);

/**
 * @todo write statistics accounting goes here
 * modeled on nfs I/O stats
 */
	return 1;
}
