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
 * \file    9p_attach.c
 * \brief   9P version
 *
 * 9p_attach.c : _9P_interpretor, request ATTACH
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "export_mgr.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "fsal.h"
#include "9p.h"

int _9p_attach(struct _9p_request_data *req9p, void *worker_data,
	       u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *afid = NULL;
	u16 *uname_len = NULL;
	char *uname_str = NULL;
	u16 *aname_len = NULL;
	char *aname_str = NULL;
	u32 *n_uname = NULL;

	uint64_t fileid;

	u32 err = 0;

	struct _9p_fid *pfid = NULL;

	struct gsh_export *export;
	cache_inode_status_t cache_status;
	char exppath[MAXPATHLEN];

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, afid, u32);
	_9p_getstr(cursor, uname_len, uname_str);
	_9p_getstr(cursor, aname_len, aname_str);
	_9p_getptr(cursor, n_uname, u32);

	LogDebug(COMPONENT_9P,
		 "TATTACH: tag=%u fid=%u afid=%d uname='%.*s' aname='%.*s' n_uname=%d",
		 (u32) *msgtag, *fid, *afid, (int) *uname_len, uname_str,
		 (int) *aname_len, aname_str, *n_uname);

	/*
	 * Find the export for the aname (using as well Path or Tag)
	 */
	snprintf(exppath, MAXPATHLEN, "%.*s", (int)*aname_len, aname_str);

	if (exppath[0] == '/')
		export = get_gsh_export_by_path(exppath, false);
	else
		export = get_gsh_export_by_tag(exppath);

	/* Did we find something ? */
	if (export == NULL) {
		err = ENOENT;
		goto errout;
	}

	if (*fid >= _9P_FID_PER_CONN) {
		err = ERANGE;
		goto errout;
	}

	/* Set export and fid id in fid */
	pfid = gsh_calloc(1, sizeof(struct _9p_fid));
	if (pfid == NULL) {
		err = ENOMEM;
		goto errout;
	}

	pfid->fid = *fid;
	req9p->pconn->fids[*fid] = pfid;

	/* Is user name provided as a string or as an uid ? */
	if (*n_uname != _9P_NONUNAME) {
		/* Build the fid creds */
		err = _9p_tools_get_req_context_by_uid(*n_uname, pfid);
		if (err != 0) {
			err = -err;
			goto errout;
		}
	} else if (*uname_len != 0) {
		/* Build the fid creds */
		err = _9p_tools_get_req_context_by_name(*uname_len, uname_str,
							pfid);
		if (err != 0) {
			err = -err;
			goto errout;
		}
	} else {
		/* No n_uname nor uname */
		err = EINVAL;
		goto errout;
	}

	/* Check if root cache entry is correctly set, fetch it, and
	 * take an LRU reference.
	 */
	cache_status = nfs_export_get_root_entry(export, &pfid->pentry);
	if (cache_status != CACHE_INODE_SUCCESS) {
		err = _9p_tools_errno(cache_status);
		goto errout;
	}

	/* Keep track of the export in the req_ctx */
	pfid->op_context.export = export;
	pfid->op_context.fsal_export = export->fsal_export;

	op_ctx =  &pfid->op_context;
	/* This fid is a special one: it comes from TATTACH */
	pfid->from_attach = TRUE;

	cache_status = cache_inode_fileid(pfid->pentry, &fileid);
	if (cache_status != CACHE_INODE_SUCCESS) {
		err = _9p_tools_errno(cache_status);
		goto errout;
	}

	/* Compute the qid */
	pfid->qid.type = _9P_QTDIR;
	pfid->qid.version = 0;	/* No cache, we want the client
				 * to stay synchronous with the server */
	pfid->qid.path = fileid;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RATTACH);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, pfid->qid);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RATTACH: tag=%u fid=%u qid=(type=%u,version=%u,path=%llu)",
		 *msgtag, *fid, (u32) pfid->qid.type, pfid->qid.version,
		 (unsigned long long)pfid->qid.path);

	return 1;

errout:

	if (export != NULL)
		put_gsh_export(export);
	if (pfid != NULL) {
		if (pfid->pentry != NULL)
			cache_inode_put(pfid->pentry);
		gsh_free(pfid);
	}

	return _9p_rerror(req9p, worker_data, msgtag, err, plenout, preply);

}
