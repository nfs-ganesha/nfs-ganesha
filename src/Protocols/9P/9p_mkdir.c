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
 * \file    9p_mkdir.c
 * \brief   9P version
 *
 * 9p_mkdir.c : _9P_interpretor, request MKDIR
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

int _9p_mkdir(struct _9p_request_data *req9p, void *worker_data,
	      u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *mode = NULL;
	u32 *gid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;

	struct _9p_fid *pfid = NULL;
	struct _9p_qid qid_newdir;

	cache_entry_t *pentry_newdir = NULL;
	char dir_name[MAXNAMLEN];
	uint64_t fileid;
	cache_inode_status_t cache_status;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, mode, u32);
	_9p_getptr(cursor, gid, u32);

	LogDebug(COMPONENT_9P,
		 "TMKDIR: tag=%u fid=%u name=%.*s mode=0%o gid=%u",
		 (u32) *msgtag, *fid, *name_len, name_str, *mode, *gid);

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

	snprintf(dir_name, MAXNAMLEN, "%.*s", *name_len, name_str);

	op_ctx = &pfid->op_context;
	/* Create the directory */
	/* BUGAZOMEU: @todo : the gid parameter is not used yet */
	cache_status =
	    cache_inode_create(pfid->pentry, dir_name, DIRECTORY, *mode, NULL,
			       &pentry_newdir);
	if (pentry_newdir == NULL)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	/* This is not a TATTACH fid */
	pfid->from_attach = FALSE;

	cache_status = cache_inode_fileid(pentry_newdir, &fileid);

	/* put the entry:
	 * we don't want to remember it even if cache_inode_fileid fails. */
	cache_inode_put(pentry_newdir);

	if (cache_status != CACHE_INODE_SUCCESS)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	/* Build the qid */
	qid_newdir.type = _9P_QTDIR;
	qid_newdir.version = 0;
	qid_newdir.path = fileid;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RMKDIR);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, qid_newdir);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RMKDIR: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu)",
		 (u32) *msgtag, *fid, *name_len, name_str, qid_newdir.type,
		 qid_newdir.version, (unsigned long long)qid_newdir.path);

	return 1;
}
