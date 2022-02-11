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
#include "fsal.h"
#include "9p.h"

int _9p_auth(struct _9p_request_data *req9p, u32 *plenout, char *preply)
{
	char *cursor = req9p->_9pmsg + _9P_HDR_SIZE + _9P_TYPE_SIZE;
	u16 *msgtag = NULL;
	u32 *afid = NULL;
	u16 *uname_len = NULL;
	char *uname_str = NULL;
	u16 *aname_len = NULL;
	char *aname_str = NULL;
	u32 *n_aname = NULL;


	/* Get data */
	_9p_getptr(cursor, msgtag, u16);
	_9p_getptr(cursor, afid, u32);
	_9p_getstr(cursor, uname_len, uname_str);
	_9p_getstr(cursor, aname_len, aname_str);
	_9p_getptr(cursor, n_aname, u32);

	LogDebug(COMPONENT_9P,
		 "TAUTH: tag=%u afid=%d uname='%.*s' aname='%.*s' n_uname=%d",
		 (u32) *msgtag, *afid, (int) *uname_len, uname_str,
		 (int) *aname_len, aname_str, *n_aname);

	if (*afid >= _9P_FID_PER_CONN)
		return _9p_rerror(req9p, msgtag, ERANGE, plenout, preply);

	/* This message is not implemented yet, return ENOTSUPP */
	return _9p_rerror(req9p, msgtag, EOPNOTSUPP, plenout, preply);
}
