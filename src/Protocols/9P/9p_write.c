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
#include "nfs_exports.h"
#include "log.h"
#include "fsal.h"
#include "9p.h"
#include "server_stats.h"
#include "client_mgr.h"

int _9p_write(struct _9p_request_data *req9p, u32 *plenout, char *preply)
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

	char *databuffer = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, offset, u64);
	_9p_getptr(cursor, count, u32);

	databuffer = cursor;

	LogDebug(COMPONENT_9P, "TWRITE: tag=%u fid=%u offset=%llu count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	pfid = req9p->pconn->fids[*fid];

	/* Make sure the requested amount of data respects negotiated msize */
	if (*count + _9P_ROOM_TWRITE > req9p->pconn->msize)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, msgtag, EIO, plenout, preply);
	}

	_9p_init_opctx(pfid, req9p);

	if ((op_ctx->export_perms->options &
				 EXPORT_OPTION_WRITE_ACCESS) == 0)
		return _9p_rerror(req9p, msgtag, EROFS, plenout, preply);

	/* Do the job */
	size = *count;

	if (pfid->xattr != NULL) {
		if (*offset > pfid->xattr->xattr_size)
			return _9p_rerror(req9p, msgtag, EINVAL, plenout,
					  preply);
		if (pfid->xattr->xattr_write != _9P_XATTR_CAN_WRITE &&
		    pfid->xattr->xattr_write != _9P_XATTR_DID_WRITE)
			return _9p_rerror(req9p, msgtag, EINVAL, plenout,
					  preply);

		written_size = MIN(*count,
				   pfid->xattr->xattr_size - *offset);

		memcpy(pfid->xattr->xattr_content + *offset,
		       databuffer, written_size);
		pfid->xattr->xattr_offset += size;
		pfid->xattr->xattr_write = _9P_XATTR_DID_WRITE;

		/* ADD CODE TO DETECT GAP */
#if 0
		fsal_status =
		    pfid->pentry->ops->setextattr_value_by_id(
			pfid->pentry,
			&pfid->op_context,
			pfid->xattr->xattr_id,
			xattrval, size + 1);

		if (FSAL_IS_ERROR(fsal_status))
			return _9p_rerror(req9p, msgtag,
					  _9p_tools_errno(fsal_status), plenout,
					  preply);
#endif

		outcount = written_size;
	} else {
		struct async_process_data write_data;
		struct fsal_io_arg *write_arg = alloca(sizeof(*write_arg) +
						      sizeof(struct iovec));

		write_arg->info = NULL;
		write_arg->state = pfid->state;
		write_arg->offset = *offset;
		write_arg->iov_count = 1;
		write_arg->iov[0].iov_len = size;
		write_arg->iov[0].iov_base = databuffer;
		write_arg->io_amount = 0;
		write_arg->fsal_stable = false;


		write_data.ret.major = 0;
		write_data.ret.minor = 0;
		write_data.done = false;
		write_data.cond = req9p->cond;
		write_data.mutex = req9p->mutex;

		/* Do the actual write */
		fsal_write(pfid->pentry, true, write_arg, &write_data);

		if (req9p->pconn->client) {
			op_ctx->client = req9p->pconn->client;

			server_stats_io_done(write_arg->iov[0].iov_len,
					     write_arg->io_amount,
					     FSAL_IS_ERROR(write_data.ret),
					     false);
		}

		if (FSAL_IS_ERROR(write_data.ret))
			return _9p_rerror(req9p, msgtag,
					  _9p_tools_errno(write_data.ret),
					  plenout, preply);

		outcount = (u32) write_arg->io_amount;

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
