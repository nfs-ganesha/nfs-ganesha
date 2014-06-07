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
 * \file    9p_lock.c
 * \brief   9P version
 *
 * 9p_lock.c : _9P_interpretor, request LOCK
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <netdb.h>
#include "nfs_core.h"
#include "log.h"
#include "cache_inode.h"
#include "export_mgr.h"
#include "sal_functions.h"
#include "fsal.h"
#include "9p.h"

/*
 * Reminder:
 * LOCK_TYPE_RDLCK = 0
 * LOCK_TYPE_WRLCK = 1
 * LOCK_TYPE_UNLCK = 2
 */
char *strtype[] = { "RDLOCK", "WRLOCK", "UNLOCK" };

/*
 * Remonder:
 * LOCK_SUCCESS = 0
 * LOCK_BLOCKED = 1
 * LOCK_ERROR   = 2
 * LOCK_GRACE   = 3
 */
char *strstatus[] = { "SUCCESS", "BLOCKED", "ERROR", "GRACE" };

int _9p_lock(struct _9p_request_data *req9p, void *worker_data,
	     u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u8 *type = NULL;
	u32 *flags = NULL;
	u64 *start = NULL;
	u64 *length = NULL;
	u32 *proc_id = NULL;
	u16 *client_id_len = NULL;
	char *client_id_str = NULL;

	u8 status = _9P_LOCK_SUCCESS;

	state_status_t state_status = STATE_SUCCESS;
	state_owner_t *holder;
	state_owner_t *powner;
	state_t state;
	fsal_lock_param_t lock;
	fsal_lock_param_t conflict;

	char name[MAXNAMLEN];

	struct hostent *hp;
	struct sockaddr_storage client_addr;

	struct _9p_fid *pfid = NULL;

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, type, u8);
	_9p_getptr(cursor, flags, u32);
	_9p_getptr(cursor, start, u64);
	_9p_getptr(cursor, length, u64);
	_9p_getptr(cursor, proc_id, u32);
	_9p_getstr(cursor, client_id_len, client_id_str);

	LogDebug(COMPONENT_9P,
		 "TLOCK: tag=%u fid=%u type=%u|%s flags=0x%x start=%llu length=%llu proc_id=%u client=%.*s",
		 (u32) *msgtag, *fid, *type, strtype[*type], *flags,
		 (unsigned long long)*start, (unsigned long long)*length,
		 *proc_id, *client_id_len, client_id_str);

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

	/* Tmp hook to avoid lock issue when compiling kernels.
	 * This should not impact ONE client only
	 * get the client's ip addr */
	snprintf(name, MAXNAMLEN, "%.*s", *client_id_len, client_id_str);

	hp = gethostbyname(name);
	if (hp == NULL)
		return _9p_rerror(req9p, worker_data, msgtag, EINVAL, plenout,
				  preply);

	memcpy((char *)&client_addr, hp->h_addr, hp->h_length);

	powner = get_9p_owner(&client_addr, *proc_id);
	if (powner == NULL)
		return _9p_rerror(req9p, worker_data, msgtag, EINVAL, plenout,
				  preply);

	/* Do the job */
	switch (*type) {
	case _9P_LOCK_TYPE_RDLCK:
	case _9P_LOCK_TYPE_WRLCK:
		/* Fill in plock */
		lock.lock_type =
		    (*type == _9P_LOCK_TYPE_WRLCK) ? FSAL_LOCK_W : FSAL_LOCK_R;
		lock.lock_start = *start;
		lock.lock_length = *length;

		if (nfs_in_grace()) {
			status = _9P_LOCK_GRACE;
			break;
		}

		state_status = state_lock(pfid->pentry,
					  powner, &state,
					  STATE_NON_BLOCKING, NULL, &lock,
					  &holder, &conflict, POSIX_LOCK);

		if (state_status == STATE_SUCCESS)
			status = _9P_LOCK_SUCCESS;
		else if (state_status == STATE_LOCK_BLOCKED)
			status = _9P_LOCK_BLOCKED;
		else
			status = _9P_LOCK_ERROR;

		break;

	case _9P_LOCK_TYPE_UNLCK:
		if (state_unlock(pfid->pentry,
				 powner, NULL, &lock,
				 POSIX_LOCK)
			    != STATE_SUCCESS)
			status = _9P_LOCK_ERROR;
		else
			status = _9P_LOCK_SUCCESS;

		break;

	default:
		return _9p_rerror(req9p, worker_data, msgtag, EINVAL, plenout,
				  preply);
		break;
	}			/* switch( *type ) */

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RLOCK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setvalue(cursor, status, u8);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RLOCK: tag=%u fid=%u type=%u|%s flags=0x%x start=%llu length=%llu proc_id=%u client=%.*s status=%u|%s",
		 (u32) *msgtag, *fid, *type, strtype[*type], *flags,
		 (unsigned long long) *start, (unsigned long long) *length,
		 *proc_id, *client_id_len, client_id_str, status,
		 strstatus[status]);

	return 1;
}
