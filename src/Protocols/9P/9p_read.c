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
 * \file    9p_read.c
 * \brief   9P version
 *
 * 9p_read.c : _9P_interpretor, request READ
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
#include "server_stats.h"
#include "client_mgr.h"

int _9p_read(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	char *databuffer;

	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u64 *offset = NULL;
	u32 *count = NULL;
	u32 outcount = 0;

	struct _9p_fid *pfid = NULL;

	size_t read_size = 0;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, offset, u64);
	_9p_getptr(cursor, count, u32);

	LogDebug(COMPONENT_9P, "TREAD: tag=%u fid=%u offset=%llu count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pfid = req9p->pconn->fids[*fid];

	/* Make sure the requested amount of data respects negotiated msize */
	if (*count + _9P_ROOM_RREAD > req9p->pconn->msize)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	_9p_init_opctx(pfid, req9p);

	/* Start building the reply already
	 * So we don't need to use an intermediate data buffer
	 */
	_9p_setinitptr(cursor, preply, _9P_RREAD);
	_9p_setptr(cursor, msgtag, u16);
	databuffer = _9p_getbuffertofill(cursor);

	/* Do the job */
	if (pfid->xattr != NULL) {
		/* Copy the value cached during xattrwalk */
		if (*offset > pfid->xattr->xattr_size)
			return _9p_rerror(req9p, msgtag, EINVAL, plenout,
					  preply);
		if (pfid->xattr->xattr_write != _9P_XATTR_READ_ONLY)
			return _9p_rerror(req9p, msgtag, EINVAL, plenout,
					  preply);

		read_size = MIN(*count,
				pfid->xattr->xattr_size - *offset);
		memcpy(databuffer,
		       pfid->xattr->xattr_content + *offset,
		       read_size);

		outcount = read_size;
	} else {
		struct async_process_data read_data;
		struct fsal_io_arg *read_arg = alloca(sizeof(*read_arg) +
							sizeof(struct iovec));

		read_arg->info = NULL;
		read_arg->state = pfid->state;
		read_arg->offset = *offset;
		read_arg->iov_count = 1;
		read_arg->iov[0].iov_len = *count;
		read_arg->iov[0].iov_base = databuffer;
		read_arg->io_amount = 0;
		read_arg->end_of_file = false;

		read_data.ret.major = 0;
		read_data.ret.minor = 0;
		read_data.done = false;
		read_data.cond = req9p->cond;
		read_data.mutex = req9p->mutex;

		/* Do the actual read */
		fsal_read(pfid->pentry, true, read_arg, &read_data);

		if (req9p->pconn->client) {
			op_ctx->client = req9p->pconn->client;

			server_stats_io_done(read_arg->iov[0].iov_len,
					     read_arg->io_amount,
					     FSAL_IS_ERROR(read_data.ret),
					     false);
		}

		if (FSAL_IS_ERROR(read_data.ret))
			return _9p_rerror(req9p, msgtag,
					  _9p_tools_errno(read_data.ret),
					  plenout, preply);

		outcount = (u32) read_arg->io_amount;
	}
	_9p_setfilledbuffer(cursor, outcount);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "RREAD: tag=%u fid=%u offset=%llu count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count);

/**
 * @todo read statistics accounting goes here
 * modeled on nfs I/O statistics
 */
	return 1;
}
