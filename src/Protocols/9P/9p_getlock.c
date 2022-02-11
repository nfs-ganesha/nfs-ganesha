// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * \file    9p_getlock.c
 * \brief   9P version
 *
 * 9p_getlock.c : _9P_interpretor, request GETLOCK
 *
 *
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "nfs_core.h"
#include "abstract_mem.h"
#include "log.h"
#include "sal_functions.h"
#include "fsal.h"
#include "9p.h"

int _9p_getlock(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *fid = NULL;
	u8 *type = NULL;
	u64 *start = NULL;
	u64 *length = NULL;
	u32 *proc_id = NULL;
	u16 *client_id_len = NULL;
	char *client_id_str = NULL;

/*   struct _9p_fid * pfid = NULL ; */

	/* Get data */
	_9p_getptr(cursor, msgtag, u16);

	_9p_getptr(cursor, fid, u32);
	_9p_getptr(cursor, type, u8);
	_9p_getptr(cursor, start, u64);
	_9p_getptr(cursor, length, u64);
	_9p_getptr(cursor, proc_id, u32);
	_9p_getstr(cursor, client_id_len, client_id_str);

	LogDebug(COMPONENT_9P,
		 "TGETLOCK: tag=%u fid=%u type=%u start=%llu length=%llu proc_id=%u client=%.*s",
		 (u32) *msgtag, *fid, *type, (unsigned long long) *start,
		 (unsigned long long)*length, *proc_id, *client_id_len,
		 client_id_str);

	if (*fid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	/* pfid = req9p->pconn->fids[*fid] ; */

	/** @todo This function does nothing for the moment.
	 * Make it compliant with fcntl( F_GETLCK, ... */

	/* Build the reply */
	_9p_setinitptr(cursor, preply, _9P_RGETLOCK);
	_9p_setptr(cursor, msgtag, u16);

	_9p_setptr(cursor, type, u8);
	_9p_setptr(cursor, start, u64);
	_9p_setptr(cursor, length, u64);
	_9p_setptr(cursor, proc_id, u32);
	_9p_setstr(cursor, *client_id_len, client_id_str);

	_9p_setendptr(cursor, preply);
	_9p_checkbound(cursor, preply, plenout);

	LogDebug(COMPONENT_9P,
		 "RGETLOCK: tag=%u fid=%u type=%u start=%llu length=%llu proc_id=%u client=%.*s",
		 (u32) *msgtag, *fid, *type, (unsigned long long) *start,
		 (unsigned long long)*length, *proc_id, *client_id_len,
		 client_id_str);

	return 1;
}
