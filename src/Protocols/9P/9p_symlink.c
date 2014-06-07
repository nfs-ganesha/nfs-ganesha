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
 * \file    9p_symlink.c
 * \brief   9P version
 *
 * 9p_symlink.c : _9P_interpretor, request SYMLINK
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

int _9p_symlink(struct _9p_request_data *req9p, void *worker_data,
		u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;
	u16 *linkcontent_len = NULL;
	char *linkcontent_str = NULL;
	u32 *gid = NULL;

	struct _9p_fid *pfid = NULL;
	struct _9p_qid qid_symlink;

	cache_entry_t *pentry_symlink = NULL;
	char symlink_name[MAXNAMLEN];
	uint64_t fileid;
	cache_inode_status_t cache_status;
	uint32_t mode = 0777;
	cache_inode_create_arg_t create_arg;

	memset(&create_arg, 0, sizeof(create_arg));

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getstr(cursor, linkcontent_len, linkcontent_str);
	_9p_getptr(cursor, gid, u32);

	LogDebug(COMPONENT_9P,
		 "TSYMLINK: tag=%u fid=%u name=%.*s linkcontent=%.*s gid=%u",
		 (u32) *msgtag, *fid, *name_len, name_str, *linkcontent_len,
		 linkcontent_str, *gid);

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

	op_ctx = &pfid->op_context;
	snprintf(symlink_name, MAXNAMLEN, "%.*s", *name_len, name_str);

	create_arg.link_content = gsh_malloc(MAXPATHLEN);
	if (create_arg.link_content == NULL)
		return _9p_rerror(req9p, worker_data, msgtag, EFAULT, plenout,
				  preply);

	snprintf(create_arg.link_content, MAXPATHLEN, "%.*s", *linkcontent_len,
		 linkcontent_str);

	/* Let's do the job */
	/* BUGAZOMEU: @todo : the gid parameter is not used yet,
	 * flags is not yet used */
	cache_status =
	    cache_inode_create(pfid->pentry, symlink_name, SYMBOLIC_LINK, mode,
			       &create_arg, &pentry_symlink);

	if (create_arg.link_content != NULL)
		gsh_free(create_arg.link_content);
	if (pentry_symlink == NULL) {
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);
	}

	/* This is not a TATTACH fid */
	pfid->from_attach = FALSE;

	cache_status = cache_inode_fileid(pentry_symlink, &fileid);

	/* put the entry:
	 * we don't want to remember it even if cache_inode_fileid fails. */
	cache_inode_put(pentry_symlink);

	if (cache_status != CACHE_INODE_SUCCESS) {
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);
	}

	/* Build the qid */
	qid_symlink.type = _9P_QTSYMLINK;
	qid_symlink.version = 0;
	qid_symlink.path = fileid;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RSYMLINK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, qid_symlink);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RSYMLINK: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu)",
		 (u32) *msgtag, *fid, *name_len, name_str, qid_symlink.type,
		 qid_symlink.version, (unsigned long long)qid_symlink.path);

	return 1;
}
