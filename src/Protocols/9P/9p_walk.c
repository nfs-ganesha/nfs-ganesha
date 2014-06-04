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
 * \file    9p_walk.c
 * \brief   9P version
 *
 * 9p_walk.c : _9P_interpretor, request WALK
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

int _9p_walk(struct _9p_request_data *req9p, void *worker_data,
	     u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	unsigned int i = 0;

	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *newfid = NULL;
	u16 *nwname = NULL;
	u16 *wnames_len;
	char *wnames_str;
	uint64_t fileid;
	cache_inode_status_t cache_status;
	cache_entry_t *pentry = NULL;
	char name[MAXNAMLEN];

	u16 *nwqid;

	struct _9p_fid *pfid = NULL;
	struct _9p_fid *pnewfid = NULL;

	/* Now Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, newfid, u32);
	_9p_getptr(cursor, nwname, u16);

	LogDebug(COMPONENT_9P, "TWALK: tag=%u fid=%u newfid=%u nwname=%u",
		 (u32) *msgtag, *fid, *newfid, *nwname);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	if (*newfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];
	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}
	op_ctx = &pfid->op_context;
	pnewfid = gsh_calloc(1, sizeof(struct _9p_fid));
	if (pnewfid == NULL)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	/* Is this a lookup or a fid cloning operation ? */
	if (*nwname == 0) {
		/* Cloning operation */
		memcpy((char *)pnewfid, (char *)pfid, sizeof(struct _9p_fid));

		/* Set the new fid id */
		pnewfid->fid = *newfid;

		/* This is not a TATTACH fid */
		pnewfid->from_attach = FALSE;

		/* Increments refcount */
		cache_inode_lru_ref(pnewfid->pentry, LRU_FLAG_NONE);
	} else {
		/* the walk is in fact a lookup */
		pentry = pfid->pentry;

		for (i = 0; i < *nwname; i++) {
			_9p_getstr(cursor, wnames_len, wnames_str);
			snprintf(name, MAXNAMLEN, "%.*s", *wnames_len,
				 wnames_str);

			LogDebug(COMPONENT_9P,
				 "TWALK (lookup): tag=%u fid=%u newfid=%u (component %u/%u :%s)",
				 (u32) *msgtag, *fid, *newfid, i + 1, *nwname,
				 name);

			if (pnewfid->pentry == pentry)
				pnewfid->pentry = NULL;

			/* refcount +1 */
			cache_status =
			    cache_inode_lookup(pentry, name, &pnewfid->pentry);

			if (pnewfid->pentry == NULL) {
				gsh_free(pnewfid);
				return _9p_rerror(req9p, worker_data, msgtag,
						  _9p_tools_errno(cache_status),
						  plenout, preply);
			}

			if (pentry != pfid->pentry)
				cache_inode_put(pentry);

			pentry = pnewfid->pentry;
		}

		pnewfid->fid = *newfid;
		pnewfid->op_context = pfid->op_context;
		pnewfid->ppentry = pfid->pentry;
		strncpy(pnewfid->name, name, MAXNAMLEN-1);

		/* gdata ref is not hold : the pfid, which use same gdata */
		/*  will be clunked after pnewfid */
		/* This clunk release the gdata */
		pnewfid->gdata = pfid->gdata;

		/* This is not a TATTACH fid */
		pnewfid->from_attach = FALSE;

		cache_status = cache_inode_fileid(pnewfid->pentry, &fileid);
		if (cache_status != CACHE_INODE_SUCCESS) {
			gsh_free(pnewfid);
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);
		}

		/* Build the qid */
		/* No cache, we want the client to stay synchronous
		 * with the server */
		pnewfid->qid.version = 0;
		pnewfid->qid.path = fileid;

		pnewfid->specdata.xattr.xattr_id = 0;
		pnewfid->specdata.xattr.xattr_content = NULL;

		switch (pnewfid->pentry->type) {
		case REGULAR_FILE:
		case CHARACTER_FILE:
		case BLOCK_FILE:
		case SOCKET_FILE:
		case FIFO_FILE:
			pnewfid->qid.type = _9P_QTFILE;
			break;

		case SYMBOLIC_LINK:
			pnewfid->qid.type = _9P_QTSYMLINK;
			break;

		case DIRECTORY:
			pnewfid->qid.type = _9P_QTDIR;
			break;

		default:
			LogMajor(COMPONENT_9P,
				 "implementation error, you should not see this message !!!!!!");
			gsh_free(pnewfid);
			return _9p_rerror(req9p, worker_data, msgtag, EINVAL,
					  plenout, preply);
			break;
		}

	}

	/* keep info on new fid */
	req9p->pconn->fids[*newfid] = pnewfid;

	/* As much qid as requested fid */
	nwqid = nwname;

	/* Hold refcount on gdata */
	uid2grp_hold_group_data(pnewfid->gdata);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RWALK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setptr(cursor, nwqid, u16);
	for (i = 0; i < *nwqid; i++) {
		/** @todo: should be different qids
		 * for each directory walked through */
		_9p_setqid(cursor, pnewfid->qid);
	}

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RWALK: tag=%u fid=%u newfid=%u nwqid=%u fileid=%llu pentry=%p refcount=%i",
		 (u32) *msgtag, *fid, *newfid, *nwqid,
		 (unsigned long long)pnewfid->qid.path, pnewfid->pentry,
		 pnewfid->pentry->lru.refcnt);

	return 1;
}
