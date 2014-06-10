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
 * \file    9p_xattrwalk.c
 * \brief   9P version
 *
 * 9p_xattrwalk.c : _9P_interpretor, request XATTRWALK
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "cache_inode_lru.h"
#include "fsal.h"
#include "9p.h"

#define XATTRS_ARRAY_LEN 100

int _9p_xattrwalk(struct _9p_request_data *req9p, void *worker_data,
		  u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u32 *attrfid = NULL;
	u16 *name_len;
	char *name_str;
	u64 attrsize = 0LL;

	fsal_status_t fsal_status;
	char name[MAXNAMLEN];
	fsal_xattrent_t xattrs_arr[XATTRS_ARRAY_LEN];
	int eod_met = false;
	unsigned int nb_xattrs_read = 0;
	unsigned int i = 0;
	char *xattr_cursor = NULL;
	unsigned int tmplen = 0;

	struct _9p_fid *pfid = NULL;
	struct _9p_fid *pxattrfid = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, attrfid, u32);

	LogDebug(COMPONENT_9P, "TXATTRWALK: tag=%u fid=%u attrfid=%u",
		 (u32) *msgtag, *fid, *attrfid);

	_9p_getstr(cursor, name_len, name_str);

	if (*name_len == 0)
		LogDebug(COMPONENT_9P,
			 "TXATTRWALK (component): tag=%u fid=%u attrfid=%u name=(LIST XATTR)",
			 (u32) *msgtag, *fid, *attrfid);
	else
		LogDebug(COMPONENT_9P,
			 "TXATTRWALK (component): tag=%u fid=%u attrfid=%u name=%.*s",
			 (u32) *msgtag, *fid, *attrfid, *name_len, name_str);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	if (*attrfid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, worker_data, msgtag, ERANGE, plenout,
				  preply);

	pfid = req9p->pconn->fids[*fid];
	/* Check that it is a valid fid */
	if (pfid == NULL || pfid->pentry == NULL) {
		LogDebug(COMPONENT_9P, "request on invalid fid=%u", *fid);
		return _9p_rerror(req9p, worker_data, msgtag, EIO, plenout,
				  preply);
	}

	pxattrfid = gsh_calloc(1, sizeof(struct _9p_fid));
	if (pxattrfid == NULL)
		return _9p_rerror(req9p, worker_data, msgtag, ENOMEM, plenout,
				  preply);

	/* set op_ctx, it will be useful if FSAL is later called */
	op_ctx = &pfid->op_context;

	/* Initiate xattr's fid by copying file's fid in it */
	memcpy((char *)pxattrfid, (char *)pfid, sizeof(struct _9p_fid));

	snprintf(name, MAXNAMLEN, "%.*s", *name_len, name_str);

	pxattrfid->specdata.xattr.xattr_content = gsh_malloc(XATTR_BUFFERSIZE);
	if (pxattrfid->specdata.xattr.xattr_content == NULL) {
		gsh_free(pxattrfid);
		return _9p_rerror(req9p, worker_data, msgtag, ENOMEM, plenout,
				  preply);
	}

	if (*name_len == 0) {
		/* xattrwalk is used with an empty name,
		 * this is a listxattr request */
		fsal_status =
		    pxattrfid->pentry->obj_handle->ops->list_ext_attrs(
			pxattrfid->pentry->obj_handle,
			FSAL_XATTR_RW_COOKIE,	/* Start with RW cookie,
						 * hiding RO ones */
			 xattrs_arr,
			 XATTRS_ARRAY_LEN, /** @todo fix static length */
			 &nb_xattrs_read,
			 &eod_met);

		if (FSAL_IS_ERROR(fsal_status)) {
			gsh_free(pxattrfid->specdata.xattr.xattr_content);
			gsh_free(pxattrfid);
			return _9p_rerror(req9p, worker_data, msgtag,
					  _9p_tools_errno
					  (cache_inode_error_convert
					   (fsal_status)), plenout, preply);
		}

		/* if all xattrent are not read,
		 * returns ERANGE as listxattr does */
		if (eod_met != TRUE) {
			gsh_free(pxattrfid->specdata.xattr.xattr_content);
			gsh_free(pxattrfid);
			return _9p_rerror(req9p, worker_data, msgtag, ERANGE,
					  plenout, preply);
		}

		xattr_cursor = pxattrfid->specdata.xattr.xattr_content;
		attrsize = 0LL;
		for (i = 0; i < nb_xattrs_read; i++) {
			tmplen =
			    snprintf(xattr_cursor, MAXNAMLEN, "%s",
				     xattrs_arr[i].xattr_name);
			xattr_cursor[tmplen] = '\0';	/* Just to be sure */
			/* +1 for trailing '\0' */
			xattr_cursor += tmplen + 1;
			attrsize += tmplen + 1;

			/* Make sure not to go beyond the buffer */
			if (attrsize > XATTR_BUFFERSIZE) {
				gsh_free(pxattrfid->specdata.xattr.
					 xattr_content);
				gsh_free(pxattrfid);
				return _9p_rerror(req9p, worker_data, msgtag,
						  ERANGE, plenout, preply);
			}
		}
	} else {
		/* xattrwalk has a non-empty name, use regular setxattr */
		fsal_status =
		    pxattrfid->pentry->obj_handle->ops->
		    getextattr_id_by_name(pxattrfid->pentry->obj_handle,
					  name,
					  &pxattrfid->specdata.xattr.xattr_id);


		if (FSAL_IS_ERROR(fsal_status)) {
			gsh_free(pxattrfid->specdata.xattr.xattr_content);
			gsh_free(pxattrfid);

			/* ENOENT for xattr is ENOATTR */
			if (fsal_status.major == ERR_FSAL_NOENT)
				return _9p_rerror(req9p, worker_data, msgtag,
						  ENOATTR, plenout, preply);
			else
				return _9p_rerror(req9p, worker_data, msgtag,
						  _9p_tools_errno
						  (cache_inode_error_convert
						   (fsal_status)), plenout,
						  preply);
		}

		fsal_status =
		    pxattrfid->pentry->obj_handle->ops->
		    getextattr_value_by_name(pxattrfid->pentry->obj_handle,
					     name,
					     pxattrfid->specdata.xattr.
					     xattr_content, XATTR_BUFFERSIZE,
					     &attrsize);

		if (FSAL_IS_ERROR(fsal_status)) {

			/* Hook dedicated to ACL management. When attributes
			 * system.posix_acl_access is used, it can't be
			 * created, but can be written anyway.
			 * To do this, return ENODATA instead of ENOATTR
			 * In this case, we do created what's needed to
			 * setxattr() into the special xattr */
			if (fsal_status.minor == ENODATA) {
				if (!strncmp(name,
					     "system.posix_acl_access",
					     MAXNAMLEN))
					attrsize = 0LL;
				else if (!strncmp(name,
						  "security.selinux",
						  MAXNAMLEN))
					attrsize = 0LL;
				else {
					gsh_free(pxattrfid->specdata.
						xattr.xattr_content);
					gsh_free(pxattrfid);
					return _9p_rerror(req9p,
							  worker_data,
							  msgtag,
							  ENODATA,
							  plenout,
							  preply);
				}
			} else {
				gsh_free(pxattrfid->specdata.
					 xattr.xattr_content);
				gsh_free(pxattrfid);

				return _9p_rerror(req9p,
						  worker_data,
						  msgtag,
						  _9p_tools_errno
						  (cache_inode_error_convert
						   (fsal_status)),
						  plenout,
						  preply);
			}
		}
	}

	req9p->pconn->fids[*attrfid] = pxattrfid;

	/* Increments refcount as we're manually making a new copy */
	cache_inode_lru_ref(pfid->pentry, LRU_FLAG_NONE);

	/* fid doesn't come from attach, don't put export on clunk... */
	pxattrfid->from_attach = FALSE;

	/* hold reference on gdata */
	uid2grp_hold_group_data(pxattrfid->gdata);

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RXATTRWALK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, attrsize, u64);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RXATTRWALK: tag=%u fid=%u attrfid=%u name=%.*s size=%llu",
		 (u32) *msgtag, *fid, *attrfid, *name_len, name_str,
		 (unsigned long long)attrsize);

	return 1;
}				/* _9p_xattrwalk */
