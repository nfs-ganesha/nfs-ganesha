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
 * \file    9p_readdir.c
 * \brief   9P version
 *
 * 9p_readdir.c : _9P_interpretor, request READDIR
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
#include "abstract_mem.h"

char pathdot[] = ".";
char pathdotdot[] = "..";

struct _9p_cb_entry {
	u64 qid_path;
	u8 *qid_type;
	char d_type;	/* Attention, this is a VFS d_type, not a 9P type */
	const char *name_str;
	u16 name_len;
	uint64_t cookie;
};

struct _9p_cb_data {
	u8 *cursor;
	unsigned int count;
	unsigned int max;
};

static inline u8 *fill_entry(u8 *cursor, u8 qid_type, u64 qid_path, u64 cookie,
			     u8 d_type, u16 name_len, const char *name_str)
{
	/* qid in 3 parts */
	_9p_setvalue(cursor, qid_type, u8);	/* 9P entry type */
	/* qid_version set to 0 to prevent the client from caching */
	_9p_setvalue(cursor, 0, u32);
	_9p_setvalue(cursor, qid_path, u64);

	/* offset */
	_9p_setvalue(cursor, cookie, u64);

	/* Type (this time outside the qid) - VFS d_type (like in getdents) */
	_9p_setvalue(cursor, d_type, u8);

	/* name */
	_9p_setstr(cursor, name_len, name_str);

	return cursor;
}

static cache_inode_status_t _9p_readdir_callback(void *opaque,
						 cache_entry_t *entry,
						 const struct attrlist *attr,
						 uint64_t mounted_on_fileid)
{
	struct cache_inode_readdir_cb_parms *cb_parms = opaque;
	struct _9p_cb_data *tracker = cb_parms->opaque;
	int name_len = strlen(cb_parms->name);
	u8 qid_type, d_type;

	if (tracker == NULL) {
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}

	if (tracker->count + 24 + name_len > tracker->max) {
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}

	switch (attr->type) {
	case FIFO_FILE:
		qid_type = _9P_QTFILE;
		d_type = DT_FIFO;
		break;

	case CHARACTER_FILE:
		qid_type = _9P_QTFILE;
		d_type = DT_CHR;
		break;

	case BLOCK_FILE:
		qid_type = _9P_QTFILE;
		d_type = DT_BLK;
		break;

	case REGULAR_FILE:
		qid_type = _9P_QTFILE;
		d_type = DT_REG;
		break;

	case SOCKET_FILE:
		qid_type = _9P_QTFILE;
		d_type = DT_SOCK;
		break;

	case DIRECTORY:
		qid_type = _9P_QTDIR;
		d_type = DT_DIR;
		break;

	case SYMBOLIC_LINK:
		qid_type = _9P_QTSYMLINK;
		d_type = DT_LNK;
		break;

	default:
		cb_parms->in_result = false;
		return CACHE_INODE_SUCCESS;
	}

	/* Add 13 bytes in recsize for qid + 8 bytes for offset + 1 for type
	 * + 2 for strlen = 24 bytes */
	tracker->count += 24 + name_len;

	tracker->cursor =
	    fill_entry(tracker->cursor, qid_type, attr->fileid,
		       cb_parms->cookie, d_type, name_len, cb_parms->name);

	cb_parms->in_result = true;
	return CACHE_INODE_SUCCESS;
}

int _9p_readdir(struct _9p_request_data *req9p, void *worker_data,
		u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	struct _9p_cb_data tracker;

	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u64 *offset = NULL;
	u32 *count = NULL;

	u32 dcount = 0;

	char *dcount_pos = NULL;

	cache_inode_status_t cache_status;
	bool eod_met;
	cache_entry_t *pentry_dot_dot = NULL;

	uint64_t cookie = 0LL;
	unsigned int num_entries = 0;

	struct _9p_fid *pfid = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, offset, u64);
	_9p_getptr(cursor, count, u32);

	LogDebug(COMPONENT_9P, "TREADDIR: tag=%u fid=%u offset=%llu count=%u",
		 (u32) *msgtag, *fid, (unsigned long long)*offset, *count);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];

	/* Make sure the requested amount of data respects negotiated msize */
	if (*count + _9P_ROOM_RREADDIR > req9p->pconn->msize)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	op_ctx = &pfid->op_context;

	/* For each entry, returns:
	 * qid     = 13 bytes
	 * offset  = 8 bytes
	 * type    = 1 byte
	 * namelen = 2 bytes
	 * namestr = ~16 bytes (average size)
	 * -------------------
	 * total   = ~40 bytes (average size) per dentry */

	if (*count < 52)	/* require room for . and .. */
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);

	/* Build the reply - it'll just be overwritten if error */
	_9p_setinitptr(cursor, preply, _9P_RREADDIR);
	_9p_setptr(cursor, msgtag, u16);

	/* Remember dcount position for later use */
	_9p_savepos(cursor, dcount_pos, u32);

	/* Is this the first request ? */
	if (*offset == 0LL) {
		/* compute the parent entry */
		cache_status =
		    cache_inode_lookupp(pfid->pentry, &pentry_dot_dot);
		if (pentry_dot_dot == NULL)
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);

		/* Deal with "." and ".." */
		cursor =
		    fill_entry(cursor, _9P_QTDIR,
			       pfid->pentry->obj_handle->attributes.fileid, 1LL,
			       DT_DIR, strlen(pathdot), pathdot);
		dcount += 24 + strlen(pathdot);

		cursor =
		    fill_entry(cursor, _9P_QTDIR,
			       pentry_dot_dot->obj_handle->attributes.fileid,
			       2LL, DT_DIR, strlen(pathdotdot), pathdotdot);
		dcount += 24 + strlen(pathdotdot);

		/* put the parent */
		cache_inode_put(pentry_dot_dot);

		cookie = 0LL;
	} else if (*offset == 1LL) {
		/* compute the parent entry */
		cache_status =
		    cache_inode_lookupp(pfid->pentry, &pentry_dot_dot);
		if (pentry_dot_dot == NULL)
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);

		cursor =
		    fill_entry(cursor, _9P_QTDIR,
			       pentry_dot_dot->obj_handle->attributes.fileid,
			       2LL, DT_DIR, strlen(pathdotdot), pathdotdot);
		dcount += 24 + strlen(pathdotdot);

		/* put the parent */
		cache_inode_put(pentry_dot_dot);

		cookie = 0LL;
	} else if (*offset == 2LL) {
		cookie = 0LL;
	} else {
		cookie = (uint64_t) (*offset);
	}

	tracker.cursor = cursor;
	tracker.count = dcount;
	tracker.max = *count;

	cache_status = cache_inode_readdir(pfid->pentry, cookie, &num_entries,
					   &eod_met,
					   0,	/* no attr */
					   _9p_readdir_callback, &tracker);
	if (cache_status != CACHE_INODE_SUCCESS) {
		/* The avl lookup will try to get the next entry after 'cookie'.
		 * If none is found CACHE_INODE_NOT_FOUND is returned
		 * In the 9P logic, this situation just mean
		 * "end of directory reached" */
		if (cache_status != CACHE_INODE_NOT_FOUND)
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);
	}

	cursor = tracker.cursor;

	/* Set buffsize in previously saved position */
	_9p_setvalue(dcount_pos, tracker.count, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P, "RREADDIR: tag=%u fid=%u dcount=%u",
		 (u32) *msgtag, *fid, dcount);

	return 1;
}
