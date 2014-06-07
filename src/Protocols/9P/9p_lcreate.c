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
 * \file    9p_lcreate.c
 * \brief   9P version
 *
 * 9p_lcreate.c : _9P_interpretor, request LCREATE
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

int _9p_lcreate(struct _9p_request_data *req9p, void *worker_data,
		u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *flags = NULL;
	u32 *mode = NULL;
	u32 *gid = NULL;
	u16 *name_len = NULL;
	char *name_str = NULL;

	struct _9p_fid *pfid = NULL;
	struct _9p_qid qid_newfile;
	u32 iounit = _9P_IOUNIT;

	cache_entry_t *pentry_newfile = NULL;
	char file_name[MAXNAMLEN];
	int64_t fileid;
	cache_inode_status_t cache_status;
	fsal_openflags_t openflags = 0;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getstr(cursor, name_len, name_str);
	_9p_getptr(cursor, flags, u32);
	_9p_getptr(cursor, mode, u32);
	_9p_getptr(cursor, gid, u32);

	LogDebug(COMPONENT_9P,
		 "TLCREATE: tag=%u fid=%u name=%.*s flags=0%o mode=0%o gid=%u",
		 (u32) *msgtag, *fid, *name_len, name_str, *flags, *mode,
		 *gid);

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
	snprintf(file_name, MAXNAMLEN, "%.*s", *name_len, name_str);

	/* Create the file */

	/* BUGAZOMEU: @todo : the gid parameter is not used yet,
	 * flags is not yet used */
	cache_status =
	    cache_inode_create(pfid->pentry, file_name, REGULAR_FILE, *mode,
			       NULL, &pentry_newfile);
	if (pentry_newfile == NULL)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	cache_status =
	    cache_inode_fileid(pentry_newfile, &fileid);
	if (cache_status != CACHE_INODE_SUCCESS)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	_9p_openflags2FSAL(flags, &openflags);

	cache_status = cache_inode_open(pentry_newfile, openflags, 0);
	if (cache_status != CACHE_INODE_SUCCESS) {
		/* Owner override..
		 * deal with this stupid 04xy mode corner case */
		/** @todo this is racy, use cache_inode_lock_trust_attrs
		 *        also, this owner override check is not
		 *        strictly true, this is not the only mode
		 *        that could be in play.
		 */
		if ((cache_status == CACHE_INODE_FSAL_EACCESS)
		    && (pfid->op_context.creds->caller_uid ==
			pentry_newfile->obj_handle->attributes.owner)
		    && ((*mode & 0400) == 0400)) {
			/* If we reach this piece of code, this means that
			 * a user did open( O_CREAT, 04xy) on a file the
			 * file was created in 04xy mode by the forme
			 * cache_inode code, but for the mode is 04xy
			 * the user is not allowed to open it.
			 * Becoming root override this */
			uid_t saved_uid = pfid->op_context.creds->caller_uid;

			/* Become root */
			pfid->op_context.creds->caller_uid = 0;

			/* Do the job as root */
			cache_status =
			    cache_inode_open(pentry_newfile, openflags, 0);

			/* Back to standard user */
			pfid->op_context.creds->caller_uid = saved_uid;

			if (cache_status != CACHE_INODE_SUCCESS)
				return _9p_rerror(req9p, worker_data, msgtag,
						  _9p_tools_errno(cache_status),
						  plenout, preply);
		} else
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno(cache_status),
					  plenout, preply);
	}

	/* This is not a TATTACH fid */
	pfid->from_attach = FALSE;

	/* Pin as well. We probably want to close the file if this fails,
	 * but it won't happen - right?! */
	cache_status = cache_inode_inc_pin_ref(pentry_newfile);
	if (cache_status != CACHE_INODE_SUCCESS)
		return _9p_rerror(req9p, worker_data, msgtag,
				  _9p_tools_errno(cache_status), plenout,
				  preply);

	/* put parent directory entry */
	cache_inode_put(pfid->pentry);

	/* Build the qid */
	qid_newfile.type = _9P_QTFILE;
	qid_newfile.version = 0;
	qid_newfile.path = fileid;

	iounit = 0;		/* default value */

	/* The fid will represent the new file now - we can't fail anymore */
	pfid->pentry = pentry_newfile;
	pfid->qid = qid_newfile;
	pfid->specdata.xattr.xattr_id = 0;
	pfid->specdata.xattr.xattr_content = NULL;
	pfid->opens = 1;

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RLCREATE);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setqid(cursor, qid_newfile);
	_9p_setvalue(cursor, iounit, u32);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RLCREATE: tag=%u fid=%u name=%.*s qid=(type=%u,version=%u,path=%llu) iounit=%u pentry=%p",
		 (u32) *msgtag, *fid, *name_len, name_str, qid_newfile.type,
		 qid_newfile.version, (unsigned long long)qid_newfile.path,
		 iounit, pfid->pentry);

	return 1;
}
