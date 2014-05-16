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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    9p_getattr.c
 * \brief   9P version
 *
 * 9p_getattr.c : _9P_interpretor, request ATTACH
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
#include "export_mgr.h"
#include "fsal.h"
#include "9p.h"

int _9p_getattr(struct _9p_request_data *req9p, void *worker_data,
		u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u64 *request_mask = NULL;

	struct _9p_fid *pfid = NULL;

	u64 valid = 0LL;	/* Not a pointer */
	u32 mode = 0;		/* Not a pointer */
	u32 uid = 0;
	u32 gid = 0;
	u64 nlink = 0LL;
	u64 rdev = 0LL;
	u64 size = 0LL;
	u64 blksize = 0LL;	/* Be careful, this one is no pointer */
	u64 blocks = 0LL;	/* And so does this one...            */
	u64 atime_sec = 0LL;
	u64 atime_nsec = 0LL;
	u64 mtime_sec = 0LL;
	u64 mtime_nsec = 0LL;
	u64 ctime_sec = 0LL;
	u64 ctime_nsec = 0LL;
	u64 btime_sec = 0LL;
	u64 btime_nsec = 0LL;
	u64 gen = 0LL;
	u64 data_version = 0LL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, request_mask, u64);

	LogDebug(COMPONENT_9P, "TGETATTR: tag=%u fid=%u request_mask=0x%llx",
		 (u32) *msgtag, *fid, (unsigned long long) *request_mask);

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

	/* Attach point is found, build the requested attributes */

	valid = _9P_GETATTR_BASIC;	/* FSAL covers all basic attributes */

	if (*request_mask & _9P_GETATTR_RDEV) {
		mode = (u32) pfid->pentry->obj_handle->attributes.mode;
		if (pfid->pentry->obj_handle->attributes.type == DIRECTORY)
			mode |= __S_IFDIR;
		if (pfid->pentry->obj_handle->attributes.type == REGULAR_FILE)
			mode |= __S_IFREG;
		if (pfid->pentry->obj_handle->attributes.type == SYMBOLIC_LINK)
			mode |= __S_IFLNK;
		if (pfid->pentry->obj_handle->attributes.type == SOCKET_FILE)
			mode |= __S_IFSOCK;
		if (pfid->pentry->obj_handle->attributes.type == BLOCK_FILE)
			mode |= __S_IFBLK;
		if (pfid->pentry->obj_handle->attributes.type == CHARACTER_FILE)
			mode |= __S_IFCHR;
		if (pfid->pentry->obj_handle->attributes.type == FIFO_FILE)
			mode |= __S_IFIFO;
	} else
		mode = 0;

	/** @todo this is racy, use cache_inode_lock_trust_attrs */
	uid =
	    (*request_mask & _9P_GETATTR_UID) ?
		(u32) pfid->pentry->obj_handle->attributes.owner :
		0;
	gid =
	    (*request_mask & _9P_GETATTR_GID) ?
		(u32) pfid->pentry->obj_handle->attributes.group :
		0;
	nlink =
	    (*request_mask & _9P_GETATTR_NLINK) ?
		(u64) pfid->pentry->obj_handle->attributes.numlinks :
		0LL;
	/* rdev = (*request_mask & _9P_GETATTR_RDEV) ?
	 *     (u64) pfid->pentry->obj_handle->attributes.rawdev.major :
	 *     0LL; */
	rdev =
	    (*request_mask & _9P_GETATTR_RDEV) ?
		(u64) pfid->op_context.export->filesystem_id.major :
		0LL;
	size =
	    (*request_mask & _9P_GETATTR_SIZE) ?
		(u64) pfid->pentry->obj_handle->attributes.filesize :
		0LL;
	blksize =
	    (*request_mask & _9P_GETATTR_BLOCKS) ? (u64) _9P_BLK_SIZE : 0LL;
	blocks =
	    (*request_mask & _9P_GETATTR_BLOCKS) ? (u64) (pfid->pentry->
							  obj_handle->
							  attributes.filesize /
							  DEV_BSIZE) : 0LL;
	atime_sec =
	    (*request_mask & _9P_GETATTR_ATIME) ?
		(u64) pfid->pentry->obj_handle->attributes.atime.tv_sec :
		0LL;
	atime_nsec = 0LL;
	mtime_sec =
	    (*request_mask & _9P_GETATTR_MTIME) ?
		(u64) pfid->pentry->obj_handle->attributes.mtime.tv_sec :
		0LL;
	mtime_nsec = 0LL;
	ctime_sec =
	    (*request_mask & _9P_GETATTR_CTIME) ?
		(u64) pfid->pentry->obj_handle->attributes.ctime.tv_sec :
		0LL;
	ctime_nsec = 0LL;

	/* Not yet supported attributes */
	btime_sec = 0LL;
	btime_nsec = 0LL;
	gen = 0LL;
	data_version = 0LL;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RGETATTR);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, valid, u64);
	_9p_setqid(cursor, pfid->qid);
	_9p_setvalue(cursor, mode, u32);
	_9p_setvalue(cursor, uid, u32);
	_9p_setvalue(cursor, gid, u32);
	_9p_setvalue(cursor, nlink, u64);
	_9p_setvalue(cursor, rdev, u64);
	_9p_setvalue(cursor, size, u64);
	_9p_setvalue(cursor, blksize, u64);
	_9p_setvalue(cursor, blocks, u64);
	_9p_setvalue(cursor, atime_sec, u64);
	_9p_setvalue(cursor, atime_nsec, u64);
	_9p_setvalue(cursor, mtime_sec, u64);
	_9p_setvalue(cursor, mtime_nsec, u64);
	_9p_setvalue(cursor, ctime_sec, u64);
	_9p_setvalue(cursor, ctime_nsec, u64);
	_9p_setvalue(cursor, btime_sec, u64);
	_9p_setvalue(cursor, btime_nsec, u64);
	_9p_setvalue(cursor, gen, u64);
	_9p_setvalue(cursor, data_version, u64);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RGETATTR: tag=%u valid=0x%"PRIx64" qid=(type=%u,version=%u,"
		 "path=%"PRIu64") mode=0%o uid=%u gid=%u nlink=%"PRIu64
		 " rdev=%"PRIu64" size=%"PRIu64" blksize=%"PRIu64
		 " blocks=%"PRIu64" atime=(%"PRIu64",%"PRIu64") mtime=(%"PRIu64
		 ",%"PRIu64") ctime=(%"PRIu64",%"PRIu64") btime=(%"PRIu64
		 ",%"PRIu64") gen=%"PRIu64", data_version=%"PRIu64, *msgtag,
		 valid, pfid->qid.type, pfid->qid.version, pfid->qid.path, mode,
		 uid, gid, nlink, rdev, size, blksize, blocks, atime_sec,
		 atime_nsec, mtime_sec, mtime_nsec, ctime_sec, ctime_nsec,
		 btime_sec, btime_nsec, gen, data_version);

	return 1;
}
